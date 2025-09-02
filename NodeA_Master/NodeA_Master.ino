#include <SoftwareSerial.h>
#include <Wire.h>
#include <U8g2lib.h>

// 핀 정의
#define PUMP_RELAY 3
#define VALVE_RELAY 4
#define BUZZER 5
#define START_BTN 6
#define STOP_BTN 7
#define HUMIDITY_SET_BTN 12  // D12 설정 습도 조절 버튼
#define RS485_DE 2
#define RS485_RX 9
#define RS485_TX 10

// RS485 통신
SoftwareSerial rs485(RS485_RX, RS485_TX);

// OLED (SH1106, Page Buffer 모드)
U8G2_SH1106_128X64_NONAME_1_HW_I2C u8g2(U8G2_R0);

// 변수
bool systemRunning = false;
int humidityValues[30] = {0};  // 최근 30개 습도값 (30초 평균용)
int valueIndex = 0;
int consecutiveHigh = 0;      // 연속 고습도 횟수
int consecutiveError = 0;     // 연속 오류 횟수
unsigned long lastRequest = 0;
const unsigned long REQUEST_INTERVAL = 1000;  // 1초

// 설정 습도 관련 변수
int targetHumidity = 95;      // 설정 습도 (95%로 시작)
unsigned long lastHumidityBtnPress = 0;

// 작동시간 관련 변수
unsigned long systemStartTime = 0;  // 시스템 시작 시간
unsigned long totalRunTime = 0;      // 총 작동시간 (밀리초)
bool timeInitialized = false;        // 시간 초기화 여부

// 콤프레샤 보호 관련 변수
unsigned long sessionStartTime = 0;  // 세션 시작 시간 (최대 50분 제한용)
unsigned long lastRestTime = 0;       // 마지막 휴식 시작 시간
bool inRestMode = false;             // 휴식 모드 여부
const unsigned long MAX_SESSION_TIME = 50 * 60 * 1000;  // 50분 (밀리초)

// 시나리오 모드 관련 변수
bool scenarioMode = false;
const int MAX_SCENARIOS = 5;  // 5개로 변경 (타임 설정 시나리오)
int scrollOffset = 0;          // 스크롤 오프셋
const int DISPLAY_SCENARIOS = 4; // 한 번에 표시할 시나리오 수
unsigned long lastScenarioBtnPress = 0;
bool stopBtnPressed = false;
bool setBtnPressed = false;

// 콤프레샤 보호 관련 변수 (동적 설정용)
unsigned long WORK_TIME = 5 * 60 * 1000;    // 기본값: 5분 동작
unsigned long REST_TIME = 1 * 60 * 1000;    // 기본값: 1분 휴식
bool alwaysOnMode = false;                  // 항상 ON 모드
bool protectionReset = false;               // 보호 로직 리셋 플래그
int currentScenario = 0;                    // 기본값: 1번 시나리오 (5분/1분)

void setup() {
  Serial.begin(9600);
  
  // 핀 모드 설정
  pinMode(PUMP_RELAY, OUTPUT);
  pinMode(VALVE_RELAY, OUTPUT);
  pinMode(BUZZER, OUTPUT);
  pinMode(START_BTN, INPUT_PULLUP);
  pinMode(STOP_BTN, INPUT_PULLUP);
  pinMode(HUMIDITY_SET_BTN, INPUT_PULLUP);  // 설정 습도 버튼
  pinMode(RS485_DE, OUTPUT);
  
  // 릴레이 초기 상태 (OFF)
  digitalWrite(PUMP_RELAY, HIGH);
  digitalWrite(VALVE_RELAY, HIGH);
  
  // RS485 수신 모드로 초기화
  digitalWrite(RS485_DE, LOW);
  
  // I2C 및 OLED 초기화 (간단하게)
  Wire.begin();
  u8g2.begin();
  
  // OLED 초기 메시지
  displayHumidity(0, "INIT");
  
  // RS485 통신 시작
  rs485.begin(9600);
  
  Serial.println("inVIRUStech System Ready");
  Serial.println("Press START");
}

