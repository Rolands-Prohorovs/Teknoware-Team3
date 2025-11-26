#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>

// pins
const int pir = 35;
const int button = 14;
const int ledIntensity = 25;
const int ledCCT = 26;

// pwm set up
const int intensityChannel = 0;
const int cctChannel = 1;

// wifi
const char* ssid = "TP-LinkAS13";
const char* password = "90062834";

// variables
bool motion = false;
bool lightOn = false;
bool isNight = false;

unsigned long lastApiCheck = 0;
unsigned long startTimer = 0;
int mode = 0; // 0=OFF, 1=PIR, 2=ALWAYS ON

// pir interrupt
void IRAM_ATTR pirTrigger() {
  if (mode == 1 && !lightOn) {
    motion = true;
  }
}

// get hours of sunset sunrise
int getSunsetSunrise(String s) {
  String hourStr = s.substring(11, 13);
  return hourStr.toInt();
}

// check night or day
bool checkNight() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected");
    return isNight;
  }

  HTTPClient http;
  http.begin("http://api.sunrise-sunset.org/json?lat=60.9827&lng=25.6612&formatted=0"); // NYC
  int code = http.GET();

  if (code != 200) {
    Serial.println("API call failed");
    http.end();
    return isNight;
  }

  String body = http.getString();
  http.end();

  int r1 = body.indexOf("\"sunrise\":\"");
  int r2 = body.indexOf("\"", r1 + 12);
  String sunrise = body.substring(r1 + 11, r2);

  int s1 = body.indexOf("\"sunset\":\"");
  int s2 = body.indexOf("\"", s1 + 11);
  String sunset = body.substring(s1 + 10, s2);

  Serial.print("Sunrise: "); Serial.println(sunrise);
  Serial.print("Sunset: "); Serial.println(sunset);

  int sunriseHour = getSunsetSunrise(sunrise);
  int sunsetHour = getSunsetSunrise(sunset);

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to get time, assuming day");
    isNight = false;
    return isNight;
  }

  int nowHour = timeinfo.tm_hour;
  Serial.print("Current UTC hour: "); Serial.println(nowHour);

  if (nowHour < sunriseHour || nowHour >= sunsetHour) {
    isNight = true;
  } else {
    isNight = false;
  }

  Serial.print("Night: "); Serial.println(isNight ? "YES" : "NO");
  return isNight;
}

void setup() {
  Serial.begin(115200);

  // wifi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("\nWiFi connected!");

  configTime(0, 0, "pool.ntp.org");

  ledcSetup(intensityChannel, 5000, 8);
  ledcAttachPin(ledIntensity, intensityChannel);
  ledcSetup(cctChannel, 5000, 8);
  ledcAttachPin(ledCCT, cctChannel);

  ledcWrite(intensityChannel, 255);
  ledcWrite(cctChannel, 255);

  // pir & button
  pinMode(pir, INPUT);
  attachInterrupt(digitalPinToInterrupt(pir), pirTrigger, RISING);
  pinMode(button, INPUT_PULLUP);

  // check api at start
  checkNight();

  Serial.println("System ready!");
  delay(30000); // pir warm-up
}

void loop() {
  unsigned long now = millis();

  // check api 
  if (now - lastApiCheck >= 30000) {
    checkNight();
    lastApiCheck = now;
  }

  // changing modes
  static bool lastButton = HIGH;
  bool button = digitalRead(button);

  if (button == LOW && lastButton == HIGH) {
    mode++;
    if (mode > 2) mode = 0;

    Serial.print("Mode: ");
    if (mode == 0) Serial.println("ON");
    if (mode == 1) Serial.println("PIR");
    if (mode == 2) Serial.println("ALWAYS Off");

    delay(200);
  }
  lastButton = button;

  // MODE 0: OFF
  if (mode == 0) {
    ledcWrite(intensityChannel, 0);
    ledcWrite(cctChannel, 0);
    lightOn = false;
    return;
  }

  // MODE 2: ALWAYS ON
  if (mode == 2) {
    if (isNight) {
      ledcWrite(intensityChannel, 255);
      ledcWrite(cctChannel, 128);
    } else {
      ledcWrite(intensityChannel, 0);
      ledcWrite(cctChannel, 0);
    }
    return;
  }

  // MODE 1: PIR MODE
  if (mode == 1) {
    if (!isNight) {
      ledcWrite(intensityChannel, 255);
      ledcWrite(ledCCT, 255);
      lightOn = false;
      return;
    }

    // motion detected
    if (motion) {
      motion = false;
      lightOn = true;
      startTimer = now;

      ledcWrite(intensityChannel, 0);
      ledcWrite(cctChannel, 0);

      Serial.println("Motion → LIGHT ON");
    }

    // timeout
    if (lightOn && now - startTimer >= 5000) {
      lightOn = false;

      ledcWrite(intensityChannel, 255);
      ledcWrite(cctChannel, 255);

      Serial.println("Timeout → LIGHT OFF");
    }
  }
}
