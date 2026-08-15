#include <Arduino.h>
#include <MPU6050.h>
#include <Wire.h>
#include <QMC5883LCompass.h>
#include <SPI.h>
#include <LoRa.h>
#include <math.h>

/* -------------------------------- I2C ------------------------------------ */
#define SLAVE_ADDRESS 0x08

/* -------------------------------- PINS ----------------------------------- */
const uint8_t shift_latch = 33;
const uint8_t shift_clock = 32;
const uint8_t shift_data = 25;
const uint8_t voltage_pin = 35;
const uint8_t I2C_SDA = 21;
const uint8_t I2C_SCL = 22;
const uint8_t trigPin = 27;
const uint8_t echoPin = 34;
const uint8_t motor_enable_pin = 26;
const uint8_t GPS_RX_PIN = 16;
const uint8_t GPS_TX_PIN = 17;

/* ------------------------------ INSTANCES -------------------------------- */
MPU6050 mpu;
QMC5883LCompass compass;

/* ------------------------------ SHARED STATE ----------------------------- */
bool metal = false;                 // current metal state
bool obstacle = false;              // sonar obstacle flag
int voltage_pct = 0;                // battery percent 0..100
float tilt[2] = {0, 0};             // tilt[0] = pitch, tilt[1] = roll
int heading = 0;                    // compass 0..359 deg

int sensitivity_threshold = 300;    // 0..625 sent to the sensor slave
int sens_pct = 50;                  // 0..100 reported to HMI (knob position)
bool sens_dirty = true;            // slave needs a threshold update (initial sync)

char move = '0';                    // current move command
int speed_pwm = 80;                 // motor PWM 80..255
int speed_pct = 0;                  // 0..100 reported to HMI
bool beep = false;                  // buzzer request
bool drop_req = false;              // drop marker request

double gps_lat = 0;                 // decimal degrees
double gps_lon = 0;
bool got_fix = false;

bool auto_on = false;               // autonomous mode flag
bool auto_reset = false;            // restart the search pattern
unsigned long last_auto_drop = 0;   // auto-drop cooldown

volatile bool pending_detection = false;
String det_lat_s = "";
String det_lon_s = "";
int det_sig = 0;

struct Bounds {
  bool set = false;
  double n = 0, s = 0, e = 0, w = 0;
};
Bounds bounds;

byte shiftRegisterData = 0b00000000;

/* ------------------------------- MUTEXES --------------------------------- */
SemaphoreHandle_t shift_mutex;
SemaphoreHandle_t voltage_mutex;
SemaphoreHandle_t tilt_mutex;
SemaphoreHandle_t compass_mutex;
SemaphoreHandle_t sonar_mutex;
SemaphoreHandle_t metal_mutex;
SemaphoreHandle_t sensitivity_mutex;
SemaphoreHandle_t drop_mutex;
SemaphoreHandle_t move_mutex;
SemaphoreHandle_t speed_mutex;
SemaphoreHandle_t beep_mutex;
SemaphoreHandle_t gps_mutex;
SemaphoreHandle_t nav_mutex;
SemaphoreHandle_t i2c_mutex;
SemaphoreHandle_t det_mutex;

/* ---------------------------- TASK HANDLES ------------------------------- */
TaskHandle_t sensor_data = NULL;
TaskHandle_t sonar_data = NULL;
TaskHandle_t metal_data = NULL;
TaskHandle_t lora_data = NULL;
TaskHandle_t go_data = NULL;
TaskHandle_t beep_data = NULL;
TaskHandle_t coord_data = NULL;
TaskHandle_t auto_data = NULL;

/* --------------------------- AUTO NAV CONSTANTS -------------------------- */
#define WAYPOINT_RADIUS_M 3.0
#define TURN_TOLERANCE_DEG 20.0
#define STRIPE_SPACING_M 5.0
#define AUTO_DROP_COOLDOWN_MS 15000UL

/* --------------------------- FUNCTION PROTOTYPES ------------------------- */
void sensor_task(void *pvParameters);
void sonar_task(void *pvParameters);
void metal_task(void *pvParameters);
void lora_task(void *pvParameters);
void go_task(void *pvParameters);
void beep_task(void *pvParameters);
void coord_task(void *pvParameters);
void auto_task(void *pvParameters);