void loop() {
  // 시리얼 입력 확인 (부저 테스트용)
  if (Serial.available()) {
    char input = Serial.read();
    if (input == 'b' || input == 'B') {
      Serial.println("=== BUZZER TEST ===");
      testBuzzer();
    }
  }
  
  // 시나리오 모드 진입 감지 (STOP 버튼 5초 이상 누르기)
  checkScenarioModeEntry();
  
  // 시나리오 모드에서의 버튼 처리
  if (scenarioMode) {
    handleScenarioMode();
  } else {
    // 일반 모드에서의 버튼 처리
    // 버튼 입력 처리 (디바운싱 추가)
    static unsigned long lastButtonPress = 0;
    if (millis() - lastButtonPress > 300) {  // 300ms 디바운싱
      if (digitalRead(START_BTN) == LOW && !systemRunning) {
        Serial.println("START button pressed!");
        startSystem();
        lastButtonPress = millis();
      }
      
      if (digitalRead(STOP_BTN) == LOW && systemRunning) {
        Serial.println("STOP button pressed!");
        stopSystem();
        lastButtonPress = millis();
      }
    }
    
    // 설정 습도 버튼 처리 (별도 디바운싱)
    if (millis() - lastHumidityBtnPress > 300) {  // 300ms 디바운싱
      if (digitalRead(HUMIDITY_SET_BTN) == LOW) {
        adjustTargetHumidity();
        lastHumidityBtnPress = millis();
      }
    }
    
    // 콤프레샤 보호 기능 (loop에서 주기적으로 호출)
    if (systemRunning) {
      compressorProtectionISR();  // TEST: 10초 ON / 10초 OFF
    }
    
    // 일반 모드에서 OLED 표시 (RS485 통신 후)
    if (millis() - lastRequest >= REQUEST_INTERVAL) {
      requestHumidity();  // 실제 RS485 통신 활성화
      lastRequest = millis();
    }
  }
  
  // 버튼 상태 (5초마다)
  static unsigned long lastDebug = 0;
  if (millis() - lastDebug >= 5000) {
    Serial.print("Sys: ");
    Serial.println(systemRunning ? "RUN" : "STOP");
    if (scenarioMode) {
      Serial.print("Scenario Mode: ");
      Serial.println(currentScenario);
    }
    lastDebug = millis();
  }
}

// 시나리오 모드 진입 감지 (STOP 버튼 5초 이상 누르기)
void checkScenarioModeEntry() {
  static unsigned long stopBtnPressStart = 0;
  static bool stopBtnPressed = false;
  static bool scenarioEntered = false;  // 시나리오 진입 상태 관리
  
  bool stopBtn = (digitalRead(STOP_BTN) == LOW);
  
  if (stopBtn && !stopBtnPressed && !scenarioEntered) {
    // STOP 버튼이 처음 눌렸을 때
    stopBtnPressStart = millis();
    stopBtnPressed = true;
  }
  
  if (!stopBtn && stopBtnPressed) {
    // STOP 버튼이 떼졌을 때
    stopBtnPressed = false;
    scenarioEntered = false;  // 진입 상태 리셋
  }
  
  // STOP 버튼을 5초 이상 누르고 있을 때 시나리오 모드 진입
  if (stopBtn && stopBtnPressed && !scenarioMode && !scenarioEntered && (millis() - stopBtnPressStart >= 5000)) {
    scenarioMode = true;
    currentScenario = 0;
    scrollOffset = 0;
    scenarioEntered = true;  // 진입 상태 설정
    Serial.println("=== SCENARIO MODE ENTERED ===");
    
    // 시나리오 모드 진입 알림음
    for (int i = 0; i < 2; i++) {
      digitalWrite(BUZZER, HIGH);
      delay(200);
      digitalWrite(BUZZER, LOW);
      delay(200);
    }
  }
}

