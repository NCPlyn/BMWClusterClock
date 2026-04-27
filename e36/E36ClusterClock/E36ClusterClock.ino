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
  // UTC
  if (tz == "UTC") return "UTC0";

  // Western European Time (UTC+0 / UTC+1 DST)
  if (tz == "Europe/London")     return "GMT0BST,M3.5.0/1,M10.5.0/2";
  if (tz == "Europe/Dublin")     return "IST-1GMT0,M10.5.0/2,M3.5.0/1";
  if (tz == "Europe/Lisbon")     return "WET0WEST,M3.5.0/1,M10.5.0/2";
  if (tz == "Atlantic/Canary")   return "WET0WEST,M3.5.0/1,M10.5.0/2";
  if (tz == "Atlantic/Faroe")    return "WET0WEST,M3.5.0/1,M10.5.0/2";
  if (tz == "Atlantic/Madeira")  return "WET0WEST,M3.5.0/1,M10.5.0/2";

  // Central European Time (UTC+1 / UTC+2 DST)
  if (tz == "Europe/Amsterdam")  return "CET-1CEST,M3.5.0/2,M10.5.0/3";
  if (tz == "Europe/Andorra")    return "CET-1CEST,M3.5.0/2,M10.5.0/3";
  if (tz == "Europe/Belgrade")   return "CET-1CEST,M3.5.0/2,M10.5.0/3";
  if (tz == "Europe/Berlin")     return "CET-1CEST,M3.5.0/2,M10.5.0/3";
  if (tz == "Europe/Bratislava") return "CET-1CEST,M3.5.0/2,M10.5.0/3";
  if (tz == "Europe/Brussels")   return "CET-1CEST,M3.5.0/2,M10.5.0/3";
  if (tz == "Europe/Budapest")   return "CET-1CEST,M3.5.0/2,M10.5.0/3";
  if (tz == "Europe/Copenhagen") return "CET-1CEST,M3.5.0/2,M10.5.0/3";
  if (tz == "Europe/Gibraltar")  return "CET-1CEST,M3.5.0/2,M10.5.0/3";
  if (tz == "Europe/Ljubljana")  return "CET-1CEST,M3.5.0/2,M10.5.0/3";
  if (tz == "Europe/Luxembourg") return "CET-1CEST,M3.5.0/2,M10.5.0/3";
  if (tz == "Europe/Madrid")     return "CET-1CEST,M3.5.0/2,M10.5.0/3";
  if (tz == "Europe/Malta")      return "CET-1CEST,M3.5.0/2,M10.5.0/3";
  if (tz == "Europe/Monaco")     return "CET-1CEST,M3.5.0/2,M10.5.0/3";
  if (tz == "Europe/Oslo")       return "CET-1CEST,M3.5.0/2,M10.5.0/3";
  if (tz == "Europe/Paris")      return "CET-1CEST,M3.5.0/2,M10.5.0/3";
  if (tz == "Europe/Podgorica")  return "CET-1CEST,M3.5.0/2,M10.5.0/3";
  if (tz == "Europe/Prague")     return "CET-1CEST,M3.5.0/2,M10.5.0/3";
  if (tz == "Europe/Rome")       return "CET-1CEST,M3.5.0/2,M10.5.0/3";
  if (tz == "Europe/San_Marino") return "CET-1CEST,M3.5.0/2,M10.5.0/3";
  if (tz == "Europe/Sarajevo")   return "CET-1CEST,M3.5.0/2,M10.5.0/3";
  if (tz == "Europe/Skopje")     return "CET-1CEST,M3.5.0/2,M10.5.0/3";
  if (tz == "Europe/Stockholm")  return "CET-1CEST,M3.5.0/2,M10.5.0/3";
  if (tz == "Europe/Tirane")     return "CET-1CEST,M3.5.0/2,M10.5.0/3";
  if (tz == "Europe/Vaduz")      return "CET-1CEST,M3.5.0/2,M10.5.0/3";
  if (tz == "Europe/Vatican")    return "CET-1CEST,M3.5.0/2,M10.5.0/3";
  if (tz == "Europe/Vienna")     return "CET-1CEST,M3.5.0/2,M10.5.0/3";
  if (tz == "Europe/Warsaw")     return "CET-1CEST,M3.5.0/2,M10.5.0/3";
  if (tz == "Europe/Zagreb")     return "CET-1CEST,M3.5.0/2,M10.5.0/3";
  if (tz == "Europe/Zurich")     return "CET-1CEST,M3.5.0/2,M10.5.0/3";
  if (tz == "Arctic/Longyearbyen") return "CET-1CEST,M3.5.0/2,M10.5.0/3";

  // Eastern European Time (UTC+2 / UTC+3 DST)
  if (tz == "Europe/Athens")     return "EET-2EEST,M3.5.0/3,M10.5.0/4";
  if (tz == "Europe/Bucharest")  return "EET-2EEST,M3.5.0/3,M10.5.0/4";
  if (tz == "Europe/Chisinau")   return "EET-2EEST,M3.5.0/3,M10.5.0/4";
  if (tz == "Europe/Helsinki")   return "EET-2EEST,M3.5.0/3,M10.5.0/4";
  if (tz == "Europe/Kyiv")       return "EET-2EEST,M3.5.0/3,M10.5.0/4";
  if (tz == "Europe/Mariehamn")  return "EET-2EEST,M3.5.0/3,M10.5.0/4";
  if (tz == "Europe/Nicosia")    return "EET-2EEST,M3.5.0/3,M10.5.0/4";
  if (tz == "Europe/Riga")       return "EET-2EEST,M3.5.0/3,M10.5.0/4";
  if (tz == "Europe/Sofia")      return "EET-2EEST,M3.5.0/3,M10.5.0/4";
  if (tz == "Europe/Tallinn")    return "EET-2EEST,M3.5.0/3,M10.5.0/4";
  if (tz == "Europe/Uzhgorod")   return "EET-2EEST,M3.5.0/3,M10.5.0/4";
  if (tz == "Europe/Vilnius")    return "EET-2EEST,M3.5.0/3,M10.5.0/4";
  if (tz == "Europe/Zaporozhye") return "EET-2EEST,M3.5.0/3,M10.5.0/4";
  if (tz == "Asia/Nicosia")      return "EET-2EEST,M3.5.0/3,M10.5.0/4";

  // Moscow Time (UTC+3, no DST)
  if (tz == "Europe/Kaliningrad") return "EET-2";
  if (tz == "Europe/Moscow")     return "MSK-3";
  if (tz == "Europe/Minsk")      return "FET-3";
  if (tz == "Europe/Simferopol") return "MSK-3";
  if (tz == "Europe/Istanbul")   return "TRT-3";
  if (tz == "Asia/Istanbul")     return "TRT-3";

  // Iceland (UTC+0, no DST)
  if (tz == "Atlantic/Reykjavik") return "GMT0";

  // Fallback
  Serial.print("Unknown TZ: ");
  Serial.println(tz);
  Serial.println("Falling back to Czech time (CET)");
  return "CET-1CEST,M3.5.0/2,M10.5.0/3";
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
