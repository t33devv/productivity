#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET 4
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

int effectiveFace = -1;
const int MPU = 0x68;
int16_t AcX, AcY, AcZ;

const unsigned long POMODORO_25_MS = 25UL * 60UL * 1000UL;
const unsigned long POMODORO_35_MS = 35UL * 60UL * 1000UL;
const unsigned long BREAK_5_MS     = 5UL  * 60UL * 1000UL;

enum TimerMode {
  MODE_IDLE = 0,
  MODE_WORK,
  MODE_BREAK
};

bool running25 = false;
bool paused25  = false;
unsigned long start25 = 0;
unsigned long elapsed25 = 0;
TimerMode mode25 = MODE_IDLE;

bool running35 = false;
bool paused35  = false;
unsigned long start35 = 0;
unsigned long elapsed35 = 0;
TimerMode mode35 = MODE_IDLE;

unsigned long lastFaceChangeMs = 0;
int lastFace = -1;
int activeTimerFace = -1;

const int BUTTON_PIN = 20;
bool lastButtonState = HIGH;
unsigned long lastButtonChangeMs = 0;
const unsigned long DEBOUNCE_MS = 50;

const char* WIFI_SSID = "WIFI_NAME";
const char* WIFI_PASS = "WIFI_PASSWORD";

const char* NTP_SERVER = "pool.ntp.org";
const long  GMT_OFFSET_SEC = 8L * 3600L;
const int   DAYLIGHT_OFFSET_SEC = 0;

const char* WEATHER_URL = "https://weatherbackend-bsd2.onrender.com/weather";
unsigned long lastWeatherMs = 0;
const unsigned long WEATHER_INTERVAL_MS = 10UL * 60UL * 1000UL;
float weatherTempC = NAN;
String weatherDesc = "";
String weatherCity = "";

void updateWeather(bool startup) {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClientSecure client;
  client.setInsecure();

  unsigned long startMs = millis();
  int attempts = 0;

  while (true) {
    HTTPClient http;
    if (!http.begin(client, WEATHER_URL)) {
      http.end();
      break;
    }

    http.setTimeout(5000);
    int httpCode = http.GET();
    if (httpCode > 0) {
      String payload = http.getString();

      StaticJsonDocument<1024> doc;
      DeserializationError err = deserializeJson(doc, payload);
      http.end();

      if (!err) {
        weatherTempC = doc["main"]["temp"] | NAN;
        weatherDesc  = doc["weather"][0]["main"] | "";
        weatherCity  = doc["name"] | "";
        break;
      }
    } else {
      http.end();
    }

    attempts++;

    if (!startup) break;

    if (millis() - startMs > 60000) {
      break;
    }

    delay(1000);
  }
}

void reset25() {
  running25 = false;
  paused25  = false;
  elapsed25 = 0;
  start25   = 0;
  mode25    = MODE_IDLE;
}

void reset35() {
  running35 = false;
  paused35  = false;
  elapsed35 = 0;
  start35   = 0;
  mode35    = MODE_IDLE;
}

void setup() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  Wire.begin(6, 7);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  Wire.beginTransmission(MPU);
  Wire.write(0x6B);
  Wire.write(0);
  Wire.endTransmission(true);

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(200);
  }

  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);

  updateWeather(true);
  lastWeatherMs = millis();
}

