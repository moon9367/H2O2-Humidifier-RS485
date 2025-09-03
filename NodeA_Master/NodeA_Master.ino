#include <SoftwareSerial.h>
#include <Wire.h>
#include <U8g2lib.h>

// 핀 정의
#define PUMP_RELAY 3
#define VALVE_RELAY 4
#define BUZZER 5
#define START_BTN 6
#define STOP_BTN 7
#define HUMIDITY_SET_BTN 12
#define RS485_DE 2
#define RS485_RX 9
#define RS485_TX 10

// RS485 통신
SoftwareSerial rs485(RS485_RX, RS485_TX);

// OLED (SH1106, Page Buffer 모드)
U8G2_SH1106_128X64_NONAME_1_HW_I2C u8g2(U8G2_R0);

// 변수 (메모리 최적화)
bool systemRunning = false;
int humidityValues[15] = {0};  // 30개에서 15개로 줄임
int valueIndex = 0;
int consecutiveError = 0;
unsigned long lastRequest = 0;
const unsigned long REQUEST_INTERVAL = 1000;

// 설정 습도 관련 변수
int targetHumidity = 95;
unsigned long lastHumidityBtnPress = 0;

// 작동시간 관련 변수
unsigned long systemStartTime = 0;
unsigned long totalRunTime = 0;
bool timeInitialized = false;

// 콤프레샤 보호 관련 변수
unsigned long sessionStartTime = 0;
const unsigned long MAX_SESSION_TIME = 50UL * 60UL * 1000UL;

// 시나리오 모드 관련 변수
bool scenarioMode = false;
const int MAX_SCENARIOS = 5;
int scrollOffset = 0;
const int DISPLAY_SCENARIOS = 4;

// 콤프레샤 보호 관련 변수 (동적 설정용)
unsigned long WORK_TIME = 300UL * 1000UL;  // 5분 = 300초
unsigned long REST_TIME = 60UL * 1000UL;   // 1분 = 60초
bool alwaysOnMode = false;
bool protectionReset = false;
int currentScenario = 0;

void setup() {
  Serial.begin(9600);
  
  // 핀 모드 설정
  pinMode(PUMP_RELAY, OUTPUT);
  pinMode(VALVE_RELAY, OUTPUT);
  pinMode(BUZZER, OUTPUT);
  pinMode(START_BTN, INPUT_PULLUP);
  pinMode(STOP_BTN, INPUT_PULLUP);
  pinMode(HUMIDITY_SET_BTN, INPUT_PULLUP);
  pinMode(RS485_DE, OUTPUT);
  
  // 릴레이 초기 상태 (OFF)
  digitalWrite(PUMP_RELAY, HIGH);
  digitalWrite(VALVE_RELAY, HIGH);
  
  // RS485 수신 모드로 초기화
  digitalWrite(RS485_DE, LOW);
  
  // I2C 및 OLED 초기화
  Wire.begin();
  u8g2.begin();
  
  // OLED 초기 메시지
  displayHumidity(0, "INIT");
  
  // RS485 통신 시작
  rs485.begin(9600);
  
  Serial.println("System Ready");
  Serial.println("Press START");
}

void loop() {
  // 시리얼 입력 확인 (부저 테스트용)
  if (Serial.available()) {
    char input = Serial.read();
    if (input == 'b' || input == 'B') {
      testBuzzer();
    }
  }
  
  // 시나리오 모드 진입 감지
  checkScenarioModeEntry();
  
  // 시나리오 모드에서의 버튼 처리
  if (scenarioMode) {
    handleScenarioMode();
  } else {
    // 일반 모드에서의 버튼 처리
    static unsigned long lastButtonPress = 0;
    if (millis() - lastButtonPress > 300) {
      if (digitalRead(START_BTN) == LOW && !systemRunning) {
        startSystem();
        lastButtonPress = millis();
      }
      
      if (digitalRead(STOP_BTN) == LOW && systemRunning) {
        stopSystem();
        lastButtonPress = millis();
      }
    }
    
    // 설정 습도 버튼 처리
    if (millis() - lastHumidityBtnPress > 300) {
      if (digitalRead(HUMIDITY_SET_BTN) == LOW) {
        adjustTargetHumidity();
        lastHumidityBtnPress = millis();
      }
    }
    
    // 콤프레샤 보호 기능
    if (systemRunning) {
      compressorProtectionISR();
    }
    
    // 일반 모드에서 OLED 표시
    if (millis() - lastRequest >= REQUEST_INTERVAL) {
      requestHumidity();
      lastRequest = millis();
    }
  }
}

