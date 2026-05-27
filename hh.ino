#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <EEPROM.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);

// -------- PINS --------
#define SERVO_PIN 23
#define MOSFET_PIN 5
#define RELAY1 18
#define RELAY2 19

#define VIN_PIN 35
#define VOUT_PIN 34

#define BUTTON 27
#define POT1 32

#define LED_HEART 2
#define LED_STABLE 4
#define LED_FAULT 16
#define BUZZER 17

// -------- SYSTEM --------
float target = 24.0;
float tolerance = 0.05;
float scaleVin = 5.7;
float scaleVout = 11.0;

// -------- MODE --------
enum Mode { DEMO_MODE, AUTO_MODE, MANUAL_MODE };
Mode currentMode = DEMO_MODE;

// -------- STATE --------
enum State { MODE_SELECT, RUN };
State state = MODE_SELECT;
unsigned long stateStart = 0;

// -------- CONTROL --------
int manualRelay = 1;
int pwmValue = 120;
int lastLevel = 0;

// -------- SERVO --------
int servoLevelAngle[5] = {0, 38, 82, 118, 152};
int lastServoAngle = -1;

// -------- DEMO --------
unsigned long lastStepTime = 0;
#define STEP_DELAY 3000
int step = 0;
int lastAppliedStep = -1;

// -------- BUTTON --------
bool lastBtn = HIGH;
unsigned long pressStart = 0;

// ================= BUZZER ENGINE =================
unsigned long buzzerTimer = 0;
int buzzerMode = 0;

enum {
  BUZ_OFF,
  BUZ_SHORT,
  BUZ_DOUBLE,
  BUZ_LEVEL
};

void startBuzzer(int mode) {
  buzzerMode = mode;
  buzzerTimer = millis();
}

void updateBuzzer() {

  switch (buzzerMode) {

    case BUZ_OFF:
      digitalWrite(BUZZER, LOW);
      break;

    case BUZ_SHORT:
      if (millis() - buzzerTimer < 100) digitalWrite(BUZZER, HIGH);
      else {
        digitalWrite(BUZZER, LOW);
        buzzerMode = BUZ_OFF;
      }
      break;

    case BUZ_DOUBLE:
      if (millis() - buzzerTimer < 100) digitalWrite(BUZZER, HIGH);
      else if (millis() - buzzerTimer < 200) digitalWrite(BUZZER, LOW);
      else if (millis() - buzzerTimer < 300) digitalWrite(BUZZER, HIGH);
      else {
        digitalWrite(BUZZER, LOW);
        buzzerMode = BUZ_OFF;
      }
      break;

    case BUZ_LEVEL:
      if (millis() - buzzerTimer < 50) digitalWrite(BUZZER, HIGH);
      else {
        digitalWrite(BUZZER, LOW);
        buzzerMode = BUZ_OFF;
      }
      break;
  }
}

// ================= PWM =================
void initPWM() {
  ledcAttach(MOSFET_PIN, 5000, 8);
  ledcAttach(SERVO_PIN, 50, 16);
}

// ================= SERVO =================
void setServo(int angle) {
  if (abs(angle - lastServoAngle) > 2) {
    int duty = map(angle, 0, 180, 1638, 8192);
    ledcWrite(SERVO_PIN, duty);
    lastServoAngle = angle;
  }
}

void moveToLevel(int lvl) {
  setServo(servoLevelAngle[lvl]);
}

// ================= VOLTAGE =================
float readVoltage(int pin, float scale) {
  long sum = 0;
  for (int i = 0; i < 20; i++) {
    sum += analogRead(pin);
    delayMicroseconds(80);
  }
  return (sum / 20.0 / 4095.0) * 3.3 * scale;
}

// ================= EXPECTED VIN =================
float expectedVin(int lvl) {
  switch(lvl) {
    case 1: return 12.0;
    case 2: return 11.3;
    case 3: return 10.6;
    case 4: return 9.9;
  }
  return 0;
}