// 시나리오 모드 처리
void handleScenarioMode() {
  static unsigned long lastStartBtnPress = 0;
  static unsigned long lastStopBtnPress = 0;
  static bool lastStartBtn = false;
  static bool lastStopBtn = false;
  
  // 시작 버튼 처리 (500ms 디바운싱)
  if (millis() - lastStartBtnPress > 500) {
    bool startBtn = (digitalRead(START_BTN) == LOW);
    
    if (startBtn && !lastStartBtn) {
      currentScenario = (currentScenario + 1) % MAX_SCENARIOS;
      
      // 스크롤 오프셋 조정
      if (currentScenario >= scrollOffset + DISPLAY_SCENARIOS) {
        scrollOffset = currentScenario - DISPLAY_SCENARIOS + 1;
      } else if (currentScenario < scrollOffset) {
        scrollOffset = currentScenario;
      }
      
      Serial.print("Scenario selected: ");
      Serial.println(currentScenario + 1);
      
      // 시나리오 선택 알림음
      digitalWrite(BUZZER, HIGH);
      delay(100);
      digitalWrite(BUZZER, LOW);
      
      lastStartBtnPress = millis();
    }
    lastStartBtn = startBtn;
  }
  
  // 스탑 버튼 처리 (1000ms 디바운싱 - 시나리오 실행)
  if (millis() - lastStopBtnPress > 1000) {
    bool stopBtn = (digitalRead(STOP_BTN) == LOW);
    
    if (stopBtn && !lastStopBtn) {
      executeScenario(currentScenario);
      scenarioMode = false;
      Serial.println("=== SCENARIO MODE EXIT ===");
      lastStopBtnPress = millis();
    }
    lastStopBtn = stopBtn;
  }
  
  // 시나리오 모드에서 OLED 계속 표시 (매 루프마다)
  displayScenarioMode();
}

// 시나리오 실행
void executeScenario(int scenario) {
  Serial.print("Executing Scenario ");
  Serial.println(scenario + 1);
  
  switch (scenario) {
    case 0:  // 시나리오 1: 5분 ON / 1분 OFF (기본)
      WORK_TIME = 5 * 60 * 1000;
      REST_TIME = 1 * 60 * 1000;
      alwaysOnMode = false;
      break;
    case 1:  // 시나리오 2: 10분 ON / 5분 OFF
      WORK_TIME = 10 * 60 * 1000;
      REST_TIME = 5 * 60 * 1000;
      alwaysOnMode = false;
      break;
    case 2:  // 시나리오 3: 1분 ON / 30초 OFF
      WORK_TIME = 1 * 60 * 1000;
      REST_TIME = 30 * 1000;
      alwaysOnMode = false;
      break;
    case 3:  // 시나리오 4: 30초 ON / 30초 OFF
      WORK_TIME = 30 * 1000;
      REST_TIME = 30 * 1000;
      alwaysOnMode = false;
      break;
    case 4:  // 시나리오 5: 항상 ON (주의: 과부하 위험)
      alwaysOnMode = true;
      break;
  }
  
  // 시나리오 실행 시 작동시간 초기화
  totalRunTime = 0;
  timeInitialized = false;
  protectionReset = true;  // 보호 로직 리셋
  
  // 시나리오 실행 알림음
  for (int i = 0; i < 3; i++) {
    digitalWrite(BUZZER, HIGH);
    delay(150);
    digitalWrite(BUZZER, LOW);
    delay(150);
  }
  
  // 시나리오 설정 완료 후 메인 화면으로 복귀 (자동 시작하지 않음)
  Serial.println("Scenario configured. Press START to begin.");
}

// 시나리오 모드 표시
void displayScenarioMode() {
  u8g2.firstPage();
  do {
    // 제목
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(5, 12, "TIMER SCENARIO");
    
    // 시나리오 목록 (스크롤 방식)
    u8g2.setFont(u8g2_font_ncenB08_tr);
    for (int i = 0; i < DISPLAY_SCENARIOS; i++) {
      int scenarioIndex = scrollOffset + i;
      if (scenarioIndex < MAX_SCENARIOS) {
        int yPos = 25 + (i * 10);
        
        // 선택된 항목에 ">" 표시
        if (scenarioIndex == currentScenario) {
          u8g2.drawStr(5, yPos, ">");
        }
        
        // 시나리오 번호와 타임 설정
        char scenarioStr[25];
        sprintf(scenarioStr, "%d. %s", scenarioIndex + 1, getScenarioDescription(scenarioIndex));
        u8g2.drawStr(15, yPos, scenarioStr);
      }
    }
    
    // 스크롤 표시
    if (scrollOffset > 0) {
      u8g2.drawStr(120, 25, "^");
    }
    if (scrollOffset + DISPLAY_SCENARIOS < MAX_SCENARIOS) {
      u8g2.drawStr(120, 55, "v");
    }
    
  } while (u8g2.nextPage());
}