// 시나리오 모드 진입 감지
void checkScenarioModeEntry() {
  static unsigned long stopBtnPressStart = 0;
  static bool stopBtnPressed = false;
  static bool scenarioEntered = false;
  
  bool stopBtn = (digitalRead(STOP_BTN) == LOW);
  
  if (stopBtn && !stopBtnPressed && !scenarioEntered) {
    stopBtnPressStart = millis();
    stopBtnPressed = true;
  }
  
  if (!stopBtn && stopBtnPressed) {
    stopBtnPressed = false;
    scenarioEntered = false;
  }
  
  if (stopBtn && stopBtnPressed && !scenarioMode && !scenarioEntered && (millis() - stopBtnPressStart >= 3000)) {
    scenarioMode = true;
    currentScenario = 0;
    scrollOffset = 0;
    scenarioEntered = true;
    Serial.println("SCENARIO MODE");
    
    // 시나리오 모드 진입 알림음
    for (int i = 0; i < 2; i++) {
      digitalWrite(BUZZER, HIGH);
      delay(200);
      digitalWrite(BUZZER, LOW);
      delay(200);
    }
    
    delay(2000);
  }
}

// 시나리오 모드 처리
void handleScenarioMode() {
  static unsigned long lastStartBtnPress = 0;
  static unsigned long lastStopBtnPress = 0;
  static bool lastStartBtn = false;
  static bool lastStopBtn = false;
  
  // 시작 버튼 처리
  if (millis() - lastStartBtnPress > 500) {
    bool startBtn = (digitalRead(START_BTN) == LOW);
    
    if (startBtn && !lastStartBtn) {
      currentScenario = (currentScenario + 1) % MAX_SCENARIOS;
      
      if (currentScenario >= scrollOffset + DISPLAY_SCENARIOS) {
        scrollOffset = currentScenario - DISPLAY_SCENARIOS + 1;
      } else if (currentScenario < scrollOffset) {
        scrollOffset = currentScenario;
      }
      
      Serial.print("Scenario: ");
      Serial.println(currentScenario + 1);
      
      digitalWrite(BUZZER, HIGH);
      delay(100);
      digitalWrite(BUZZER, LOW);
      
      lastStartBtnPress = millis();
    }
    lastStartBtn = startBtn;
  }
  
  // 스탑 버튼 처리
  if (millis() - lastStopBtnPress > 1000) {
    bool stopBtn = (digitalRead(STOP_BTN) == LOW);
    
    if (stopBtn && !lastStopBtn) {
      executeScenario(currentScenario);
      scenarioMode = false;
      Serial.println("SCENARIO EXIT");
      lastStopBtnPress = millis();
    }
    lastStopBtn = stopBtn;
  }
  
  // 시나리오 모드에서 OLED 표시
  displayScenarioMode();
}

