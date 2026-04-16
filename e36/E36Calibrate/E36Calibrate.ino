#define PIN_SPEEDO D5
#define PIN_TACHO D6

bool rpmCalibrate = true;
bool speedCalibrate = false;
bool testall = false;

//xxxCalibrate
float freq = 0.0;
float stepHz = 0.5; // how fast it increases (adjust if needed)
int delayMs = 200; // speed of sweep

//testall
const float RPM_TABLE[][2] = { // { RPM, Hz }
  {    0,   0.0 },
  { 1000,  24.0 },
  { 1500,  37.5 },
  { 2000,  53.0 },
  { 2500,  71.5 },
  { 3000,  89.0 },
  { 3500, 103.5 },
  { 4000, 117.0 },
  { 4500, 135.5 },
  { 5000, 153.0 },
  { 5500, 169.5 },
  { 6000, 182.0 }
};
const int RPM_POINTS = sizeof(RPM_TABLE) / sizeof(RPM_TABLE[0]);

const float SPEED_TABLE[][2] = { // { km/h, Hz }
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
const int SPEED_POINTS = sizeof(SPEED_TABLE) / sizeof(SPEED_TABLE[0]);

int stepIndex = 0;
bool testingRPM = true;
unsigned long lastStep = 0;
const int stepDelay = 4000; // 4 seconds per step

void setup() {
  Serial.begin(115200);
  pinMode(PIN_TACHO, OUTPUT);
  pinMode(PIN_SPEEDO, OUTPUT);
}

void loop() {
  if(rpmCalibrate) {
    tone(PIN_TACHO, (int)freq);

    Serial.print("Hz: ");
    Serial.println(freq);

    freq += stepHz;

    // stop at some safe max
    if (freq > 250) {
      Serial.println("Restarting...");
      freq = 0;
      delay(3000);
    }

    delay(delayMs);
  } else if (speedCalibrate) {
    tone(PIN_SPEEDO, (int)freq);

    Serial.print("Hz: ");
    Serial.println(freq);

    freq += stepHz;

    if (freq > 200) {
      Serial.println("Restarting...");
      freq = 0;
      delay(3000);
    }

    delay(delayMs);
  } else if (testall) {
    unsigned long now = millis();
    if (now - lastStep >= stepDelay) {
      lastStep = now;
      if (testingRPM) {
        // --- RPM TEST ---
        float rpm = RPM_TABLE[stepIndex][0];
        float hz  = RPM_TABLE[stepIndex][1];

        tone(PIN_TACHO, (int)hz);
        noTone(PIN_SPEEDO);

        Serial.printf("RPM TEST → %.0f RPM @ %.1f Hz\n", rpm, hz);

        stepIndex++;

        if (stepIndex >= RPM_POINTS) {
          stepIndex = 0;
          testingRPM = false;
          Serial.println("---- Switching to SPEED ----");
          delay(2000);
        }
      } else {
        // --- SPEED TEST ---
        float kmh = SPEED_TABLE[stepIndex][0];
        float hz  = SPEED_TABLE[stepIndex][1];

        tone(PIN_SPEEDO, (int)hz);
        noTone(PIN_TACHO);

        Serial.printf("SPEED TEST → %.0f km/h @ %.1f Hz\n", kmh, hz);

        stepIndex++;

        if (stepIndex >= SPEED_POINTS) {
          stepIndex = 0;
          testingRPM = true;
          Serial.println("---- Switching to RPM ----");
          delay(2000);
        }
      }
    }
  }
}