#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>

// ---------- Pins ----------
const int joystickPin = A0;
const int submitPin   = 2;
const int helpPin     = 3;
const int buzzer      = 8;
const int touchPin    = 5;

const int redPin   = 9;
const int greenPin = 6;
const int bluePin  = 10;

#define DHTPIN  4
#define DHTTYPE DHT11

// ---------- Devices ----------
LiquidCrystal_I2C lcd(0x27, 16, 2);   // use the address your scanner found
DHT dht(DHTPIN, DHTTYPE);

// ---------- Timing ----------
const unsigned long DHT_INTERVAL = 2000;
const unsigned long DEBOUNCE_MS  = 30;
const unsigned long MESSAGE_MS   = 1500;
const unsigned long BUZZER_MS    = 500;
const unsigned long GREEN_TIME   = 3000;

unsigned long lastDhtRead  = 0;
unsigned long messageUntil = 0;
unsigned long buzzerUntil  = 0;
unsigned long resolvedAt   = 0;

// ---------- Request state machine ----------
enum RequestState { IDLE, PENDING, RESOLVED };
RequestState reqState = IDLE;

// ---------- State ----------
float temperature = NAN;
float humidity    = NAN;
bool  sensorRead  = false;
int painLevel     = 0;
int lastPainLevel = -1;
int submittedPain = -1;          // -1 = none yet
int helpCount     = 0;
bool topDirty     = true;
bool bottomDirty  = true;
bool lastTouch    = LOW;
char message[17]  = "";

// ---------- Button helper (non-blocking debounce) ----------
struct Button {
  byte pin;
  bool lastReading;
  bool stableState;
  unsigned long lastChange;
};

Button submitBtn = {submitPin, HIGH, HIGH, 0};
Button helpBtn   = {helpPin,   HIGH, HIGH, 0};

bool wasPressed(Button &b) {
  bool reading = digitalRead(b.pin);
  bool pressed = false;

  if (reading != b.lastReading) {
    b.lastChange = millis();
    b.lastReading = reading;
  }

  if (millis() - b.lastChange > DEBOUNCE_MS && reading != b.stableState) {
    b.stableState = reading;
    if (b.stableState == LOW) pressed = true;
  }
  return pressed;
}

// ---------- RGB LED ----------
// Assumes a common-cathode LED (HIGH = on). See notes below for common-anode.
void setColor(bool red, bool green, bool blue) {
  digitalWrite(redPin,   red   ? HIGH : LOW);
  digitalWrite(greenPin, green ? HIGH : LOW);
  digitalWrite(bluePin,  blue  ? HIGH : LOW);
}

// ---------- LCD helpers ----------
void printRow(byte row, const char* text) {
  lcd.setCursor(0, row);
  byte len = strlen(text);
  for (byte i = 0; i < 16; i++) {
    lcd.print(i < len ? text[i] : ' ');
  }
}

void showMessage(const char* text) {
  snprintf(message, sizeof(message), "%s", text);
  messageUntil = millis() + MESSAGE_MS;
  topDirty = true;
}

// Row 0: pain info (or a temporary message)
void showTopRow() {
  char line[17];
  if (millis() < messageUntil) {
    snprintf(line, sizeof(line), "%s", message);
  } else if (submittedPain < 0) {
    snprintf(line, sizeof(line), "P:%d S:- H:%d", painLevel, helpCount);
  } else {
    snprintf(line, sizeof(line), "P:%d S:%d H:%d", painLevel, submittedPain, helpCount);
  }
  printRow(0, line);
}

// Row 1: sensor data
void showBottomRow() {
  char line[17];
  if (!sensorRead) {
    strcpy(line, "Reading...");
  } else if (isnan(temperature) || isnan(humidity)) {
    strcpy(line, "Sensor Error!");
  } else {
    char t[8];
    dtostrf(temperature, 1, 1, t);
    snprintf(line, sizeof(line), "T:%sC H:%d%%", t, (int)(humidity + 0.5));
  }
  printRow(1, line);
}

// ---------- Serial helper ----------
const char* stateName() {
  switch (reqState) {
    case IDLE:     return "IDLE";
    case PENDING:  return "PENDING";
    default:       return "RESOLVED";
  }
}