// 시나리오 설명 반환 함수
const char* getScenarioDescription(int scenario) {
  switch (scenario) {
    case 0: return "5m/1m";
    case 1: return "10m/5m";
    case 2: return "1m/30s";
    case 3: return "30s/30s";
    case 4: return "Always ON";
    default: return "5m/1m";
  }
}

void startSystem() {
  systemRunning = true;
  Serial.println("START");
  
  // 작동시간 초기화 및 시작 (정지 후 재시작 시 리셋)
  totalRunTime = 0;
  timeInitialized = true;
  systemStartTime = millis();
  
  // 세션 시작 시간 설정 (최대 50분 제한용)
  sessionStartTime = millis();
  inRestMode = false;
  
  // 보호 로직 리셋
  protectionReset = true;
  
  // NOTE: 인터럽트 사용 안 함
  
  // 경고음 (3번)
  for (int i = 0; i < 3; i++) {
    digitalWrite(BUZZER, HIGH);
    delay(200);
    digitalWrite(BUZZER, LOW);
    delay(200);
  }
  
  // 펌프와 밸브 작동
  digitalWrite(PUMP_RELAY, LOW);
  digitalWrite(VALVE_RELAY, LOW);
  
  Serial.println("RUNNING");
}

void stopSystem() {
  systemRunning = false;
  Serial.println("STOP");
  
  // 작동시간 누적
  if (timeInitialized) {
    totalRunTime += (millis() - systemStartTime);
  }
  
  // 릴레이 정지
  digitalWrite(PUMP_RELAY, HIGH);
  digitalWrite(VALVE_RELAY, HIGH);
  
  // NOTE: 인터럽트 사용 안 함
  
  // 완료 신호음 (5번 빠르게)
  for (int i = 0; i < 5; i++) {
    digitalWrite(BUZZER, HIGH);
    delay(100);
    digitalWrite(BUZZER, LOW);
    delay(100);
  }
  
  Serial.println("STOPPED");
}

// 설정 습도 조절 함수 (5%씩 증가)
void adjustTargetHumidity() {
  targetHumidity += 5;
  if (targetHumidity > 100) {
    targetHumidity = 50;  // 95% 다음은 50로 순환
  }
  
  Serial.print("Target humidity set to: ");
  Serial.print(targetHumidity);
  Serial.println("%");
  
  // 짧은 확인 부저음
  digitalWrite(BUZZER, HIGH);
  delay(50);
  digitalWrite(BUZZER, LOW);
}

// 부저 테스트 함수
void testBuzzer() {
  Serial.println("Buzzer test");
  
  // 짧은 비프음
  digitalWrite(BUZZER, HIGH);
  delay(100);
  digitalWrite(BUZZER, LOW);
  delay(100);
  
  // 경고음 패턴 (3번)
  for (int i = 0; i < 3; i++) {
    digitalWrite(BUZZER, HIGH);
    delay(200);
    digitalWrite(BUZZER, LOW);
    delay(200);
  }
  
  // 완료 신호음 패턴 (5번 빠르게)
  for (int i = 0; i < 5; i++) {
    digitalWrite(BUZZER, HIGH);
    delay(100);
    digitalWrite(BUZZER, LOW);
    delay(100);
  }
  
  Serial.println("Test done");
}

