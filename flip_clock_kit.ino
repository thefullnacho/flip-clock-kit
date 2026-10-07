/*
 * Flip Clock Kit — standalone firmware
 * ------------------------------------
 * Drives the NotKuro flip-clock mechanism (MakerWorld 908946) in the Tuny
 * case with a single 28BYJ-48 stepper, no WiFi, no Home Assistant, no cloud.
 * Time comes from a DS3231 real-time clock (battery-backed, so it survives
 * power outages). Two buttons set the time. That's the whole product.
 *
 * Board: ESP32 dev board (Arduino framework)
 * RTC:   DS3231 on I2C (SDA=21, SCL=22), CR2032 backup cell
 * Motor: 28BYJ-48 via ULN2003 driver
 * Power: 5V 2A barrel jack -> screw-terminal split to ESP32 (VIN) and ULN2003
 *
 * Mechanism assumption: the stepper drives the MINUTE drum directly, and the
 * HOUR drum advances mechanically once per minute-drum revolution. The
 * firmware only ever counts minutes; the hardware handles hours.
 * If your mechanism variant drives the hour drum separately, see README.
 *
 * Written for the Christmas 2026 gift kits. Attribution:
 *   Mechanism: NotKuro · Case: Tuny · Firmware rewrite: Alex de Brantes
 */

#include <Wire.h>
#include <RTClib.h>
#include <Preferences.h>

// ---------------------------------------------------------------------------
// CALIBRATE PER BUILD — every printed mechanism is slightly different.
// ---------------------------------------------------------------------------
const float STEPS_PER_FLIP = 136.5; // half-steps of the 28BYJ-48 per 1-min flip
                                   // Tuny's reference used 136; community tuning
                                   // lands ~136.53 (8190 steps/hour). Start here,
                                   // calibrate per build -- see README.
const int   STEP_DELAY_MS  = 8;       // ms per half-step during normal flips
const int   BOOT_STEP_DELAY_MS = 4;   // faster during boot fast-forward
const int   MAX_BOOT_FLIPS = 1440;   // cap boot fast-forward at 24h of flips
// ---------------------------------------------------------------------------

// ULN2003 inputs -> ESP32 pins (IN1..IN4). Labeled Dupont leads in the kit.
const int PIN_IN1 = 16;
const int PIN_IN2 = 17;
const int PIN_IN3 = 5;
const int PIN_IN4 = 18;

// Time-set buttons, wired to GND, INPUT_PULLUP (press = LOW).
const int PIN_BTN_HOUR = 32;   // advances one hour per press
const int PIN_BTN_MIN  = 33;   // advances one minute per press

const int PIN_LED = 2;         // built-in LED: blinks on each flip

// 28BYJ-48 half-step sequence (8 steps). Forward = minute drum forward.
const uint8_t HALF_STEP[8][4] = {
  {1,0,0,0},
  {1,1,0,0},
  {0,1,0,0},
  {0,1,1,0},
  {0,0,1,0},
  {0,0,1,1},
  {0,0,0,1},
  {1,0,0,1},
};

RTC_DS3231 rtc;
Preferences prefs;

int      stepPhase = 0;      // current position in HALF_STEP table
float    stepCarry = 0.0;    // fractional-step accumulator (drift correction)
int      shownMinute = -1;   // last minute shown on the drums (0..1439)
uint32_t lastNvsWrite = 0;

void motorPins(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
  digitalWrite(PIN_IN1, a);
  digitalWrite(PIN_IN2, b);
  digitalWrite(PIN_IN3, c);
  digitalWrite(PIN_IN4, d);
}

void motorOff() {
  motorPins(0, 0, 0, 0);     // de-energize: no heat, no holding current
}

// Step the motor N half-steps forward. Fractional carry keeps long-term
// drift at zero even when STEPS_PER_FLIP isn't an integer.
void stepMotor(float steps, int delayMs) {
  stepCarry += steps - (int)steps;
  int whole = (int)steps;
  if (stepCarry >= 1.0) { whole += 1; stepCarry -= 1.0; }

  for (int i = 0; i < whole; i++) {
    stepPhase = (stepPhase + 1) % 8;
    motorPins(HALF_STEP[stepPhase][0], HALF_STEP[stepPhase][1],
              HALF_STEP[stepPhase][2], HALF_STEP[stepPhase][3]);
    delay(delayMs);
  }
  motorOff();
}

