#include <SoftwareSerial.h>

// 핀 정의
#define HUMIDITY_SENSOR A0    // 습도 센서
#define RS485_DE 2           // RS485 DE/RE
#define RS485_RX 9           // RS485 RX
#define RS485_TX 10          // RS485 TX

// RS485 통신
SoftwareSerial rs485(RS485_RX, RS485_TX);

// 변수
String receivedData = "";
bool dataReceived = false;

void setup() {
  // 시리얼 통신 시작
  Serial.begin(9600);
  delay(1000);  // 시리얼 안정화 대기
  
  Serial.println("=== 슬레이브 노드 시작 ===");
  Serial.println("시리얼 통신 테스트...");
  
  // 핀 모드 설정
  pinMode(RS485_DE, OUTPUT);
  
  // 수신 모드로 시작
  digitalWrite(RS485_DE, LOW);
  Serial.println("RS485 수신 모드로 시작");
  
  // RS485 통신 시작
  rs485.begin(9600);
  Serial.println("RS485 통신 시작 완료");
  
  Serial.println("=== 설정 완료 ===");
  Serial.println("마스터로부터 요청 대기 중...");
}

void loop() {
  // RS485 데이터 수신 확인
  if (rs485.available()) {
    char c = rs485.read();
    
    if (c == '\n') {
      // 줄바꿈 문자 수신 시 데이터 처리
      dataReceived = true;
      receivedData.trim();
    } else {
      // 문자 누적
      receivedData += c;
    }
  }
  
  // 데이터 처리
  if (dataReceived) {
    processRequest(receivedData);
    receivedData = "";
    dataReceived = false;
  }
}

void processRequest(String request) {
  Serial.print("수신: ");
  Serial.println(request);
  
  if (request == "REQ") {
    // 습도 요청에 대한 응답
    int humidity = readHumidity();
    
    if (humidity == -1) {
      // 센서 오류
      sendResponse("ERR");
      Serial.println("센서 오류 - ERR 전송");
    } else {
      // 정상 습도값 전송
      sendResponse(String(humidity));
      Serial.print("습도 전송: ");
      Serial.println(humidity);
    }
  }
}

int readHumidity() {
  // 아날로그 값 읽기 (0~1023)
  int sensorValue = analogRead(HUMIDITY_SENSOR);
  
  // 센서 값 검증 (0 또는 1023 고정값은 오류)
  if (sensorValue == 0 || sensorValue == 1023) {
    return -1;  // 오류
  }
  
  // 0~1023을 0~100%로 변환
  int humidity = map(sensorValue, 0, 1023, 0, 100);
  
  // 범위 제한
  if (humidity < 0) humidity = 0;
  if (humidity > 100) humidity = 100;
  
  return humidity;
}

void sendResponse(String response) {
  // 송신 모드로 전환
  digitalWrite(RS485_DE, HIGH);
  delay(1);
  
  // 응답 전송
  rs485.println(response);
  Serial.print("전송: ");
  Serial.println(response);
  
  // 수신 모드로 전환
  delay(10);
  digitalWrite(RS485_DE, LOW);
}
