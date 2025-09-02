# H2O2 Humidifier RS485 Control System

## 프로젝트 개요

이 프로젝트는 H2O2 가습기를 위한 RS485 통신 기반 마스터-슬레이브 제어 시스템입니다. Arduino Nano를 사용하여 습도 센서 데이터를 수집하고, 다양한 타이머 시나리오를 통해 콤프레샤를 보호하면서 가습기를 제어합니다.

## 주요 기능

### 1. 습도 모니터링 및 제어
- **30초 평균 습도 계산**: 최근 30개 습도값의 평균을 사용하여 안정적인 측정
- **자동 정지**: 설정 습도 도달 시 자동으로 시스템 정지
- **오류 처리**: 연속 3회 오류 시 자동 정지 및 알림

### 2. 타이머 시나리오 시스템
5가지 사전 정의된 타이머 시나리오를 제공하여 콤프레샤 보호:

| 시나리오 | 동작 시간 | 휴식 시간 | 설명 |
|---------|----------|-----------|------|
| 1번 | 5분 | 1분 | 기본 설정 (권장) |
| 2번 | 10분 | 5분 | 장시간 동작 |
| 3번 | 1분 | 30초 | 단시간 동작 |
| 4번 | 30초 | 30초 | 테스트용 |
| 5번 | 항상 ON | - | 과부하 위험 (주의) |

### 3. 콤프레샤 보호 시스템
- **동작/휴식 사이클**: 설정된 시나리오에 따라 자동 ON/OFF
- **최대 세션 제한**: 50분 연속 동작 후 자동 정지
- **보호 로직 리셋**: 시나리오 변경 시 자동 초기화

### 4. 사용자 인터페이스
- **OLED 디스플레이**: SH1106 128x64 화면
- **3개 버튼**: START, STOP, SET (습도 조절)
- **부저 알림**: 다양한 상태별 알림음
- **실시간 타이머**: MM:SS 형식의 작동시간 표시

## 하드웨어 구성

### NodeA (Master) - 제어부
- **MCU**: Arduino Nano
- **디스플레이**: SH1106 OLED (I2C)
- **통신**: RS485 (SoftwareSerial)
- **제어**: 릴레이 2개 (펌프, 밸브)
- **입력**: 버튼 3개
- **출력**: 부저

### NodeB (Slave) - 센서부
- **MCU**: Arduino Nano
- **센서**: 습도 센서
- **통신**: RS485 (SoftwareSerial)

## 핀 배치

### NodeA (Master)
```
D2  - RS485_DE (송수신 제어)
D3  - PUMP_RELAY (펌프 릴레이)
D4  - VALVE_RELAY (밸브 릴레이)
D5  - BUZZER (부저)
D6  - START_BTN (시작 버튼)
D7  - STOP_BTN (정지 버튼)
D9  - RS485_RX (RS485 수신)
D10 - RS485_TX (RS485 송신)
D12 - HUMIDITY_SET_BTN (습도 설정 버튼)
A4  - I2C_SDA (OLED)
A5  - I2C_SCL (OLED)
```

### NodeB (Slave)
```
D2  - RS485_DE (송수신 제어)
D9  - RS485_RX (RS485 수신)
D10 - RS485_TX (RS485 송신)
A0  - 습도 센서 (아날로그 입력)
```

## 사용법

### 기본 동작
1. **시스템 시작**: START 버튼으로 가습기 동작 시작
2. **시스템 정지**: STOP 버튼으로 가습기 동작 정지
3. **습도 설정**: SET 버튼으로 목표 습도 조절 (5%씩 증가, 50%→95%→50% 순환)

### 시나리오 모드 진입
- **진입 방법**: STOP 버튼을 5초 이상 길게 누르기
- **시나리오 선택**: START 버튼으로 시나리오 변경
- **시나리오 실행**: STOP 버튼으로 선택된 시나리오 적용

### OLED 화면 정보
```
┌─────────────────────────┐
│ inVIRUSrech       95%   │  ← 제목 및 목표 습도
│                         │
│         72%             │  ← 현재 습도 (30초 평균)
│                         │
│         RUN             │  ← 시스템 상태
│                         │
│ 5m/1m           12:34   │  ← 시나리오 및 작동시간
└─────────────────────────┘
```

