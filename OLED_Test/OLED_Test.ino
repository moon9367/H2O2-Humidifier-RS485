#include <Wire.h>
#include <U8g2lib.h>

// Page buffer 모드 (RAM 절약, 안정적)
U8G2_SH1106_128X64_NONAME_1_HW_I2C u8g2(U8G2_R0);

void setup() {
  Wire.begin();
  Wire.setClock(400000);   // I2C 속도 400kHz
  u8g2.begin();            // OLED 초기화
}

void loop() {
  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(0, 15, "Hello, Arduino!");
    u8g2.drawStr(0, 35, "OLED 1.3 inch OK");

    static int counter = 0;
    char buf[20];
    sprintf(buf, "Count: %d", counter++);
    u8g2.drawStr(0, 55, buf);
  } while (u8g2.nextPage());

  delay(1000);
}