// 시나리오 실행
void executeScenario(int scenario) {
  Serial.print("Scenario ");
  Serial.println(scenario + 1);
  
     switch (scenario) {
     case 0:
       WORK_TIME = 300UL * 1000UL;  // 5분 = 300초
       REST_TIME = 60UL * 1000UL;   // 1분 = 60초
       alwaysOnMode = false;
       Serial.println("5m/1m");
       break;
     case 1:
       WORK_TIME = 600UL * 1000UL;  // 10분 = 600초
       REST_TIME = 300UL * 1000UL;  // 5분 = 300초
       alwaysOnMode = false;
       Serial.println("10m/5m");
       break;
     case 2:
       WORK_TIME = 60UL * 1000UL;   // 1분 = 60초
       REST_TIME = 30UL * 1000UL;   // 30초
       alwaysOnMode = false;
       Serial.println("1m/30s");
       break;
     case 3:
       WORK_TIME = 30UL * 1000UL;   // 30초
       REST_TIME = 30UL * 1000UL;   // 30초
       alwaysOnMode = false;
       Serial.println("30s/30s");
       break;
     case 4:
       alwaysOnMode = true;
       Serial.println("Always ON");
       break;
   }
  
  // 시나리오 설정 확인 출력
  Serial.println("=== SCENARIO SETTINGS ===");
  Serial.print("Scenario: "); Serial.println(scenario + 1);
  if (!alwaysOnMode) {
    Serial.print("WORK_TIME: "); Serial.print(WORK_TIME); Serial.print("ms ("); Serial.print(WORK_TIME/1000); Serial.println("s)");
    Serial.print("REST_TIME: "); Serial.print(REST_TIME); Serial.print("ms ("); Serial.print(REST_TIME/1000); Serial.println("s)");
  } else {
    Serial.println("Always ON mode - no timer");
  }
  
  if (!alwaysOnMode) {
    Serial.print("Work: "); Serial.print(WORK_TIME/1000); Serial.println("s");
    Serial.print("Rest: "); Serial.print(REST_TIME/1000); Serial.println("s");
  }
  
  totalRunTime = 0;
  timeInitialized = false;
  protectionReset = true;
  
  // 시나리오 실행 알림음
  for (int i = 0; i < 3; i++) {
    digitalWrite(BUZZER, HIGH);
    delay(150);
    digitalWrite(BUZZER, LOW);
    delay(150);
  }
  
  Serial.println("Press START");
}

// 시나리오 모드 표시
void displayScenarioMode() {
  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(5, 12, "TIMER SCENARIO");
    
    for (int i = 0; i < DISPLAY_SCENARIOS; i++) {
      int scenarioIndex = scrollOffset + i;
      if (scenarioIndex < MAX_SCENARIOS) {
        int yPos = 25 + (i * 10);
        
        if (scenarioIndex == currentScenario) {
          u8g2.drawStr(5, yPos, ">");
        }
        
        char scenarioStr[20];
        sprintf(scenarioStr, "%d. %s", scenarioIndex + 1, getScenarioDescription(scenarioIndex));
        u8g2.drawStr(15, yPos, scenarioStr);
      }
    }
    
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
  
  totalRunTime = 0;
  timeInitialized = true;
  systemStartTime = millis();
  sessionStartTime = millis();
  protectionReset = true;
  
  // 경고음
  for (int i = 0; i < 3; i++) {
    digitalWrite(BUZZER, HIGH);
    delay(200);
    digitalWrite(BUZZER, LOW);
    delay(200);
  }
  
  digitalWrite(PUMP_RELAY, LOW);
  digitalWrite(VALVE_RELAY, LOW);
  
  Serial.println("RUNNING");
}

void stopSystem() {
  systemRunning = false;
  Serial.println("STOP");
  
  if (timeInitialized) {
    totalRunTime += (millis() - systemStartTime);
  }
  
  digitalWrite(PUMP_RELAY, HIGH);
  digitalWrite(VALVE_RELAY, HIGH);
  
  // 완료 신호음
  for (int i = 0; i < 5; i++) {
    digitalWrite(BUZZER, HIGH);
    delay(100);
    digitalWrite(BUZZER, LOW);
    delay(100);
  }
  
  Serial.println("STOPPED");
}

// 설정 습도 조절 함수
void adjustTargetHumidity() {
  targetHumidity += 5;
  if (targetHumidity > 100) {
    targetHumidity = 50;
  }
  
  Serial.print("Target: ");
  Serial.print(targetHumidity);
  Serial.println("%");
  
  digitalWrite(BUZZER, HIGH);
  delay(50);
  digitalWrite(BUZZER, LOW);
}

