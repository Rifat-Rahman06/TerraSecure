#include <LoRa.h>

/* -------------------------------- PINS ----------------------------------- */
const int pot1Pin = 34;       // Speed potentiometer (Analog)
const int pot2Pin = 35;       // Sensitivity potentiometer (Analog)
const int joyXPin = 32;       // Joystick X-axis (Forward/Backward)
const int joyYPin = 33;       // Joystick Y-axis (Left/Right)
const int joySwitchPin = 25;  // Joystick switch (drop marker)
const int buzzerPin = 26;     // Buzzer
const int redLEDPin = 13;     // Red LED (beacon activity)
const int greenLEDPin = 12;   // Green LED
const int blueLEDPin = 14;    // Blue LED (power-on)

/* ------------------------------ LoRa pins -------------------------------- */
#define LORA_NSS  5
#define LORA_RST  4
#define LORA_DIO0 16

/* ------------------------------ CONSTANTS -------------------------------- */
#define POT1_MAX 20           // speed pot scale 0..20
#define POT2_MAX 25           // sensitivity pot scale 0..25
#define LOOP_MS 50            // sensor sampling period
#define TX_THROTTLE_MS 100    // minimum gap between LoRa transmissions
#define BEACON_MS 1000        // HMI beacon period
#define LED_BLINK_MS 500      // activity LED blink period

/* ------------------------------ VARIABLES -------------------------------- */
int pot1Value = 0;
int pot2Value = 0;
int joyXValue = 0;
int joyYValue = 0;
char joystick = '0';
volatile bool joySwitchPressed = false;
bool pot1Changed = false;
bool pot2Changed = false;

unsigned long readTime = 0;
unsigned long sendTime = 0;
unsigned long beaconTime = 0;
unsigned long ledTime = 0;
unsigned long lastJoyDebounce = 0;
bool ledState = false;
String serialLine = "";

/* ---------------------------- INTERRUPT ---------------------------------- */
// Pull-up input + button to GND -> press produces a FALLING edge.
void IRAM_ATTR handleJoySwitch() {
  if (millis() - lastJoyDebounce > 50) {
    joySwitchPressed = true;
    lastJoyDebounce = millis();
  }
}

/* ------------------------------ READING ---------------------------------- */
void readSensor() {
  joyXValue = analogRead(joyXPin);
  joyYValue = analogRead(joyYPin);

  char temp = '0';
  if (joyXValue > 3000) {
    temp = 'F';
  } else if (joyXValue < 1000) {
    temp = 'B';
  } else if (joyYValue > 3000) {
    temp = 'R';
  } else if (joyYValue < 1000) {
    temp = 'L';
  }
  joystick = temp;

  int t1 = map(analogRead(pot1Pin), 0, 4096, 0, POT1_MAX);
  if (t1 != pot1Value) {
    pot1Value = t1;
    pot1Changed = true;
  }

  int t2 = map(analogRead(pot2Pin), 0, 4096, 0, POT2_MAX);
  if (t2 != pot2Value) {
    pot2Value = t2;
    pot2Changed = true;
  }
}

/* ------------------------------ LoRa SEND -------------------------------- */
void sendLoRa(const String &msg) {
  LoRa.beginPacket();
  LoRa.print(msg);
  LoRa.endPacket();
}

/* -------------------------------- SETUP ---------------------------------- */
void setup() {
  Serial.begin(115200);

  LoRa.setPins(LORA_NSS, LORA_RST, LORA_DIO0);
  if (!LoRa.begin(433E6)) {
    Serial.println("LoRa init failed!");
    while (1);
  }
  Serial.println("LoRa Initialized");

  pinMode(pot1Pin, INPUT);
  pinMode(pot2Pin, INPUT);
  pinMode(joyXPin, INPUT);
  pinMode(joyYPin, INPUT);

  pinMode(joySwitchPin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(joySwitchPin), handleJoySwitch, FALLING);

  pinMode(buzzerPin, OUTPUT);
  pinMode(redLEDPin, OUTPUT);
  pinMode(greenLEDPin, OUTPUT);
  pinMode(blueLEDPin, OUTPUT);

  digitalWrite(redLEDPin, LOW);
  digitalWrite(greenLEDPin, LOW);
  digitalWrite(blueLEDPin, HIGH);

  readTime = millis();
  sendTime = millis();
  beaconTime = millis();

  // Force an initial sync: the first loop iteration transmits the current pot
  // positions, even when they are zero (change detection would miss them).
  pot1Changed = true;
  pot2Changed = true;
}

/* -------------------------------- LOOP ----------------------------------- */
void loop() {
  /* 1) Rover -> PC: relay every incoming LoRa packet to USB serial */
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    String packet = "";
    while (LoRa.available()) {
      packet += (char)LoRa.read();
    }
    Serial.println(packet);
  }

  /* 2) PC -> Rover: relay complete serial lines over LoRa
        (START / STOP / BOUNDS=... / custom drop commands) */
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n') {
      if (serialLine.length() > 0) {
        sendLoRa(serialLine);
      }
      serialLine = "";
    } else if (serialLine.length() < 120) {
      serialLine += c;
    }
  }

  /* 3) Sample sticks / pots at 50 ms */
  if (millis() - readTime >= LOOP_MS) {
    readSensor();
    readTime = millis();
  }

  /* 4) Transmit pending control commands (at most one per 100 ms) */
  if (millis() - sendTime >= TX_THROTTLE_MS) {
    if (joystick != '0') {
      sendLoRa(String(joystick));
      joystick = '0';
      sendTime = millis();
    } else if (pot1Changed) {
      sendLoRa("g" + String(pot1Value));
      pot1Changed = false;
      sendTime = millis();
    } else if (pot2Changed) {
      // inverted before sending: rover turns this into the threshold,
      // so turning the knob UP means MORE sensitive
      sendLoRa("s" + String(POT2_MAX - pot2Value));
      pot2Changed = false;
      sendTime = millis();
    } else if (joySwitchPressed) {
      sendLoRa("d");
      joySwitchPressed = false;
      sendTime = millis();
    }
  }

  /* 5) 1 Hz HMI beacon: <speed,sensitivity> in percent */
  if (millis() - beaconTime >= BEACON_MS) {
    String beacon = "<" + String(pot1Value * (100 / POT1_MAX)) + "," +
                    String(pot2Value * (100 / POT2_MAX)) + ">";
    Serial.println(beacon);
    beaconTime = millis();
  }

  /* 6) Activity LED blink */
  if (millis() - ledTime >= LED_BLINK_MS) {
    ledState = !ledState;
    digitalWrite(redLEDPin, ledState);
    ledTime = millis();
  }
}