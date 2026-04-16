#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <time.h>

#define PIN_SPEEDO  D5
#define PIN_TACHO   D6

// ===== RPM CALIBRATION =====
/*const float TACHO_CAL[][2] = { //diesel
  {    0,   0.0 },
  { 1000,  23.5 },
  { 1500,  37.5 },
  { 2000,  53.0 },
  { 2500,  71.5 },
  { 3000,  88.5 },
  { 3500, 103.5 },
  { 4000, 117.0 },
  { 4500, 135.5 },
  { 5000, 152.5 },
  { 5500, 169.5 },
  { 6000, 182.0 }
};*/
const float TACHO_CAL[][2] = { //gas x cyl
  {    0,   0.0 },
  {  500,  24.0 },
  { 1000,  41.0 },
  { 1500,  57.5 },
  { 2000,  75.0 },
  { 2500,  89.5 },
  { 3000, 106.0 },
  { 3500, 119.5 },
  { 4000, 137.0 },
  { 4500, 153.5 },
  { 5000, 169.0 },
  { 5500, 184.5 },
  { 6000, 199.5 }
};
const int TACHO_POINTS = sizeof(TACHO_CAL) / sizeof(TACHO_CAL[0]);

// ===== SPEED CALIBRATION =====
const float SPEED_CAL[][2] = {
  {   0,   0 },
  {  20,  26 },
  {  40,  50 },
  {  60,  75 },
  {  80, 100 },
  { 100, 127 },
  { 120, 152 },
  { 140, 176 },
  { 160, 202 },
  { 180, 228 },
  { 200, 254 },
  { 220, 279 },
  { 240, 305 }
};
const int SPEED_POINTS = sizeof(SPEED_CAL) / sizeof(SPEED_CAL[0]);

// ===== INTERPOLATION =====
float interpolate(const float table[][2], int size, float input) {
  if (input <= table[0][0]) return table[0][1];
  for (int i = 1; i < size; i++) {
    if (input <= table[i][0]) {
      float x0 = table[i-1][0], y0 = table[i-1][1];
      float x1 = table[i][0], y1 = table[i][1];
      float t = (input - x0) / (x1 - x0);
      return y0 + t * (y1 - y0);
    }
  }
  return table[size - 1][1];
}

// ===== SMOOTHING =====
float currentSpeedHz = 0;
float currentRpmHz   = 0;

float targetSpeedHz = 0;
float targetRpmHz   = 0;

float smoothing = 0.05; // needle smoothness

// ===== TIME =====
struct tm timeinfo;

// ===== AUTO TIMEZONE VIA IP =====
String getTimezoneFromIP() {
  WiFiClient client;
  HTTPClient http;
  http.begin(client, "http://ip-api.com/json");
  int httpCode = http.GET();
  if (httpCode == 200) {
    String payload = http.getString();
    int index = payload.indexOf("\"timezone\":\"");
    if (index > 0) {
      int start = index + 12;
      int end = payload.indexOf("\"", start);
      String tz = payload.substring(start, end);
      Serial.print("Detected timezone: ");
      Serial.println(tz);
      return tz;
    }
  }
  Serial.println("Failed to get timezone, fallback to UTC");
  return "UTC";
}

String mapTZtoPOSIX(String tz) {
  if (tz == "Europe/Prague") return "CET-1CEST,M3.5.0/2,M10.5.0/3";
  if (tz == "Europe/London") return "GMT0BST,M3.5.0/1,M10.5.0/2";
  if (tz == "Europe/Berlin") return "CET-1CEST,M3.5.0/2,M10.5.0/3";
  if (tz == "UTC") return "UTC0";
  Serial.println("Unknown TZ, fallback to UTC");
  return "UTC0";
}

// ===== STARTUP SWEEP =====
void startupSweep() {
  Serial.println("Startup sweep...");
  float maxSpeedHz = SPEED_CAL[SPEED_POINTS - 1][1];
  float maxRpmHz   = TACHO_CAL[TACHO_POINTS - 1][1];

  for (float t = 0.0; t <= 1.0; t += 0.01) {
    tone(PIN_SPEEDO, (int)(t * maxSpeedHz));
    tone(PIN_TACHO,  (int)(t * maxRpmHz));
    delay(10);
  }

  delay(500);

  for (float t = 1.0; t >= 0.0; t -= 0.01) {
    tone(PIN_SPEEDO, (int)(t * maxSpeedHz));
    tone(PIN_TACHO,  (int)(t * maxRpmHz));
    delay(10);
  }

  noTone(PIN_SPEEDO);
  noTone(PIN_TACHO);
  Serial.println("Sweep done");
}

// ===== SETUP =====
void setup() {
  Serial.begin(115200);

  pinMode(PIN_SPEEDO, OUTPUT);
  pinMode(PIN_TACHO, OUTPUT);

  WiFiManager wm;
  wm.autoConnect("E36-CLOCK");

  String tzDetected = getTimezoneFromIP();
  String posixTZ = mapTZtoPOSIX(tzDetected);

  Serial.print("Using POSIX TZ: ");
  Serial.println(posixTZ);

  configTime(posixTZ.c_str(), "pool.ntp.org");

  Serial.println("Waiting for time sync...");
  while (!getLocalTime(&timeinfo)) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nTime synced!");
  startupSweep();
}

// ===== LOOP =====
void loop() {
  getLocalTime(&timeinfo);

  int hours   = timeinfo.tm_hour;
  int minutes = timeinfo.tm_min;
  int seconds = timeinfo.tm_sec;

  int kmh = hours * 10;
  float rpm = minutes * 100.0 + (seconds / 60.0) * 100.0;

  targetSpeedHz = interpolate(SPEED_CAL, SPEED_POINTS, kmh);
  targetRpmHz   = interpolate(TACHO_CAL, TACHO_POINTS, rpm);

  currentSpeedHz += (targetSpeedHz - currentSpeedHz) * smoothing;
  currentRpmHz   += (targetRpmHz   - currentRpmHz)   * smoothing;

  tone(PIN_SPEEDO, (int)currentSpeedHz);
  tone(PIN_TACHO,  (int)currentRpmHz);

  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 1000) {
    lastPrint = millis();
    Serial.printf("%02d:%02d:%02d → %d km/h | %.0f RPM\n", hours, minutes, seconds, kmh, rpm);
  }

  delay(20);
}