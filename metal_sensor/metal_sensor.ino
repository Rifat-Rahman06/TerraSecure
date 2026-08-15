#include <avr/power.h>
#include <Wire.h>
#include <Servo.h>

#define SLAVE_ADDRESS 0x08
#define LED_RED 9
#define LED_BLUE 10
#define PULSE_PIN 7
#define ADC_CHANNEL 0          // 0 = ADC0 (A0). If the coil decay wire is on
                               // another pin, change this AND the pinMode below.
#define BASELINE 630           // decay reading threshold of the no-metal state

Servo servo1;
Servo servo2;

int def_1 = 175;
int def_2 = 15;
int servo_delay = 8;

int value = 0;
int sensitivity = 300;           // threshold 0..625 (300 = mid default until
                                 // the rover pushes its own value)
bool metal = false;
bool dropPending = false;

String receivedData;

void setup() {
  Wire.begin(SLAVE_ADDRESS);
  Wire.onReceive(receiveEvent);
  Wire.onRequest(requestEvent);

  pinMode(LED_RED, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);

  // 12-bit ADC (requires the LGT8FX core)
  analogReadResolution(12);

  // Coil pulse output
  pinMode(PULSE_PIN, OUTPUT);
  digitalWrite(PULSE_PIN, LOW);

  // Decay signal input - must match ADC_CHANNEL (0 = A0)
  pinMode(A0, INPUT);

  // ADC: AVcc reference, channel ADC0, prescaler 8 -> 2 MHz ADC clock @ 16 MHz
  ADMUX = (1 << REFS0) | (ADC_CHANNEL & 0x1F);
  ADCSRA = (1 << ADEN) | (1 << ADPS1) | (1 << ADPS2);

  servo1.attach(5);
  servo1.write(def_1);
  delay(1000);
  servo2.attach(6);
  servo2.write(def_2);

  Serial.begin(9600);
}

void loop() {
  // Pulse the search coil
  PORTD |= (1 << PD7);
  delayMicroseconds(150);
  PORTD &= ~(1 << PD7);

  // Wait for the decay to settle slightly, then sample it
  delayMicroseconds(90);

  value = readsensor();

  // signal grows as the decay reading drops below the baseline
  int signal = BASELINE - value;

  if (signal >= sensitivity) {
    metal = true;
    digitalWrite(LED_RED, HIGH);
    digitalWrite(LED_BLUE, HIGH);
  } else {
    metal = false;
    digitalWrite(LED_RED, LOW);
    digitalWrite(LED_BLUE, LOW);
  }

  if (dropPending) {
    drop();
    dropPending = false;
  }

  delayMicroseconds(100);
}

int readsensor() {
  ADCSRA |= (1 << ADSC);
  while (ADCSRA & (1 << ADSC));
  return ADC;
}

/* ------------------------------ I2C SLAVE -------------------------------- */
void receiveEvent(int numBytes) {
  while (Wire.available()) {
    char c = Wire.read();
    receivedData += c;
  }

  if (receivedData[0] == 'd') {
    dropPending = true;
  } else {
    int thresh = receivedData.toInt();
    if (thresh < 0) thresh = 0;
    if (thresh > 625) thresh = 625;
    sensitivity = thresh;
  }
  receivedData = "";
}

void requestEvent() {
  Wire.write(metal ? 'T' : 'F');

  int signal = BASELINE - value;
  if (signal < 0) signal = 0;
  int strength = (int)((long)signal * 100L / BASELINE);
  if (strength > 100) strength = 100;
  Wire.write(strength);
}

/* ------------------------------ SERVO DROP ------------------------------- */
void drop() {
  for (int angle = 1; angle <= 93; angle++) {
    def_1 -= 1;
    def_2 += 1;
    servo1.write(def_1);
    servo2.write(def_2);
    delay(servo_delay);
  }

  delay(500);

  for (int angle = 1; angle <= 93; angle++) {
    def_1 += 1;
    def_2 -= 1;
    servo1.write(def_1);
    servo2.write(def_2);
    delay(servo_delay);
  }
}