void printStatus() {
  Serial.print("STATUS | Pain (live): ");
  Serial.print(painLevel);

  Serial.print(" | Pain (submitted): ");
  if (submittedPain < 0) Serial.print("none");
  else Serial.print(submittedPain);

  Serial.print(" | Help requests: ");
  Serial.print(helpCount);

  Serial.print(" | Request: ");
  Serial.print(stateName());

  Serial.print(" | Temp: ");
  if (isnan(temperature)) Serial.print("ERR");
  else { Serial.print(temperature, 1); Serial.print(" C"); }

  Serial.print(" | Humidity: ");
  if (isnan(humidity)) Serial.println("ERR");
  else { Serial.print(humidity, 0); Serial.println(" %"); }
}

void setup() {
  Serial.begin(9600);

  pinMode(submitPin, INPUT_PULLUP);
  pinMode(helpPin, INPUT_PULLUP);
  pinMode(touchPin, INPUT);

  pinMode(buzzer, OUTPUT);
  digitalWrite(buzzer, LOW);

  pinMode(redPin, OUTPUT);
  pinMode(greenPin, OUTPUT);
  pinMode(bluePin, OUTPUT);
  setColor(true, true, true);       // start WHITE

  dht.begin();

  lcd.init();
  lcd.backlight();
  printRow(0, "CareBridge AI");
  printRow(1, "Starting...");
  Serial.println("CareBridge AI starting...");
  delay(2000);
  lcd.clear();

  // Draw both rows right away, before any sensor read
  painLevel = constrain(map(analogRead(joystickPin), 0, 1023, 0, 11), 0, 10);
  showTopRow();
  showBottomRow();
  topDirty = false;
  bottomDirty = false;
}

void loop() {
  unsigned long now = millis();

  // --- Joystick -> pain level (0-10) ---
  int joystickValue = analogRead(joystickPin);
  painLevel = constrain(map(joystickValue, 0, 1023, 0, 11), 0, 10);

  if (painLevel != lastPainLevel) {
    lastPainLevel = painLevel;
    topDirty = true;

    Serial.print("LEVEL:");
    Serial.print(painLevel);
    Serial.print("  (raw ");
    Serial.print(joystickValue);
    Serial.println(")");
  }

  // --- Submit pain level ---
  if (wasPressed(submitBtn)) {
    submittedPain = painLevel;
    Serial.print("PAIN:");
    Serial.println(submittedPain);
    char msg[17];
    snprintf(msg, sizeof(msg), "Pain sent: %d", submittedPain);
    showMessage(msg);
  }

  // --- Help button: only starts a new request when idle ---
  if (wasPressed(helpBtn) && reqState == IDLE) {
    reqState = PENDING;
    helpCount++;
    setColor(true, false, false);            // RED = help requested
    Serial.println("HELP_REQUEST");
    digitalWrite(buzzer, HIGH);
    buzzerUntil = now + BUZZER_MS;
    showMessage("Nurse called!");
  }

  // --- Nurse touches sensor: resolve the request ---
  bool touch = digitalRead(touchPin);
  if (touch == HIGH && lastTouch == LOW && reqState == PENDING) {
    reqState = RESOLVED;
    resolvedAt = now;
    setColor(false, true, false);            // GREEN = resolved
    Serial.println("REQUEST_RESOLVED");
    showMessage("Request resolved");
  }
  lastTouch = touch;

  // --- After 3 seconds, back to WHITE ---
  if (reqState == RESOLVED && now - resolvedAt >= GREEN_TIME) {
    reqState = IDLE;
    setColor(true, true, true);
    Serial.println("SYSTEM_READY");
  }

  // --- Turn buzzer off after 0.5 s ---
  if (buzzerUntil != 0 && now >= buzzerUntil) {
    digitalWrite(buzzer, LOW);
    buzzerUntil = 0;
  }

  // --- Read DHT11 every 2 seconds ---
  if (now - lastDhtRead >= DHT_INTERVAL) {
    lastDhtRead = now;
    temperature = dht.readTemperature();
    humidity    = dht.readHumidity();
    sensorRead  = true;

    if (isnan(temperature) || isnan(humidity)) {
      Serial.println("DHT11 read failed");
    } else {
      Serial.print("TEMP:");
      Serial.println(temperature, 1);
      Serial.print("HUMIDITY:");
      Serial.println(humidity, 0);
    }

    printStatus();
    bottomDirty = true;
    topDirty = true;
  }

  // --- Message expired -> back to pain info ---
  if (messageUntil != 0 && now >= messageUntil) {
    messageUntil = 0;
    topDirty = true;
  }

  // --- Redraw rows only when something changed ---
  if (topDirty) {
    showTopRow();
    topDirty = false;
  }
  if (bottomDirty) {
    showBottomRow();
    bottomDirty = false;
  }

  delay(10);
}