// 부저 테스트 함수
void testBuzzer() {
  Serial.println("Buzzer test");
  
  digitalWrite(BUZZER, HIGH);
  delay(100);
  digitalWrite(BUZZER, LOW);
  delay(100);
  
  for (int i = 0; i < 3; i++) {
    digitalWrite(BUZZER, HIGH);
    delay(200);
    digitalWrite(BUZZER, LOW);
    delay(200);
  }
  
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
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(5, 12, "inVIRUStech");
    
    char targetStr[8];
    sprintf(targetStr, "%d%%", targetHumidity);
    int targetWidth = u8g2.getStrWidth(targetStr);
    u8g2.drawStr(128 - targetWidth - 2, 10, targetStr);
    
    u8g2.setFont(u8g2_font_ncenB14_tr);
    char humidityStr[10];
    if (strcmp(status, "INIT") == 0) {
      strcpy(humidityStr, "INIT");
    } else if (strcmp(status, "ERR") == 0) {
      strcpy(humidityStr, "ERR");
    } else {
      sprintf(humidityStr, "%d%%", humidity);
    }
    
    int strWidth = u8g2.getStrWidth(humidityStr);
    int xPos = (128 - strWidth) / 2;
    u8g2.drawStr(xPos, 35, humidityStr);
    
    u8g2.setFont(u8g2_font_ncenB08_tr);
    const char* statusStr = systemRunning ? "RUN" : "STOP";
    
    int statusWidth = u8g2.getStrWidth(statusStr);
    int statusXPos = (128 - statusWidth) / 2;
    u8g2.drawStr(statusXPos, 55, statusStr);
    
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
    
    char scenarioLabel[10];
    sprintf(scenarioLabel, "%s", getScenarioDescription(currentScenario));
    u8g2.drawStr(2, 62, scenarioLabel);
    
  } while (u8g2.nextPage());
}

// 콤프레샤 보호 인터럽트 서비스 루틴 (ISR)
void compressorProtectionISR() {
  if (!systemRunning) return;
  
  static unsigned long workStartTime = 0;
  static unsigned long restStartTime = 0;
  static bool isRestMode = false;
  static unsigned long lastDebugTime = 0;
  
  if (protectionReset) {
    workStartTime = 0;
    restStartTime = 0;
    isRestMode = false;
    lastDebugTime = 0;
    protectionReset = false;
    Serial.println("PROTECTION RESET");
    Serial.print("Work: "); Serial.print(WORK_TIME/1000); Serial.println("s");
    Serial.print("Rest: "); Serial.print(REST_TIME/1000); Serial.println("s");
  }
  
  if (alwaysOnMode) {
    return;
  }
  
  unsigned long currentTime = millis();
  
  unsigned long sessionElapsed = currentTime - sessionStartTime;
  if (sessionElapsed >= MAX_SESSION_TIME) {
    Serial.println("MAX SESSION TIME");
    stopSystem();
    return;
  }
  
  if (currentTime - lastDebugTime >= 10000) {
    Serial.print("Mode: "); Serial.print(isRestMode ? "REST" : "WORK");
    Serial.print(" W:"); Serial.print(WORK_TIME/1000);
    Serial.print(" R:"); Serial.println(REST_TIME/1000);
    lastDebugTime = currentTime;
  }
  
  if (!isRestMode) {
    if (workStartTime == 0) {
      workStartTime = currentTime;
      Serial.println("WORK START");
    }
    
    // 간단한 시간 계산
    unsigned long workElapsed = currentTime - workStartTime;
    
    // 매 초마다 디버깅 출력
    static unsigned long lastWorkDebug = 0;
    if (currentTime - lastWorkDebug >= 1000) {
      Serial.print("Work: "); Serial.print(workElapsed/1000); Serial.print("/"); Serial.print(WORK_TIME/1000);
      Serial.print(" ("); Serial.print(workElapsed); Serial.print("ms/"); Serial.print(WORK_TIME); Serial.println("ms)");
      lastWorkDebug = currentTime;
    }
    
    // 간단한 시간 비교
    if (workElapsed >= WORK_TIME) {
      Serial.println("=== WORK TIME REACHED ===");
      Serial.print("Work elapsed: "); Serial.print(workElapsed/1000); Serial.print("s ("); Serial.print(workElapsed); Serial.println("ms)");
      Serial.print("Target time: "); Serial.print(WORK_TIME/1000); Serial.print("s ("); Serial.print(WORK_TIME); Serial.println("ms)");
      
      isRestMode = true;
      restStartTime = currentTime;
      workStartTime = 0;
      
      Serial.println("REST START");
      Serial.print("Work done: "); Serial.print(workElapsed/1000); Serial.println("s");
      
      digitalWrite(PUMP_RELAY, HIGH);
      digitalWrite(VALVE_RELAY, HIGH);
    }
  } else {
    // 간단한 시간 계산
    unsigned long restElapsed = currentTime - restStartTime;
    
    // 매 초마다 디버깅 출력
    static unsigned long lastRestDebug = 0;
    if (currentTime - lastRestDebug >= 1000) {
      Serial.print("Rest: "); Serial.print(restElapsed/1000); Serial.print("/"); Serial.print(REST_TIME/1000);
      Serial.print(" ("); Serial.print(restElapsed); Serial.print("ms/"); Serial.print(REST_TIME); Serial.println("ms)");
      lastRestDebug = currentTime;
    }
    
    // 간단한 시간 비교
    if (restElapsed >= REST_TIME) {
      Serial.println("=== REST TIME REACHED ===");
      Serial.print("Rest elapsed: "); Serial.print(restElapsed/1000); Serial.print("s ("); Serial.print(restElapsed); Serial.println("ms)");
      Serial.print("Target time: "); Serial.print(REST_TIME/1000); Serial.print("s ("); Serial.print(REST_TIME); Serial.println("ms)");
      
      isRestMode = false;
      workStartTime = currentTime;
      restStartTime = 0;
      
      Serial.println("WORK RESUME");
      Serial.print("Rest done: "); Serial.print(restElapsed/1000); Serial.println("s");
      
      digitalWrite(PUMP_RELAY, LOW);
      digitalWrite(VALVE_RELAY, LOW);
    }
  }
}