void loop() {
  unsigned long now = millis();

  Wire.beginTransmission(MPU);
  Wire.write(0x3B);
  Wire.endTransmission(false);
  Wire.requestFrom(MPU, 6, true);

  AcX = Wire.read() << 8 | Wire.read();
  AcY = Wire.read() << 8 | Wire.read();
  AcZ = Wire.read() << 8 | Wire.read();

  int realFace = getFaceUp();
  int face = realFace;

  if (face == 3 && effectiveFace != -1) {
    face = effectiveFace;
  } else {
    effectiveFace = face;
  }

  bool screenHidden = (realFace == 4);

  int rawButton = digitalRead(BUTTON_PIN);
  if (rawButton != lastButtonState && (now - lastButtonChangeMs) > DEBOUNCE_MS) {
    lastButtonChangeMs = now;
    lastButtonState = rawButton;

    if (rawButton == LOW) {
      if (activeTimerFace == 5 && running25) {
        if (!paused25) {
          elapsed25 = now - start25;
          paused25 = true;
        } else {
          start25 = now - elapsed25;
          paused25 = false;
        }
      } else if (activeTimerFace == 2 && running35) {
        if (!paused35) {
          elapsed35 = now - start35;
          paused35 = true;
        } else {
          start35 = now - elapsed35;
          paused35 = false;
        }
      }
    }
  }

  if (face != lastFace) {
    int prevFace = lastFace;
    lastFace = face;
    lastFaceChangeMs = now;

    if (face == 5) {
      activeTimerFace = 5;
      if (prevFace == 2) {
        reset35();
        reset25();
      }
    } else if (face == 2) {
      activeTimerFace = 2;
      if (prevFace == 5) {
        reset25();
        reset35();
      }
    } else {
      activeTimerFace = -1;
    }

    if (face != 5 && face != 2 && face != 3 && face != 4 && face != 1 && face != 6) {
      reset25();
      reset35();
    }
  }

  if (now - lastWeatherMs > WEATHER_INTERVAL_MS) {
    lastWeatherMs = now;
    updateWeather(false);
  }

  unsigned long remainingMs = 0;

  if (face == 5) {
    if (!running25 && mode25 == MODE_IDLE && (now - lastFaceChangeMs) > 500) {
      running25 = true;
      paused25  = false;
      elapsed25 = 0;
      start25   = now;
      mode25    = MODE_WORK;
    }

    if (running25) {
      unsigned long e = paused25 ? elapsed25 : (now - start25);
      if (!paused25) elapsed25 = e;

      if (mode25 == MODE_WORK) {
        if (e >= POMODORO_25_MS) {
          mode25   = MODE_BREAK;
          running25 = true;
          paused25  = false;
          elapsed25 = 0;
          start25   = now;
          e         = 0;
        }
        if (mode25 == MODE_WORK) {
          remainingMs = (e >= POMODORO_25_MS) ? 0 : (POMODORO_25_MS - e);
        } else {
          remainingMs = BREAK_5_MS;
        }
      } else if (mode25 == MODE_BREAK) {
        if (e >= BREAK_5_MS) {
          running25 = false;
          paused25  = false;
          elapsed25 = 0;
          start25   = 0;
          mode25    = MODE_IDLE;
          remainingMs = POMODORO_25_MS;
        } else {
          remainingMs = BREAK_5_MS - e;
        }
      }
    } else {
      if (mode25 == MODE_IDLE) {
        remainingMs = POMODORO_25_MS;
      } else if (mode25 == MODE_BREAK) {
        remainingMs = BREAK_5_MS;
      }
    }

  } else if (face == 2) {
    if (!running35 && mode35 == MODE_IDLE && (now - lastFaceChangeMs) > 500) {
      running35 = true;
      paused35  = false;
      elapsed35 = 0;
      start35   = now;
      mode35    = MODE_WORK;
    }

    if (running35) {
      unsigned long e = paused35 ? elapsed35 : (now - start35);
      if (!paused35) elapsed35 = e;

      if (mode35 == MODE_WORK) {
        if (e >= POMODORO_35_MS) {
          mode35   = MODE_BREAK;
          running35 = true;
          paused35  = false;
          elapsed35 = 0;
          start35   = now;
          e         = 0;
        }
        if (mode35 == MODE_WORK) {
          remainingMs = (e >= POMODORO_35_MS) ? 0 : (POMODORO_35_MS - e);
        } else {
          remainingMs = BREAK_5_MS;
        }
      } else if (mode35 == MODE_BREAK) {
        if (e >= BREAK_5_MS) {
          running35 = false;
          paused35  = false;
          elapsed35 = 0;
          start35   = 0;
          mode35    = MODE_IDLE;
          remainingMs = POMODORO_35_MS;
        } else {
          remainingMs = BREAK_5_MS - e;
        }
      }
    } else {
      if (mode35 == MODE_IDLE) {
        remainingMs = POMODORO_35_MS;
      } else if (mode35 == MODE_BREAK) {
        remainingMs = BREAK_5_MS;
      }
    }

  } else {
    if (activeTimerFace == 5) {
      if (running25) {
        unsigned long e = paused25 ? elapsed25 : (now - start25);
        if (!paused25) {
          elapsed25 = e;
          paused25 = true;
        }
      }
      if (mode25 == MODE_WORK) {
        unsigned long e = elapsed25;
        remainingMs = (e >= POMODORO_25_MS) ? 0 : (POMODORO_25_MS - e);
      } else if (mode25 == MODE_BREAK) {
        unsigned long e = elapsed25;
        remainingMs = (e >= BREAK_5_MS) ? 0 : (BREAK_5_MS - e);
      } else {
        remainingMs = POMODORO_25_MS;
      }
    } else if (activeTimerFace == 2) {
      if (running35) {
        unsigned long e = paused35 ? elapsed35 : (now - start35);
        if (!paused35) {
          elapsed35 = e;
          paused35 = true;
        }
      }
      if (mode35 == MODE_WORK) {
        unsigned long e = elapsed35;
        remainingMs = (e >= POMODORO_35_MS) ? 0 : (POMODORO_35_MS - e);
      } else if (mode35 == MODE_BREAK) {
        unsigned long e = elapsed35;
        remainingMs = (e >= BREAK_5_MS) ? 0 : (BREAK_5_MS - e);
      } else {
        remainingMs = POMODORO_35_MS;
      }
    } else {
      remainingMs = POMODORO_25_MS;
    }
  }

  int remainingSec = remainingMs / 1000;
  int mm = remainingSec / 60;
  int ss = remainingSec % 60;

  char pomoBuf25[6];
  snprintf(pomoBuf25, sizeof(pomoBuf25), "%02d:%02d", mm, ss);

  char pomoBuf35[5];
  snprintf(pomoBuf35, sizeof(pomoBuf35), "%02d%02d", mm, ss);

  char clockBuf[7] = "------";
  if (face == 1) {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      snprintf(clockBuf, sizeof(clockBuf), "%02d%02d%02d",
               timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    }
  }

  uint8_t rot;
  switch (face) {
    case 5: rot = 0; break;
    case 6: rot = 2; break;
    case 2: rot = 1; break;
    case 1: rot = 3; break;
    case 4: rot = 2; break;
    default: rot = 0; break;
  }

  display.setRotation(rot);

  if (screenHidden) {
    display.clearDisplay();
    display.display();
    delay(50);
    return;
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);

  if (face == 5) {
    if (mode25 == MODE_WORK) {
      if (running25 && !paused25) display.println("Working 25...");
      else if (running25 && paused25) display.println("Paused (Work)...");
      else display.println("Resting...");
    } else if (mode25 == MODE_BREAK) {
      if (running25 && !paused25) display.println("Break 5...");
      else if (running25 && paused25) display.println("Paused (Break)...");
      else display.println("Resting...");
    } else {
      display.println("Resting...");
    }
  } else if (face == 2) {
    if (mode35 == MODE_WORK) {
      if (running35 && !paused35) display.println("Working 35...");
      else if (running35 && paused35) display.println("Paused (Work)...");
      else display.println("Resting...");
    } else if (mode35 == MODE_BREAK) {
      if (running35 && !paused35) display.println("Break 5...");
      else if (running35 && paused35) display.println("Paused (Break)...");
      else display.println("Resting...");
    } else {
      display.println("Resting...");
    }
  } else if (face == 1) {
    display.println("Time:");
  } else if (face == 6) {
    display.println("Weather:");
  }

  if (face == 5) {
    display.setTextSize(4);
    display.setCursor(0, 24);
    display.println(pomoBuf25);
  } else if (face == 2) {
    display.setTextSize(4);
    display.setCursor(0, 24);
    display.println(pomoBuf35);
  } else if (face == 1) {
    display.setTextSize(4);
    display.setCursor(0, 24);
    display.println(clockBuf);
  } else if (face == 6) {
    display.setTextSize(2);
    display.setCursor(0, 24);
    if (!isnan(weatherTempC)) {
      display.printf("%s",
        weatherCity.length() ? weatherCity.c_str() : "Weather");
      display.setCursor(0, 40);
      display.printf("%.1f C", weatherTempC);
      display.setCursor(0, 56);
      display.setTextSize(1);
      display.println(weatherDesc);
    } else {
      display.println("No weather data");
    }
  }

  display.display();
  delay(50);
}

int getFaceUp() {
  float x = AcX;
  float y = AcY;
  float z = AcZ;

  float ax = fabs(x), ay = fabs(y), az = fabs(z);
  int face;

  if (ax > ay && ax > az) {
    face = (x > 0) ? 1 : 2;
  } else if (ay > ax && ay > az) {
    face = (y > 0) ? 3 : 4;
  } else {
    face = (z > 0) ? 5 : 6;
  }

  return face;
}
