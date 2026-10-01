#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <WiFi.h>
#include <time.h>

#define TFT_SCLK 0
#define TFT_MOSI 1
#define TFT_RST  2
#define TFT_DC   3
#define TFT_CS   4
#define TFT_BL   5

const uint8_t BTN_PINS[4] = {6, 7, 20, 21};
enum { B_MODE = 0, B_UP = 1, B_DOWN = 2, B_ALARM = 3 };

bool rawLast[4];
bool stable[4];
unsigned long lastChange[4];

const char* WIFI_SSID = "NETWORK:......?";
const char* WIFI_PASS = "NETWORK PASSWORD:........?";

int lastSecond = -1;

const uint8_t BUZZER = 10;   

enum State { SHOW_TIME, SET_ALM_HOUR, SET_ALM_MIN, RINGING };
State state = SHOW_TIME;

bool needsRedraw = true;
unsigned long ringStart = 0;
unsigned long lastFlash = 0;
bool flashOn = false;


class MyST7789 : public Adafruit_ST7789 {
public:
  MyST7789(int8_t cs, int8_t dc, int8_t mosi, int8_t sclk, int8_t rst)
      : Adafruit_ST7789(cs, dc, mosi, sclk, rst) {}
  void setOffsets(uint8_t col, uint8_t row) {
    _colstart = _colstart2 = col;
    _rowstart = _rowstart2 = row;
  }
};

MyST7789 tft(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);

bool alarmOn = false;
int alarmHours = 7;
int alarmMinutes = 30;


void setupDisplay() {
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, LOW);
  tft.init(76, 284);
  tft.setOffsets(82, 18);
  tft.invertDisplay(false);
  tft.setRotation(1);
  tft.fillScreen(ST77XX_BLACK);
}

bool pressed(int i) {
  bool raw = (digitalRead(BTN_PINS[i]) == LOW);

  if (raw != rawLast[i]) {
    rawLast[i] = raw;
    lastChange[i] = millis();
  }
  if (millis() - lastChange[i] > 30 && raw != stable[i]) {
    stable[i] = raw;
    if (raw) return true;
  }
  return false;
}


void drawTime(int h, int m, uint16_t hourColor, uint16_t minColor) {
  char buf[3];
  tft.setTextSize(6);
  tft.setCursor(52, 4);

  tft.setTextColor(hourColor, ST77XX_BLACK);
  sprintf(buf, "%02d", h);
  tft.print(buf);

  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft.print(":");

  tft.setTextColor(minColor, ST77XX_BLACK);
  sprintf(buf, "%02d", m);
  tft.print(buf);
}

void drawStatus() {
  char buf[16];
  tft.setTextSize(2);
  tft.setCursor(76, 58);
  if (state == SHOW_TIME) {
    if (alarmOn) {
      tft.setTextColor(ST77XX_ORANGE, ST77XX_BLACK);
      sprintf(buf, "ALARM %02d:%02d", alarmHours, alarmMinutes);
      tft.print(buf);
    } else {
      tft.setTextColor(ST77XX_RED, ST77XX_BLACK);
      tft.print("ALARM OFF  ");
    }
  } else {
    tft.setTextColor(ST77XX_CYAN, ST77XX_BLACK);
    tft.print("SET ALARM  ");
  }
}


void setup() {
  Serial.begin(115200);
  setupDisplay();

  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(2);
  tft.setCursor(0, 0);
  tft.print("Connecting...");

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(250);
  }

  tft.fillScreen(ST77XX_BLACK);
  tft.setCursor(0, 0);
  if (WiFi.status() == WL_CONNECTED) {
    configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org");
  } else {
    tft.print("WiFi failed");
  }


  for (int i = 0; i < 4; i++) {
  pinMode(BTN_PINS[i], INPUT_PULLUP);
  }
  pinMode(BUZZER, OUTPUT);  
}


void loop() {
  bool mode = pressed(B_MODE);
  bool up   = pressed(B_UP);
  bool down = pressed(B_DOWN);
  bool alm  = pressed(B_ALARM);

  struct tm t;
  bool haveTime = getLocalTime(&t, 0);

  switch (state) {

    case SHOW_TIME:
      if (mode) {
        state = SET_ALM_HOUR;
        needsRedraw = true;
        break;
      }
      if (alm) {
        alarmOn = !alarmOn;
        needsRedraw = true;
      }
      if (haveTime && (t.tm_sec != lastSecond || needsRedraw)) {
        if (t.tm_sec != lastSecond && alarmOn &&
            t.tm_hour == alarmHours && t.tm_min == alarmMinutes && t.tm_sec == 0) {
          state = RINGING;
          ringStart = millis();
          lastFlash = 0;
          flashOn = false;
          break;
        }
        lastSecond = t.tm_sec;
        needsRedraw = false;
        drawTime(t.tm_hour, t.tm_min, ST77XX_WHITE, ST77XX_WHITE);
        drawStatus();
      }
      break;

    case SET_ALM_HOUR:
      if (mode) {
        state = SET_ALM_MIN;
        needsRedraw = true;
        break;
      }
      if (alm) {
        alarmOn = true;
        state = SHOW_TIME;
        lastSecond = -1;
        break;
      }
      if (up)   { alarmHours = (alarmHours + 1) % 24;  needsRedraw = true; }
      if (down) { alarmHours = (alarmHours + 23) % 24; needsRedraw = true; }
      if (needsRedraw) {
        drawTime(alarmHours, alarmMinutes, ST77XX_YELLOW, ST77XX_WHITE);
        drawStatus();
        needsRedraw = false;
      }
      break;

    case SET_ALM_MIN:
      if (mode || alm) {
        alarmOn = true;
        state = SHOW_TIME;
        lastSecond = -1;
        break;
      }
      if (up)   { alarmMinutes = (alarmMinutes + 1) % 60;  needsRedraw = true; }
      if (down) { alarmMinutes = (alarmMinutes + 59) % 60; needsRedraw = true; }
      if (needsRedraw) {
        drawTime(alarmHours, alarmMinutes, ST77XX_WHITE, ST77XX_YELLOW);
        drawStatus();
        needsRedraw = false;
      }
      break;

    case RINGING:
      if (mode || up || down || alm || millis() - ringStart > 60000) {
        noTone(BUZZER);
        state = SHOW_TIME;
        lastSecond = -1;
        tft.fillScreen(ST77XX_BLACK);
        break;
      }
      if (millis() - lastFlash >= 400) {
        lastFlash = millis();
        flashOn = !flashOn;
        tft.fillScreen(flashOn ? ST77XX_RED : ST77XX_BLACK);
        tft.setTextSize(4);
        tft.setTextColor(ST77XX_WHITE);
        tft.setCursor(46, 22);
        tft.print("WAKE UP!");
        if (flashOn) tone(BUZZER, 2000);
        else noTone(BUZZER);
      }
      break;
  }
}