// RS485를 통한 습도 요청
void requestHumidity() {
  while (rs485.available()) {
    rs485.read();
  }
  
  digitalWrite(RS485_DE, HIGH);
  
  const char* REQ = "REQ\r\n";
  rs485.write(REQ);
  Serial.println("TX: REQ");
  
  delay(6);
  
  digitalWrite(RS485_DE, LOW);
  delay(2);
  rs485.listen();
  
  const unsigned long timeoutAt = millis() + 1000;
  String response = "";
  response.reserve(32);
  
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
      humidity = acc;
    }
    
    if (humidity >= 0 && humidity <= 100) {
      handleHumidity(humidity);
      consecutiveError = 0;
    } else {
      Serial.print("Invalid: ");
      Serial.println(humidity);
      handleError();
    }
  }
}

// 습도값 처리
void handleHumidity(int humidity) {
  humidityValues[valueIndex] = humidity;
  valueIndex = (valueIndex + 1) % 15;
  
  int sum = 0;
  int validCount = 0;
  for (int i = 0; i < 15; i++) {
    if (humidityValues[i] > 0) {
      sum += humidityValues[i];
      validCount++;
    }
  }
  
  int average = (validCount > 0) ? sum / validCount : 0;
  
  Serial.print("H: ");
  Serial.print(humidity);
  Serial.print("% Avg: ");
  Serial.println(average);
  
  displayHumidity(average, "OK");
  
  if (systemRunning && average >= targetHumidity) {
    Serial.print("Target ");
    Serial.print(targetHumidity);
    Serial.println("% reached");
    
    for (int i = 0; i < 3; i++) {
      digitalWrite(BUZZER, HIGH);
      delay(300);
      digitalWrite(BUZZER, LOW);
      delay(300);
    }
    
    stopSystem();
  }
  
  consecutiveError = 0;
}

// 오류 처리
void handleError() {
  consecutiveError++;
  Serial.print("Err: ");
  Serial.println(consecutiveError);
  
  displayHumidity(0, "ERR");
  
  if (systemRunning && consecutiveError >= 3) {
    Serial.println("3 errors - stop");
    
    for (int i = 0; i < 3; i++) {
      digitalWrite(BUZZER, HIGH);
      delay(500);
      digitalWrite(BUZZER, LOW);
      delay(500);
    }
    
    stopSystem();
  }
}