// OLED에 습도값 표시
void displayHumidity(int humidity, const char* status) {
  u8g2.firstPage();
  do {
    // 제목 (inVIRUStech로 변경)
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(5, 12, "inVIRUStech");
    
    // 우측 상단에 설정 습도 표시 (중간 크기 글꼴)
    u8g2.setFont(u8g2_font_ncenB08_tr);
    char targetStr[8];
    sprintf(targetStr, "%d%%", targetHumidity);
    int targetWidth = u8g2.getStrWidth(targetStr);
    u8g2.drawStr(128 - targetWidth - 2, 10, targetStr);
    
    // 습도값
    u8g2.setFont(u8g2_font_ncenB14_tr);
    char humidityStr[10];
    if (strcmp(status, "INIT") == 0) {
      strcpy(humidityStr, "INIT");
    } else if (strcmp(status, "ERR") == 0) {
      strcpy(humidityStr, "ERR");
    } else {
      sprintf(humidityStr, "%d%%", humidity);
    }
    
    // 중앙 정렬
    int strWidth = u8g2.getStrWidth(humidityStr);
    int xPos = (128 - strWidth) / 2;
    u8g2.drawStr(xPos, 35, humidityStr);
    
    // 상태 표시
    u8g2.setFont(u8g2_font_ncenB08_tr);
    const char* statusStr = systemRunning ? "RUN" : "STOP";
    
    int statusWidth = u8g2.getStrWidth(statusStr);
    int statusXPos = (128 - statusWidth) / 2;
    u8g2.drawStr(statusXPos, 55, statusStr);
    
    // 우측 하단에 작동시간 표시
    u8g2.setFont(u8g2_font_ncenB08_tr);
    char timeStr[10];
    unsigned long currentRunTime = totalRunTime;
    if (systemRunning && timeInitialized) {
      currentRunTime += (millis() - systemStartTime);
    }
    int minutes = currentRunTime / 60000;
    int seconds = (currentRunTime % 60000) / 1000;
    sprintf(timeStr, "%02d:%02d", minutes, seconds);
    int timeWidth = u8g2.getStrWidth(timeStr);
    u8g2.drawStr(128 - timeWidth - 2, 62, timeStr);
    
    // 좌측 하단에 현재 시나리오 표시 (번호 제거)
    char scenarioLabel[10];
    sprintf(scenarioLabel, "%s", getScenarioDescription(currentScenario));
    u8g2.drawStr(2, 62, scenarioLabel);
    
  } while (u8g2.nextPage());
}

// 콤프레샤 보호 인터럽트 서비스 루틴 (ISR)
void compressorProtectionISR() {
  if (!systemRunning) return; // 시스템이 실행 중이 아니면 무시
  
  static unsigned long workStartTime = 0;
  static unsigned long restStartTime = 0;
  static bool isRestMode = false;
  
  // 보호 로직 리셋 (시나리오 변경 시)
  if (protectionReset) {
    workStartTime = 0;
    restStartTime = 0;
    isRestMode = false;
    protectionReset = false;
    Serial.println("=== PROTECTION LOGIC RESET ===");
  }
  
  // 항상 ON 모드인 경우 보호 로직 비활성화
  if (alwaysOnMode) {
    return;
  }
  
  unsigned long currentTime = millis();
  
  // 최대 세션 시간 체크 (50분)
  unsigned long sessionElapsed = currentTime - sessionStartTime;
  if (sessionElapsed >= MAX_SESSION_TIME) {
    Serial.println("=== MAX SESSION TIME REACHED (50min) ===");
    stopSystem();
    return;
  }
  
  if (!isRestMode) {
    // 동작 모드에서 WORK_TIME 경과 체크
    if (workStartTime == 0) {
      workStartTime = currentTime;
    }
    
    unsigned long workElapsed = currentTime - workStartTime;
    if (workElapsed >= WORK_TIME) {
      // 휴식 모드로 전환
      isRestMode = true;
      restStartTime = currentTime;
      workStartTime = 0; // 리셋
      
      Serial.println("=== REST MODE STARTED ===");
      
      // D3, D4번 핀만 HIGH로 (콤프레샤 휴식 - 릴레이 OFF)
      digitalWrite(PUMP_RELAY, HIGH);
      digitalWrite(VALVE_RELAY, HIGH);
    }
  } else {
    // 휴식 모드에서 REST_TIME 경과 체크
    unsigned long restElapsed = currentTime - restStartTime;
    if (restElapsed >= REST_TIME) {
      // 동작 모드로 복귀
      isRestMode = false;
      workStartTime = currentTime;
      restStartTime = 0; // 리셋
      
      Serial.println("=== WORK MODE RESUMED ===");
      
      // D3, D4번 핀을 LOW로 (콤프레샤 재시작 - 릴레이 ON)
      digitalWrite(PUMP_RELAY, LOW);
      digitalWrite(VALVE_RELAY, LOW);
    }
  }
}