// ================= RELAY CONTROL =================
void applyStep(int s) {

  if (s == lastAppliedStep) return;

  int lvl = s + 1;

  // Servo first
  moveToLevel(lvl);
  delay(250);

  // Safe release
  digitalWrite(RELAY1, LOW);
  digitalWrite(RELAY2, LOW);
  delay(80);

  // Apply relay
  switch(s) {
    case 0: digitalWrite(RELAY1, LOW); break;
    case 1: digitalWrite(RELAY1, HIGH); break;
    case 2: digitalWrite(RELAY2, LOW); break;
    case 3: digitalWrite(RELAY2, HIGH); break;
  }

  lastAppliedStep = s;
  lastLevel = lvl;

  startBuzzer(BUZ_LEVEL); // 🔥 LEVEL BEEP
}

// ================= MODE MENU =================
void modeMenu() {

  int pot = analogRead(POT1);
  int sel = map(pot, 0, 4095, 0, 2);

  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("SELECT MODE");

  lcd.setCursor(0,1);

  if (sel == 0) lcd.print("> DEMO");
  if (sel == 1) lcd.print("> AUTO");
  if (sel == 2) lcd.print("> MANUAL");

  if (digitalRead(BUTTON) == LOW) {
    delay(200);
    currentMode = (Mode)sel;
    startBuzzer(BUZ_DOUBLE); // 🔥 MODE BEEP
    state = RUN;
  }

  if (millis() - stateStart > 5000) {
    state = RUN;
  }
}

// ================= BUTTON =================
void handleButton() {
  bool cur = digitalRead(BUTTON);

  if (lastBtn == HIGH && cur == LOW) {
    pressStart = millis();
    startBuzzer(BUZ_SHORT); // 🔥 CLICK BEEP
  }

  if (lastBtn == LOW && cur == HIGH) {
    if (millis() - pressStart > 1000) {
      state = MODE_SELECT;
      stateStart = millis();
    }
  }

  lastBtn = cur;
}

// ================= AUTO =================
int decide(float V) {
  if (V < 22.5) return 4;
  else if (V < 23.5) return 3;
  else if (V < 24.2) return 2;
  else return 1;
}

// ================= UI =================
void drawUI(float V, float Vin) {

  lcd.clear();

  lcd.setCursor(0,0);
  lcd.print(V,1);
  lcd.print("V ");

  lcd.print("I:");
  lcd.print(Vin,1);

  lcd.setCursor(0,1);

  if (currentMode == DEMO_MODE) lcd.print("DEMO ");
  else if (currentMode == AUTO_MODE) lcd.print("AUTO ");
  else lcd.print("MAN ");

  lcd.print("L");
  lcd.print(lastLevel);

  lcd.print(" E:");
  lcd.print(expectedVin(lastLevel),1);
}

// ================= SETUP =================
void setup() {

  pinMode(RELAY1, OUTPUT);
  pinMode(RELAY2, OUTPUT);
  pinMode(BUTTON, INPUT_PULLUP);

  pinMode(LED_HEART, OUTPUT);
  pinMode(LED_STABLE, OUTPUT);
  pinMode(LED_FAULT, OUTPUT);
  pinMode(BUZZER, OUTPUT);

  initPWM();

  lcd.init();
  lcd.backlight();

  lcd.print("FINAL SYSTEM");
  delay(1200);

  startBuzzer(BUZ_DOUBLE); // 🔥 STARTUP BEEP

  state = MODE_SELECT;
  stateStart = millis();
}

// ================= LOOP =================
void loop() {

  handleButton();

  float Vin = readVoltage(VIN_PIN, scaleVin);
  float Vout = readVoltage(VOUT_PIN, scaleVout);

  if (state == MODE_SELECT) {
    modeMenu();
    updateBuzzer();
    return;
  }

  int lvl;

  if (currentMode == DEMO_MODE) {

    if (millis() - lastStepTime > STEP_DELAY) {
      step++;
      if (step > 3) step = 0;
      lastStepTime = millis();
    }

    applyStep(step);
    lvl = step + 1;
  }

  else if (currentMode == AUTO_MODE) {
    lvl = decide(Vout);
    applyStep(lvl - 1);
  }

  else {
    int pot = analogRead(POT1);
    lvl = map(pot, 0, 4095, 1, 4);
    applyStep(lvl - 1);
  }

  ledcWrite(MOSFET_PIN, pwmValue);

  digitalWrite(LED_HEART, millis()%500<250);
  digitalWrite(LED_STABLE, abs(target - Vout) < tolerance);
  digitalWrite(LED_FAULT, (Vout < 20));

  drawUI(Vout, Vin);

  updateBuzzer(); // 🔥 IMPORTANT

  delay(100);
}