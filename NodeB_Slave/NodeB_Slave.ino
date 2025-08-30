/*
  FTM02 (0~5V 아날로그 출력, 12V 구동)
  A0: 온도 채널, A1: 습도 채널
  - 0~5V -> (온도/습도) 선형 변환
  - EMA(지수이동평균)로 스파이크 완화
  - 시리얼 모니터로 주기 출력
  - (옵션) RS485로 "온도,습도" 필터값만 전송 가능 (아래 USE_RS485=1)

  배선:
    FTM02 +12V  -> 12V SMPS
    FTM02 GND   -> Arduino GND (공통 접지)
    FTM02 TEMP  -> A0 (0~5V)
    FTM02 HUM   -> A1 (0~5V)

  ⚠️ 주의: A0/A1에는 5V를 넘는 전압이 들어가면 안 됩니다.
*/

//// ================= 사용자 설정(보정) =================
// 아날로그 기준/ADC
const float VREF    = 5.00;   // 기준전압(기본 5V)
const int   ADC_MAX = 1023;   // 10비트 ADC

// 온도 변환: 0~5V -> T_MIN ~ T_MAX (센서 스펙에 맞게 변경)
// 예) 0~5V -> 0~100°C 로 가정 (필요 시 T_MIN/T_MAX 수정)
const float T_MIN = -20.0;      // V=0V일 때 온도
const float T_MAX = 80.0;    // V=5V일 때 온도

// 습도 변환: 0~5V -> RH_MIN ~ RH_MAX (일반 가정: 0~100%RH)
const float RH_MIN = 0.0;     // V=0V일 때 %RH
const float RH_MAX = 100.0;   // V=5V일 때 %RH

// EMA 필터 파라미터(0.05~0.30 권장). 작을수록 부드럽고, 클수록 민감.
const float ALPHA  = 0.15;

// 샘플링/출력 간격
const unsigned long SAMPLE_PERIOD_MS = 100;  // 100ms마다 측정(10Hz)
const unsigned long PRINT_PERIOD_MS  = 500;  // 0.5초마다 표시

// (작은 원천 평균) 한 번 읽을 때 소량 다중샘플
const int   N_SAMPLES       = 4;    // 1~8 권장
const int   SAMPLE_DELAY_US = 300;  // 샘플 사이 짧은 지연


// 핀 지정
const int PIN_TEMP = A0;   // 온도 채널
const int PIN_HUM  = A1;   // 습도 채널

// 타이밍
unsigned long lastSampleAt = 0;
unsigned long lastPrintAt  = 0;

// EMA 상태
bool  emaInited = false;
float tempEMA   = 0.0;
float humEMA    = 0.0;

// 다중샘플 평균으로 전압 읽기
float readVoltageAvg(int pin) {
  // 첫 샘플 버림(채널 안정화용) - 필요시 주석 해제
  // analogRead(pin);

  long sum = 0;
  for (int i = 0; i < N_SAMPLES; i++) {
    sum += analogRead(pin);
    delayMicroseconds(SAMPLE_DELAY_US);
  }
  float avgADC = (float)sum / (float)N_SAMPLES;
  return (avgADC * VREF) / ADC_MAX;
}

// 전압 -> 온도(선형)
float voltageToTemp(float v) {
  float t = T_MIN + (v / VREF) * (T_MAX - T_MIN);
  return t;
}

// 전압 -> 습도(선형)
float voltageToRH(float v) {
  float rh = RH_MIN + (v / VREF) * (RH_MAX - RH_MIN);
  // 보기 좋은 범위로 약간의 클램프
  if (rh < -5.0)  rh = -5.0;
  if (rh > 105.0) rh = 105.0;
  return rh;
}

void setup() {
  Serial.begin(115200);
  analogReference(DEFAULT); // 외부 레퍼런스를 쓸 경우 EXTERNAL

  Serial.println(F("FTM02 온도/습도 측정 시작 (A0=온도, A1=습도, EMA 필터)"));
  Serial.print(F("T_MIN~T_MAX=")); Serial.print(T_MIN); Serial.print("~"); Serial.println(T_MAX);
  Serial.print(F("RH_MIN~RH_MAX=")); Serial.print(RH_MIN); Serial.print("~"); Serial.println(RH_MAX);
  Serial.print(F("ALPHA=")); Serial.println(ALPHA, 2);
}

void loop() {
  unsigned long now = millis();

  // 1) 주기적으로 두 채널 샘플링
  if (now - lastSampleAt >= SAMPLE_PERIOD_MS) {
    lastSampleAt = now;

    // 원천 노이즈 감소를 위한 소량 다중샘플 평균
    float vTemp = readVoltageAvg(PIN_TEMP);
    float vHum  = readVoltageAvg(PIN_HUM);

    float tRaw  = voltageToTemp(vTemp);
    float rhRaw = voltageToRH(vHum);

    // EMA 필터 적용
    if (!emaInited) {
      tempEMA = tRaw;
      humEMA  = rhRaw;
      emaInited = true;
    } else {
      tempEMA = ALPHA * tRaw  + (1.0 - ALPHA) * tempEMA;
      humEMA  = ALPHA * rhRaw + (1.0 - ALPHA) * humEMA;
    }
  }

  // 2) 화면 출력(너무 잦은 로그 방지)
  if (now - lastPrintAt >= PRINT_PERIOD_MS) {
    lastPrintAt = now;
    Serial.print(F("온도[°C]: "));
    Serial.print(tempEMA, 1);
    Serial.print(F("  습도[%RH]: "));
    Serial.println(humEMA, 1);
  }
}