bool read_metal_sensor(int &strength);
void drop_item();
void set_sensor_sensitivity(int value);
void parse_bounds(const String &cmd);
double haversine_m(double lat1, double lon1, double lat2, double lon2);
double bearing_deg(double lat1, double lon1, double lat2, double lon2);
double nmea_to_decimal(const String &raw, const String &hemi);

/* -------------------------------- SETUP ---------------------------------- */
void setup() {
  Serial.begin(9600);

  LoRa.setPins(5, 2, 4);  // NSS = GPIO 5, Reset = GPIO 4, DIO0 = GPIO 2
  if (!LoRa.begin(433E6)) {
    Serial.println("LoRa init failed!");
    while (1);
  }
  delay(2000);
  Serial.println("LoRa Initialized");

  Wire.begin(I2C_SDA, I2C_SCL);
  mpu.initialize();
  compass.init();

  Serial1.begin(9600, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);

  shift_mutex = xSemaphoreCreateMutex();
  beep_mutex = xSemaphoreCreateMutex();
  tilt_mutex = xSemaphoreCreateMutex();
  compass_mutex = xSemaphoreCreateMutex();
  voltage_mutex = xSemaphoreCreateMutex();
  sonar_mutex = xSemaphoreCreateMutex();
  metal_mutex = xSemaphoreCreateMutex();
  sensitivity_mutex = xSemaphoreCreateMutex();
  drop_mutex = xSemaphoreCreateMutex();
  move_mutex = xSemaphoreCreateMutex();
  speed_mutex = xSemaphoreCreateMutex();
  gps_mutex = xSemaphoreCreateMutex();
  nav_mutex = xSemaphoreCreateMutex();
  i2c_mutex = xSemaphoreCreateMutex();
  det_mutex = xSemaphoreCreateMutex();

  pinMode(shift_latch, OUTPUT);
  pinMode(shift_clock, OUTPUT);
  pinMode(shift_data, OUTPUT);
  shiftWrite();

  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  pinMode(motor_enable_pin, OUTPUT);
  pinMode(voltage_pin, INPUT);

  xTaskCreatePinnedToCore(sensor_task, "Task 1", 4096, NULL, 1, &sensor_data, 1);
  xTaskCreatePinnedToCore(sonar_task, "Task 2", 4096, NULL, 1, &sonar_data, 1);
  xTaskCreatePinnedToCore(metal_task, "Task 3", 4096, NULL, 1, &metal_data, 1);
  xTaskCreatePinnedToCore(lora_task, "Task 4", 4096, NULL, 1, &lora_data, 1);
  xTaskCreatePinnedToCore(go_task, "Task 5", 4096, NULL, 1, &go_data, 1);
  xTaskCreatePinnedToCore(beep_task, "Task 6", 4096, NULL, 0, &beep_data, 0);
  xTaskCreatePinnedToCore(coord_task, "Task 7", 4096, NULL, 0, &coord_data, 0);
  xTaskCreatePinnedToCore(auto_task, "Task 8", 4096, NULL, 1, &auto_data, 1);
}

void loop() {
  // all work is done inside FreeRTOS tasks
}

