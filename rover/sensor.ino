bool read_metal_sensor(int &strength) {
  bool result = false;
  strength = 0;

  if (xSemaphoreTake(i2c_mutex, portMAX_DELAY) == pdTRUE) {
    Wire.requestFrom(SLAVE_ADDRESS, 2);
    if (Wire.available() >= 2) {
      char flag = (char)Wire.read();
      strength = Wire.read();
      result = (flag == 'T');
    }
    xSemaphoreGive(i2c_mutex);
  }
  return result;
}

void drop_item() {
  if (xSemaphoreTake(i2c_mutex, portMAX_DELAY) == pdTRUE) {
    Wire.beginTransmission(SLAVE_ADDRESS);
    Wire.write('d');
    Wire.endTransmission();
    xSemaphoreGive(i2c_mutex);
  }
}

void set_sensor_sensitivity(int value) {
  if (value < 0) value = 0;
  if (value > 625) value = 625;

  String data = String(value);
  if (xSemaphoreTake(i2c_mutex, portMAX_DELAY) == pdTRUE) {
    Wire.beginTransmission(SLAVE_ADDRESS);
    Wire.write(reinterpret_cast<const uint8_t *>(data.c_str()), data.length());
    Wire.endTransmission();
    xSemaphoreGive(i2c_mutex);
  }
}