// RS485를 통한 습도 요청 (개선 버전)
void requestHumidity() {
  // 기존 버퍼 클리어 (루틴 시작 시에만)
  while (rs485.available()) {
    rs485.read();
  }
  
  // 송신 모드로 전환
  digitalWrite(RS485_DE, HIGH);
  
  // "REQ\r\n" 전송 (CR+LF)
  const char* REQ = "REQ\r\n";
  rs485.write(REQ);
  Serial.println("TX: REQ");
  
  // 9600bps 기준: 4바이트 * 12 / 10 ≈ 4.8ms → 여유 포함 6ms
  delay(6);
  
  // 수신 모드로 전환
  digitalWrite(RS485_DE, LOW);
  delay(2);
  rs485.listen();  // 수신 활성화 명시
  
  // 응답 대기 (1000ms 타임아웃)
  const unsigned long timeoutAt = millis() + 1000;
  String response = "";
  response.reserve(32);  // 최대 32자로 예약
  
  while (millis() < timeoutAt) {
    while (rs485.available()) {
      char c = rs485.read();
      if (c == '\r' || c == '\n') {
        if (response.length() > 0) {
          goto PARSE;
        }
      } else if (isPrintable(c)) {
        if (response.length() < 32) {
          response += c;
        }
      }
    }
  }
  
  // 타임아웃
  Serial.println("Timeout");
  handleError();
  return;
  
PARSE:
  response.trim();
  Serial.print("RX: ");
  Serial.println(response);
  
  if (response == "ERR") {
    handleError();
  } else {
    // 숫자 추출 ("HUM:72"도 허용)
    int humidity = -1;
    int acc = 0;
    bool seen = false;
    
    for (uint8_t i = 0; i < response.length(); i++) {
      if (isdigit(response[i])) {
        acc = acc * 10 + (response[i] - '0');
        seen = true;
      } else if (seen) {
        humidity = acc;
        break;
      }
    }
    
    if (!seen) {
      humidity = acc;  // 마지막 숫자 처리
    }
    
    if (humidity >= 0 && humidity <= 100) {
      handleHumidity(humidity);
      consecutiveError = 0;  // 성공시 에러 카운터 리셋
    } else {
      Serial.print("Invalid: ");
      Serial.println(humidity);
      handleError();
    }
  }
}

// 습도값 처리 (30초 평균)
void handleHumidity(int humidity) {
  // 습도값 저장 (최근 30개)
  humidityValues[valueIndex] = humidity;
  valueIndex = (valueIndex + 1) % 30;
  
  // 30초 평균 계산
  int sum = 0;
  int validCount = 0;
  for (int i = 0; i < 30; i++) {
    if (humidityValues[i] > 0) {
      sum += humidityValues[i];
      validCount++;
    }
  }
  
  int average = (validCount > 0) ? sum / validCount : 0;
  
  // 시리얼 모니터에 습도 정보 출력
  Serial.print("H: ");
  Serial.print(humidity);
  Serial.print("% 30s Avg: ");
  Serial.println(average);
  
  // OLED에 30초 평균 습도값 표시
  displayHumidity(average, "OK");
  
  // 설정 습도 도달 확인 (설정값 이상이면 바로 정지) - 실행 상태에서만
  if (systemRunning && average >= targetHumidity) {
    Serial.print("Target humidity ");
    Serial.print(targetHumidity);
    Serial.println("% reached - Auto stop");
    
    // 도달 알림 부저 (실행 상태에서만)
    for (int i = 0; i < 3; i++) {
      digitalWrite(BUZZER, HIGH);
      delay(300);
      digitalWrite(BUZZER, LOW);
      delay(300);
    }
    
    stopSystem();
  }
  
  consecutiveError = 0;  // 오류 카운터 리셋
}

// 오류 처리
void handleError() {
  consecutiveError++;
  Serial.print("Err: ");
  Serial.println(consecutiveError);
  
  // OLED에 오류 표시
  displayHumidity(0, "ERR");
  
  // 실행 상태에서만 오류 부저와 자동 정지
  if (systemRunning && consecutiveError >= 3) {
    Serial.println("3 errors - stop");
    
    // 오류 부저 패턴 (실행 상태에서만)
    for (int i = 0; i < 3; i++) {
      digitalWrite(BUZZER, HIGH);
      delay(500);
      digitalWrite(BUZZER, LOW);
      delay(500);
    }
    
    stopSystem();
  }
}