## 기술적 세부사항

### RS485 통신 프로토콜
- **통신 속도**: 9600bps
- **요청 형식**: "REQ\r\n" (CR+LF)
- **응답 형식**: 습도값 (예: "72" 또는 "HUM:72")
- **타임아웃**: 1000ms
- **버퍼 크기**: 32바이트

### 습도 처리 알고리즘
```cpp
// 30초 평균 계산
int humidityValues[30] = {0};  // 원형 버퍼
int valueIndex = 0;            // 현재 인덱스

// 새로운 습도값 저장
humidityValues[valueIndex] = humidity;
valueIndex = (valueIndex + 1) % 30;

// 평균 계산
int sum = 0, validCount = 0;
for (int i = 0; i < 30; i++) {
    if (humidityValues[i] > 0) {
        sum += humidityValues[i];
        validCount++;
    }
}
int average = (validCount > 0) ? sum / validCount : 0;
```

### 콤프레샤 보호 로직
```cpp
// 동작 모드에서 WORK_TIME 경과 시 휴식 모드로 전환
if (workElapsed >= WORK_TIME) {
    isRestMode = true;
    digitalWrite(PUMP_RELAY, HIGH);  // 릴레이 OFF
    digitalWrite(VALVE_RELAY, HIGH);
}

// 휴식 모드에서 REST_TIME 경과 시 동작 모드로 복귀
if (restElapsed >= REST_TIME) {
    isRestMode = false;
    digitalWrite(PUMP_RELAY, LOW);   // 릴레이 ON
    digitalWrite(VALVE_RELAY, LOW);
}
```

## 라이브러리 의존성

### NodeA (Master)
```cpp
#include <SoftwareSerial.h>  // RS485 통신
#include <Wire.h>            // I2C 통신
#include <U8g2lib.h>         // OLED 디스플레이
```

### NodeB (Slave)
```cpp
#include <SoftwareSerial.h>  // RS485 통신
```

## 설치 및 설정

### 1. 하드웨어 연결
- NodeA와 NodeB를 RS485 통신선으로 연결
- 각 노드의 전원 및 센서 연결
- 릴레이 및 부저 연결

### 2. 소프트웨어 업로드
1. Arduino IDE에서 NodeA_Master.ino 업로드
2. Arduino IDE에서 NodeB_Slave.ino 업로드
3. 각각의 시리얼 모니터로 동작 확인

### 3. 초기 설정
- 시스템 시작 시 기본값: 1번 시나리오 (5분 ON / 1분 OFF)
- 기본 목표 습도: 95%
- 콤프레샤 보호: 활성화

## 문제 해결

### 일반적인 문제들

1. **RS485 통신 오류**
   - 연결선 확인
   - 통신 속도 확인 (9600bps)
   - DE 핀 연결 확인

2. **OLED 표시 문제**
   - I2C 연결 확인
   - 전원 공급 확인
   - 라이브러리 설치 확인

3. **콤프레샤 보호 미동작**
   - 시나리오 설정 확인
   - 릴레이 연결 확인
   - 보호 로직 리셋 확인

### 디버깅 방법
- 시리얼 모니터로 상태 확인
- 부저 테스트: 시리얼에서 'b' 입력
- 5초마다 시스템 상태 출력

## 주의사항

1. **콤프레샤 보호**: 항상 ON 모드(5번 시나리오)는 과부하 위험이 있음
2. **습도 센서**: 정확한 측정을 위해 센서 위치 고려
3. **전원 공급**: 안정적인 전원 공급 필요
4. **환기**: H2O2 사용 시 적절한 환기 필요

## 업데이트 내역

### v1.0 (최신)
- 타이머 시나리오 시스템 구현
- 콤프레샤 보호 로직 완성
- RS485 통신 안정화
- OLED UI 개선
- 30초 평균 습도 계산
- 자동 정지 기능

## 라이선스

이 프로젝트는 교육 및 연구 목적으로 제작되었습니다.

## 연락처

프로젝트 관련 문의사항이 있으시면 이슈를 통해 연락해 주세요.