void flipMinute(int delayMs) {
  stepMotor(STEPS_PER_FLIP, delayMs);
  digitalWrite(PIN_LED, HIGH); delay(60); digitalWrite(PIN_LED, LOW);
}

int minutesSinceMidnight(const DateTime& t) {
  return t.hour() * 60 + t.minute();
}

// Persist position cheaply: every 15 min + on every manual set is plenty.
// (Writing flash on every flip would wear NVS out in about a year.)
void savePosition(int mins) {
  prefs.putInt("lastMin", mins);
  lastNvsWrite = millis();
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_IN1, OUTPUT); pinMode(PIN_IN2, OUTPUT);
  pinMode(PIN_IN3, OUTPUT); pinMode(PIN_IN4, OUTPUT);
  motorOff();
  pinMode(PIN_BTN_HOUR, INPUT_PULLUP);
  pinMode(PIN_BTN_MIN, INPUT_PULLUP);
  pinMode(PIN_LED, OUTPUT);

  Wire.begin();  // SDA=21, SCL=22
  prefs.begin("flipclock", false);

  if (!rtc.begin()) {
    // No RTC found: blink SOS on the LED and halt. Check the wiring.
    while (true) {
      digitalWrite(PIN_LED, HIGH); delay(150);
      digitalWrite(PIN_LED, LOW);  delay(150);
    }
  }
  if (rtc.lostPower()) {
    // RTC battery dead or first boot: start at 12:00, user sets the time.
    rtc.adjust(DateTime(2026, 12, 25, 12, 0, 0));
  }

  // Boot fast-forward: drums may be behind after a power outage.
  // NVS remembers the last shown minute; step forward to RTC time.
  DateTime now = rtc.now();
  int target = minutesSinceMidnight(now);
  int last = prefs.getInt("lastMin", -1);

  int flips = 0;
  if (last >= 0) {
    flips = target - last;
    if (flips < 0) flips += 1440;          // crossed midnight
    if (flips > MAX_BOOT_FLIPS) flips = MAX_BOOT_FLIPS;
  }
  for (int i = 0; i < flips; i++) {
    flipMinute(BOOT_STEP_DELAY_MS);
  }
  shownMinute = target;
  savePosition(shownMinute);
}

// Advance the RTC by n minutes and drive the drums to match.
void advanceClock(int n) {
  DateTime now = rtc.now();
  rtc.adjust(now + TimeSpan(n * 60));
  for (int i = 0; i < n; i++) flipMinute(STEP_DELAY_MS);
  shownMinute = minutesSinceMidnight(rtc.now());
  savePosition(shownMinute);
}

// Button helper: returns true once per press, then repeats while held.
bool buttonPressed(int pin, uint32_t &lastState, uint32_t &lastRepeat, bool &wasDown) {
  bool down = (digitalRead(pin) == LOW);
  uint32_t t = millis();
  bool fire = false;
  if (down && !wasDown) {                 // fresh press
    fire = true; lastRepeat = t;
  } else if (down && wasDown && t - lastRepeat > 350 && t - lastState > 800) {
    fire = true; lastRepeat = t;          // held: repeat
  }
  if (down && !wasDown) lastState = t;
  wasDown = down;
  // crude debounce: ignore state younger than 40ms on release
  return fire;
}

uint32_t hState=0, hRepeat=0, mState=0, mRepeat=0;
bool hWas=false, mWas=false;

void loop() {
  // Time-set buttons.
  if (buttonPressed(PIN_BTN_MIN, mState, mRepeat, mWas)) advanceClock(1);
  if (buttonPressed(PIN_BTN_HOUR, hState, hRepeat, hWas)) advanceClock(60);

  // Normal operation: flip whenever the RTC minute rolls over.
  DateTime now = rtc.now();
  int m = minutesSinceMidnight(now);
  if (shownMinute >= 0 && m != shownMinute) {
    int n = m - shownMinute;
    if (n < 0) n += 1440;                 // midnight rollover
    if (n > 0 && n <= 5) {                // small gap: catch up silently
      for (int i = 0; i < n; i++) flipMinute(STEP_DELAY_MS);
      shownMinute = m;
    } else if (n > 5) {                   // large gap (was asleep?): resync
      shownMinute = m;                    // drums already fast-forwarded at boot
    }
    if (millis() - lastNvsWrite > 15UL * 60UL * 1000UL) savePosition(shownMinute);
  }
  delay(250);
}
