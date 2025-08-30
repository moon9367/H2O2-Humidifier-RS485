#include <SoftwareSerial.h>
#include <Wire.h>
#include <U8g2lib.h>

// 핀 정의
#define PUMP_RELAY 3
#define VALVE_RELAY 4
#define BUZZER 5
#define START_BTN 6
#define STOP_BTN 7
#define RS485_DE 2
#define RS485_RX 9
#define RS485_TX 10

// RS485 통신
SoftwareSerial rs485(RS485_RX, RS485_TX);

// OLED (SH1106, Page Buffer 모드)
U8G2_SH1106_128X64_NONAME_1_HW_I2C u8g2(U8G2_R0);

// 변수
bool systemRunning = false;
int humidityValues[3] = {0};  // 최근 3개 습도값
int valueIndex = 0;
int consecutiveHigh = 0;      // 연속 고습도 횟수
int consecutiveError = 0;     // 연속 오류 횟수
unsigned long lastRequest = 0;
const unsigned long REQUEST_INTERVAL = 1000;  // 1초

void setup() {
  Serial.begin(9600);
  
  // 핀 모드 설정
  pinMode(PUMP_RELAY, OUTPUT);
  pinMode(VALVE_RELAY, OUTPUT);
  pinMode(BUZZER, OUTPUT);
  pinMode(START_BTN, INPUT_PULLUP);
  pinMode(STOP_BTN, INPUT_PULLUP);
  pinMode(RS485_DE, OUTPUT);
  
  // 릴레이 초기 상태 (OFF)
  digitalWrite(PUMP_RELAY, HIGH);
  digitalWrite(VALVE_RELAY, HIGH);
  
  // RS485 수신 모드로 초기화
  digitalWrite(RS485_DE, LOW);
  
  // I2C 및 OLED 초기화
  Wire.begin();
  Wire.setClock(400000); // I2C 속도 400kHz
  u8g2.begin();
  
  // OLED 초기 메시지
  displayHumidity(0, "INIT");
  
  // RS485 통신 시작
  rs485.begin(9600);
  
  Serial.println("inVIRUSrech System Ready");
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
  
  // RS485 통신 (1초마다 항상)
  if (millis() - lastRequest >= REQUEST_INTERVAL) {
    requestHumidity();
    lastRequest = millis();
  }
  
  // 버튼 상태 (5초마다)
  static unsigned long lastDebug = 0;
  if (millis() - lastDebug >= 5000) {
    Serial.print("Sys: ");
    Serial.println(systemRunning ? "RUN" : "STOP");
    lastDebug = millis();
  }
}

void startSystem() {
  systemRunning = true;
  Serial.println("START");
  
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
  
  // 릴레이 정지
  digitalWrite(PUMP_RELAY, HIGH);
  digitalWrite(VALVE_RELAY, HIGH);
  
  // 완료 신호음 (5번 빠르게)
  for (int i = 0; i < 5; i++) {
    digitalWrite(BUZZER, HIGH);
    delay(100);
    digitalWrite(BUZZER, LOW);
    delay(100);
  }
  
  Serial.println("STOPPED");
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
    // 제목 (inVIRUSrech로 변경)
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(5, 12, "inVIRUSrech");
    
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
    
  } while (u8g2.nextPage());
}

// RS485를 통한 습도 요청 (개선 버전)
void requestHumidity() {
  // 기존 버퍼 클리어
  while (rs485.available()) {
    rs485.read();
  }
  
  // 송신 모드로 전환
  digitalWrite(RS485_DE, HIGH);
  delay(2);  // 2ms로 증가
  
  // "REQ" 전송
  rs485.print("REQ\n");  // println 대신 print + \n 사용
  rs485.flush();         // 전송 완료 대기
  Serial.println("TX: REQ");
  
  // 수신 모드로 전환
  delay(5);              // 5ms로 증가
  digitalWrite(RS485_DE, LOW);
  delay(10);             // 수신 모드 안정화
  
  // 응답 대기 (1000ms 타임아웃으로 증가)
  unsigned long timeout = millis() + 1000;
  String response = "";
  bool dataComplete = false;
  
  while (millis() < timeout && !dataComplete) {
    if (rs485.available()) {
      char c = rs485.read();
      
      if (c == '\n' || c == '\r') {
        if (response.length() > 0) {
          dataComplete = true;
        }
      } else if (c >= 32 && c <= 126) {  // 출력 가능한 ASCII 문자만
        response += c;
        if (response.length() > 10) {     // 최대 길이 제한
          break;
        }
      }
    }
    delay(1);  // CPU 부하 감소
  }
  
  response.trim();
  
  if (response.length() > 0) {
    Serial.print("RX: ");
    Serial.println(response);
    
    if (response == "ERR") {
      handleError();
    } else {
      int humidity = response.toInt();
      if (humidity >= 0 && humidity <= 100) {
        handleHumidity(humidity);
        consecutiveError = 0;  // 성공시 에러 카운터 리셋
      } else {
        Serial.print("Invalid: ");
        Serial.println(humidity);
        handleError();
      }
    }
  } else {
    Serial.println("Timeout");
    handleError();
  }
}

// 습도값 처리
void handleHumidity(int humidity) {
  // 습도값 저장 (최근 3개)
  humidityValues[valueIndex] = humidity;
  valueIndex = (valueIndex + 1) % 3;
  
  // 평균 계산
  int sum = 0;
  int validCount = 0;
  for (int i = 0; i < 3; i++) {
    if (humidityValues[i] > 0) {
      sum += humidityValues[i];
      validCount++;
    }
  }
  
  int average = (validCount > 0) ? sum / validCount : 0;
  
  // 시리얼 모니터에 습도 정보 출력
  Serial.print("H: ");
  Serial.print(humidity);
  Serial.print("% Avg: ");
  Serial.println(average);
  
  // OLED에 습도값 표시
  displayHumidity(humidity, "OK");
  
  // 연속 고습도 확인 (60% 이상 연속 3회) - 실행 상태에서만
  if (systemRunning && average >= 60) {
    consecutiveHigh++;
    Serial.print("High humidity! Count: ");
    Serial.println(consecutiveHigh);
    
    if (consecutiveHigh >= 3) {
      Serial.println("Target reached - Auto stop");
      
      // 도달 알림 부저 (실행 상태에서만)
      for (int i = 0; i < 3; i++) {
        digitalWrite(BUZZER, HIGH);
        delay(300);
        digitalWrite(BUZZER, LOW);
        delay(300);
      }
      
      stopSystem();
    }
  } else {
    consecutiveHigh = 0;
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