/* ------------------------------ LORA TASK -------------------------------- */
void lora_task(void *pvParameters) {
  unsigned long telemetry_time = 0;

  while (true) {
    int packetSize = LoRa.parsePacket();
    if (packetSize) {
      String packet = "";
      while (LoRa.available()) {
        packet += (char)LoRa.read();
      }

      if (packet.length() == 0) {
        continue;
      }

      bool auto_state = false;
      if (xSemaphoreTake(nav_mutex, portMAX_DELAY) == pdTRUE) {
        auto_state = auto_on;
        xSemaphoreGive(nav_mutex);
      }

      if (packet == "START") {
        if (xSemaphoreTake(nav_mutex, portMAX_DELAY) == pdTRUE) {
          auto_on = true;
          auto_reset = true;
          xSemaphoreGive(nav_mutex);
        }
        Serial.println("AUTO ON");
      } else if (packet == "STOP") {
        if (xSemaphoreTake(nav_mutex, portMAX_DELAY) == pdTRUE) {
          auto_on = false;
          xSemaphoreGive(nav_mutex);
        }
        if (xSemaphoreTake(move_mutex, portMAX_DELAY) == pdTRUE) {
          move = '0';
          xSemaphoreGive(move_mutex);
        }
        Serial.println("AUTO OFF");
      } else if (packet.startsWith("BOUNDS=")) {
        parse_bounds(packet);
        Serial.println("BOUNDS SET");
      } else if (packet.length() == 1 && packet[0] == 'd') {
        if (xSemaphoreTake(drop_mutex, portMAX_DELAY) == pdTRUE) {
          drop_req = true;
          xSemaphoreGive(drop_mutex);
        }
      } else if (auto_state) {
        // manual motion / speed / sensitivity commands are ignored while
        // autonomous mode is running
      } else if (packet[0] == 'F' || packet[0] == 'B' ||
                 packet[0] == 'R' || packet[0] == 'L') {
        if (xSemaphoreTake(move_mutex, portMAX_DELAY) == pdTRUE) {
          move = packet[0];
          xSemaphoreGive(move_mutex);
        }
      } else if (packet[0] == 's') {
        int v = packet.substring(1).toInt();
        if (v < 0) v = 0;
        if (v > 25) v = 25;
        if (xSemaphoreTake(sensitivity_mutex, portMAX_DELAY) == pdTRUE) {
          sensitivity_threshold = v * 25;  // 0..625 threshold level
          sens_pct = (25 - v) * 4;         // 0..100 % knob position
          sens_dirty = true;
          xSemaphoreGive(sensitivity_mutex);
        }
      } else if (packet[0] == 'g') {
        int v = packet.substring(1).toInt();
        if (v < 0) v = 0;
        if (v > 20) v = 20;
        int pwm = 80 + (v * 175) / 20;     // 80..255 PWM
        if (xSemaphoreTake(speed_mutex, portMAX_DELAY) == pdTRUE) {
          speed_pwm = pwm;
          speed_pct = v * 5;                // 0..100 %
          xSemaphoreGive(speed_mutex);
        }
      }
    }

    /* Send pending metal detection event as soon as possible */
    bool det = false;
    String detLat = "", detLon = "";
    int detSig = 0;
    if (xSemaphoreTake(det_mutex, portMAX_DELAY) == pdTRUE) {
      det = pending_detection;
      if (det) {
        detLat = det_lat_s;
        detLon = det_lon_s;
        detSig = det_sig;
        pending_detection = false;
      }
      xSemaphoreGive(det_mutex);
    }
    if (det) {
      LoRa.beginPacket();
      LoRa.print("d," + detLat + "," + detLon + "," + String(detSig));
      LoRa.endPacket();
    }

    /* Periodic telemetry frame (1 Hz) */
    if (millis() - telemetry_time >= 1000) {
      telemetry_time = millis();

      String data = "[x,";

      if (xSemaphoreTake(gps_mutex, portMAX_DELAY) == pdTRUE) {
        data += String(gps_lat, 6);
        data += ",";
        data += String(gps_lon, 6);
        xSemaphoreGive(gps_mutex);
      }
      data += ",";

      if (xSemaphoreTake(compass_mutex, portMAX_DELAY) == pdTRUE) {
        data += String(heading);
        xSemaphoreGive(compass_mutex);
      }
      data += ",";

      if (xSemaphoreTake(voltage_mutex, portMAX_DELAY) == pdTRUE) {
        data += String(voltage_pct);
        xSemaphoreGive(voltage_mutex);
      }
      data += ",";

      if (xSemaphoreTake(tilt_mutex, portMAX_DELAY) == pdTRUE) {
        data += String(tilt[0]);
        data += ",";
        data += String(tilt[1]);
        xSemaphoreGive(tilt_mutex);
      }
      data += ",";

      if (xSemaphoreTake(speed_mutex, portMAX_DELAY) == pdTRUE) {
        data += String(speed_pct);
        xSemaphoreGive(speed_mutex);
      }
      data += ",";

      if (xSemaphoreTake(sensitivity_mutex, portMAX_DELAY) == pdTRUE) {
        data += String(sens_pct);
        xSemaphoreGive(sensitivity_mutex);
      }
      data += "]";

      LoRa.beginPacket();
      LoRa.print(data);
      LoRa.endPacket();
    }

    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

/* --------------------------- BOUNDS PARSING ------------------------------ */
void parse_bounds(const String &cmd) {
  // Format: BOUNDS=nwLat,nwLon;neLat,neLon;seLat,seLon;swLat,swLon
  String s = cmd.substring(7);
  double vals[8];
  int idx = 0;
  int p = 0;

  while (p < (int)s.length() && idx < 8) {
    int q = s.indexOf(';', p);
    if (q == -1) q = s.length();
    String pair = s.substring(p, q);
    int c = pair.indexOf(',');
    if (c == -1) return;  // malformed
    vals[idx++] = pair.substring(0, c).toDouble();
    vals[idx++] = pair.substring(c + 1).toDouble();
    p = q + 1;
  }
  if (idx != 8) return;

  Bounds b;
  b.set = true;
  b.n = fmax(vals[0], vals[2]);
  b.s = fmin(vals[4], vals[6]);
  b.e = fmax(vals[3], vals[5]);
  b.w = fmin(vals[1], vals[7]);
  if (b.n < b.s || b.e < b.w) return;

  if (xSemaphoreTake(nav_mutex, portMAX_DELAY) == pdTRUE) {
    bounds = b;
    auto_reset = true;
    xSemaphoreGive(nav_mutex);
  }
}

/* ------------------------------ BEEP TASK -------------------------------- */
void beep_task(void *pvParameters) {
  while (true) {
    bool beepp = false;
    if (xSemaphoreTake(beep_mutex, portMAX_DELAY) == pdTRUE) {
      beepp = beep;
      beep = false;
      xSemaphoreGive(beep_mutex);
    }
    if (beepp) {
      buzzer(true);
      vTaskDelay(100 / portTICK_PERIOD_MS);
      buzzer(false);
    }
    vTaskDelay(20 / portTICK_PERIOD_MS);
  }
}

/* ------------------------------ GPS TASK --------------------------------- */
double nmea_to_decimal(const String &raw, const String &hemi) {
  // ddmm.mmmm / dddmm.mmmm -> decimal degrees with hemisphere sign
  double v = raw.toDouble();
  int deg = (int)(v / 100.0);
  double minutes = v - (double)deg * 100.0;
  double dec = (double)deg + minutes / 60.0;
  if (hemi == "S" || hemi == "W") dec = -dec;
  return dec;
}

void coord_task(void *pvParameters) {
  String gpsData = "";

  while (true) {
    while (Serial1.available()) {
      char c = (char)Serial1.read();
      if (c == '\n') {
        if (gpsData.startsWith("$GPRMC")) {
          String fields[7];
          int p = 0;
          int fi = 0;
          while (fi < 7 && p < (int)gpsData.length()) {
            int q = gpsData.indexOf(',', p);
            if (q == -1) q = gpsData.length();
            fields[fi++] = gpsData.substring(p, q);
            p = q + 1;
          }
          // fields: 0=$GPRMC 1=time 2=status 3=lat 4=N/S 5=lon 6=E/W
          if (fi >= 7 && fields[2] == "A") {
            double la = nmea_to_decimal(fields[3], fields[4]);
            double lo = nmea_to_decimal(fields[5], fields[6]);
            if (xSemaphoreTake(gps_mutex, portMAX_DELAY) == pdTRUE) {
              gps_lat = la;
              gps_lon = lo;
              got_fix = true;
              xSemaphoreGive(gps_mutex);
            }
          }
        }
        gpsData = "";
      } else {
        if (gpsData.length() < 128) {
          gpsData += c;
        } else {
          gpsData = "";
        }
      }
    }
    vTaskDelay(5 / portTICK_PERIOD_MS);
  }
}

/* ------------------------------ GO TASK ---------------------------------- */
void go_task(void *pvParameters) {
  while (true) {
    bool sonar = false;
    char step = '0';
    int speedd = 0;
    bool metall = false;

    if (xSemaphoreTake(sonar_mutex, portMAX_DELAY) == pdTRUE) {
      sonar = obstacle;
      xSemaphoreGive(sonar_mutex);
    }
    if (xSemaphoreTake(metal_mutex, portMAX_DELAY) == pdTRUE) {
      metall = metal;
      xSemaphoreGive(metal_mutex);
    }
    if (xSemaphoreTake(speed_mutex, portMAX_DELAY) == pdTRUE) {
      speedd = speed_pwm;
      xSemaphoreGive(speed_mutex);
    }
    if (xSemaphoreTake(move_mutex, portMAX_DELAY) == pdTRUE) {
      step = move;
      move = '0';
      xSemaphoreGive(move_mutex);
    }

    if ((sonar || metall) && step == 'F') {
      if (xSemaphoreTake(beep_mutex, portMAX_DELAY) == pdTRUE) {
        beep = true;
        xSemaphoreGive(beep_mutex);
      }
      continue;
    }

    if (step == 'F') {
      analogWrite(motor_enable_pin, speedd);
      forward();
      vTaskDelay(100 / portTICK_PERIOD_MS);
      stop();
      analogWrite(motor_enable_pin, 0);
    } else if (step == 'B') {
      analogWrite(motor_enable_pin, speedd);
      backward();
      vTaskDelay(100 / portTICK_PERIOD_MS);
      stop();
      analogWrite(motor_enable_pin, 0);
    } else if (step == 'L') {
      analogWrite(motor_enable_pin, speedd);
      left();
      vTaskDelay(100 / portTICK_PERIOD_MS);
      stop();
      analogWrite(motor_enable_pin, 0);
    } else if (step == 'R') {
      analogWrite(motor_enable_pin, speedd);
      right();
      vTaskDelay(100 / portTICK_PERIOD_MS);
      stop();
      analogWrite(motor_enable_pin, 0);
    } else {
      vTaskDelay(20 / portTICK_PERIOD_MS);
    }
  }
}

/* ---------------------------- AUTO NAV TASK ------------------------------ */
double haversine_m(double lat1, double lon1, double lat2, double lon2) {
  double dLat = (lat2 - lat1) * PI / 180.0;
  double dLon = (lon2 - lon1) * PI / 180.0;
  double a = sin(dLat / 2) * sin(dLat / 2) +
             cos(lat1 * PI / 180.0) * cos(lat2 * PI / 180.0) *
             sin(dLon / 2) * sin(dLon / 2);
  double c = 2 * atan2(sqrt(a), sqrt(1 - a));
  return 6371000.0 * c;
}

double bearing_deg(double lat1, double lon1, double lat2, double lon2) {
  double phi1 = lat1 * PI / 180.0;
  double phi2 = lat2 * PI / 180.0;
  double dLon = (lon2 - lon1) * PI / 180.0;
  double y = sin(dLon) * cos(phi2);
  double x = cos(phi1) * sin(phi2) - sin(phi1) * cos(phi2) * cos(dLon);
  double brg = atan2(y, x) * 180.0 / PI;
  if (brg < 0) brg += 360.0;
  return brg;
}

void auto_task(void *pvParameters) {
  int stripe = 0;
  bool northDir = true;
  bool tgtValid = false;
  double tgtLat = 0, tgtLon = 0;
  bool turnDir = true;
  unsigned long metalHoldUntil = 0;

  while (true) {
    bool ena = false;
    Bounds b;
    b.set = false;

    if (xSemaphoreTake(nav_mutex, portMAX_DELAY) == pdTRUE) {
      ena = auto_on;
      b = bounds;
      if (auto_reset) {
        auto_reset = false;
        stripe = 0;
        northDir = true;
        tgtValid = false;
        turnDir = true;
      }
      xSemaphoreGive(nav_mutex);
    }

    char cmd = '0';

    if (ena && b.set) {
      double lat = 0, lon = 0;
      bool fix = false;
      bool ob = false;
      bool met = false;
      int hdg = 0;

      if (xSemaphoreTake(gps_mutex, portMAX_DELAY) == pdTRUE) {
        lat = gps_lat;
        lon = gps_lon;
        fix = got_fix;
        xSemaphoreGive(gps_mutex);
      }
      if (xSemaphoreTake(sonar_mutex, portMAX_DELAY) == pdTRUE) {
        ob = obstacle;
        xSemaphoreGive(sonar_mutex);
      }
      if (xSemaphoreTake(metal_mutex, portMAX_DELAY) == pdTRUE) {
        met = metal;
        xSemaphoreGive(metal_mutex);
      }
      if (xSemaphoreTake(compass_mutex, portMAX_DELAY) == pdTRUE) {
        hdg = heading;
        xSemaphoreGive(compass_mutex);
      }

      if (!fix) {
        cmd = '0';                       // cannot navigate without GPS fix
      } else if (met) {
        metalHoldUntil = millis() + 2500;
        cmd = '0';                       // stop over metal, let the drop finish
      } else if (millis() < metalHoldUntil) {
        cmd = '0';
      } else if (ob) {
        cmd = turnDir ? 'R' : 'L';       // dodge the obstacle, alternate sides
        turnDir = !turnDir;
      } else {
        double clat = lat, clon = lon;
        if (clat > b.n) clat = b.n;
        if (clat < b.s) clat = b.s;
        if (clon > b.e) clon = b.e;
        if (clon < b.w) clon = b.w;
        bool outside = (clat != lat) || (clon != lon);

        if (outside) {
          tgtLat = clat;                 // steer back to the nearest edge point
          tgtLon = clon;
          tgtValid = true;
        } else if (!tgtValid) {
          // pick the next stripe waypoint (lawn-mower pattern)
          double lonSpacing = STRIPE_SPACING_M /
                              (111320.0 * cos(lat * PI / 180.0));
          int tries = 0;
          bool picked = false;
          while (!picked && tries < 10) {
            double lonPos = b.w + (stripe + 0.5) * lonSpacing;
            if (lonPos > b.e) {
              stripe = 0;
              lonPos = b.w + 0.5 * lonSpacing;
            }
            stripe++;
            tgtLat = northDir ? b.n : b.s;
            tgtLon = lonPos;
            double d = haversine_m(lat, lon, tgtLat, tgtLon);
            if (d >= WAYPOINT_RADIUS_M) picked = true;
            tries++;
          }
          tgtValid = true;
        }

        double dist = haversine_m(lat, lon, tgtLat, tgtLon);
        if (dist < WAYPOINT_RADIUS_M) {
          tgtValid = false;
          northDir = !northDir;          // flip direction for the next stripe
          cmd = '0';
        } else {
          double brg = bearing_deg(lat, lon, tgtLat, tgtLon);
          double diff = brg - (double)hdg;
          while (diff > 180) diff -= 360;
          while (diff < -180) diff += 360;
          if (diff > TURN_TOLERANCE_DEG) {
            cmd = 'R';
          } else if (diff < -TURN_TOLERANCE_DEG) {
            cmd = 'L';
          } else {
            cmd = 'F';
          }
        }
      }
    }

    if (ena) {
      if (xSemaphoreTake(move_mutex, portMAX_DELAY) == pdTRUE) {
        move = cmd;
        xSemaphoreGive(move_mutex);
      }
    }

    vTaskDelay(150 / portTICK_PERIOD_MS);
  }
}

/* ----------------------------- SENSOR TASK ------------------------------- */
void sensor_task(void *pvParameters) {
  while (true) {
    /* Battery level: raw ADC 0..4095 -> percent 0..100 */
    int raw = analogRead(voltage_pin);
    int pct = (int)((long)raw * 100L / 4095L);
    if (pct > 100) pct = 100;
    if (pct < 0) pct = 0;
    if (xSemaphoreTake(voltage_mutex, portMAX_DELAY) == pdTRUE) {
      voltage_pct = pct;
      xSemaphoreGive(voltage_mutex);
    }
    vTaskDelay(50 / portTICK_PERIOD_MS);

    /* Tilt (MPU6050) + compass (QMC5883L) - both on the I2C bus,
       guarded by i2c_mutex so the metal sensor task cannot interleave */
    if (xSemaphoreTake(i2c_mutex, portMAX_DELAY) == pdTRUE) {
      int16_t ax, ay, az;
      mpu.getAcceleration(&ax, &ay, &az);
      float ax_g = ax / 16384.0;
      float ay_g = ay / 16384.0;
      float az_g = az / 16384.0;
      float t0 = atan2(ay_g, az_g) * 180.0 / PI;
      float t1 = atan2(-ax_g, sqrt(ay_g * ay_g + az_g * az_g)) * 180.0 / PI;

      int x, y, z;
      compass.read();
      x = compass.getX();
      y = compass.getY();
      z = compass.getZ();
      float temp = atan2(y, x) * 180.0 / PI;
      if (temp < 0) temp += 360;
      int hdg = (int)temp;

      if (xSemaphoreTake(tilt_mutex, portMAX_DELAY) == pdTRUE) {
        tilt[0] = t0;
        tilt[1] = t1;
        xSemaphoreGive(tilt_mutex);
      }
      if (xSemaphoreTake(compass_mutex, portMAX_DELAY) == pdTRUE) {
        heading = hdg;
        xSemaphoreGive(compass_mutex);
      }
      xSemaphoreGive(i2c_mutex);
    }
    vTaskDelay(50 / portTICK_PERIOD_MS);
  }
}

/* ------------------------------ SONAR TASK ------------------------------- */
void sonar_task(void *pvParameters) {
  while (true) {
    long duration, cm;
    digitalWrite(trigPin, LOW);
    delayMicroseconds(5);
    digitalWrite(trigPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(trigPin, LOW);
    duration = pulseIn(echoPin, HIGH);
    cm = (duration / 2) / 29.1;

    if (xSemaphoreTake(sonar_mutex, portMAX_DELAY) == pdTRUE) {
      obstacle = (cm < 45);
      xSemaphoreGive(sonar_mutex);
    }
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

/* ------------------------------ METAL TASK ------------------------------- */
bool prev_metal = false;

void metal_task(void *pvParameters) {
  while (true) {
    int strength = 0;
    bool m = read_metal_sensor(strength);

    if (xSemaphoreTake(metal_mutex, portMAX_DELAY) == pdTRUE) {
      metal = m;
      xSemaphoreGive(metal_mutex);
    }

    /* Rising edge = new detection: report to HMI and auto-drop a marker */
    if (m && !prev_metal) {
      bool fixed = false;
      if (xSemaphoreTake(gps_mutex, portMAX_DELAY) == pdTRUE) {
        fixed = got_fix;
        xSemaphoreGive(gps_mutex);
      }

      if (fixed) {
        double la = 0, lo = 0;
        if (xSemaphoreTake(gps_mutex, portMAX_DELAY) == pdTRUE) {
          la = gps_lat;
          lo = gps_lon;
          xSemaphoreGive(gps_mutex);
        }

        if (xSemaphoreTake(det_mutex, portMAX_DELAY) == pdTRUE) {
          det_lat_s = String(la, 6);
          det_lon_s = String(lo, 6);
          det_sig = strength;
          pending_detection = true;
          xSemaphoreGive(det_mutex);
        }

        /* Drop a physical marker automatically (with cooldown) */
        if (millis() - last_auto_drop > AUTO_DROP_COOLDOWN_MS) {
          if (xSemaphoreTake(drop_mutex, portMAX_DELAY) == pdTRUE) {
            drop_req = true;
            xSemaphoreGive(drop_mutex);
          }
          last_auto_drop = millis();
        }
      }
    }
    prev_metal = m;

    /* Pending drop request -> tell the sensor slave to actuate the servos */
    bool doDrop = false;
    if (xSemaphoreTake(drop_mutex, portMAX_DELAY) == pdTRUE) {
      if (drop_req) {
        doDrop = true;
        drop_req = false;
      }
      xSemaphoreGive(drop_mutex);
    }
    if (doDrop) {
      drop_item();
    }

    /* Pending sensitivity change -> push it to the sensor slave */
    bool doSens = false;
    int thresh = 0;
    if (xSemaphoreTake(sensitivity_mutex, portMAX_DELAY) == pdTRUE) {
      if (sens_dirty) {
        doSens = true;
        thresh = sensitivity_threshold;
        sens_dirty = false;
      }
      xSemaphoreGive(sensitivity_mutex);
    }
    if (doSens) {
      set_sensor_sensitivity(thresh);
    }

    vTaskDelay(80 / portTICK_PERIOD_MS);
  }
}