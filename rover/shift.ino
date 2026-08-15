void shiftWrite()
{
    digitalWrite(shift_latch, LOW);
    shiftOut(shift_data, shift_clock, MSBFIRST, shiftRegisterData);
    digitalWrite(shift_latch, HIGH);
}

void buzzer(bool state)
{
  if(xSemaphoreTake(shift_mutex, portMAX_DELAY) == pdTRUE)
  {
    if(state)
    {
      shiftRegisterData = shiftRegisterData | 0b00000001; 
    }
    else
    {
      shiftRegisterData = shiftRegisterData & 0b11111110;
    }
    shiftWrite();
    xSemaphoreGive(shift_mutex);
  }

}

void red(bool state)
{
  if(xSemaphoreTake(shift_mutex, portMAX_DELAY) == pdTRUE)
  {
    if(state)
    {
      shiftRegisterData = shiftRegisterData | 0b00100000; 
    }
    else
    {
      shiftRegisterData = shiftRegisterData & 0b11011111;
    }
    shiftWrite();
    xSemaphoreGive(shift_mutex);
  }
}

void green(bool state)
{
  if(xSemaphoreTake(shift_mutex, portMAX_DELAY) == pdTRUE)
  {
    if(state)
    {
      shiftRegisterData = shiftRegisterData | 0b01000000; 
    }
    else
    {
      shiftRegisterData = shiftRegisterData & 0b10111111;
    }
    shiftWrite();
    xSemaphoreGive(shift_mutex);
  }
}

void blue(bool state)
{
    if(xSemaphoreTake(shift_mutex, portMAX_DELAY) == pdTRUE)
  {
    if(state)
    {
      shiftRegisterData = shiftRegisterData | 0b10000000; 
    }
    else
    {
      shiftRegisterData = shiftRegisterData & 0b01111111;
    }
    shiftWrite();
    xSemaphoreGive(shift_mutex);
  }
}

void forward() {
  if (xSemaphoreTake(shift_mutex, portMAX_DELAY) == pdTRUE) {

    shiftRegisterData = shiftRegisterData | 0b00010010; 
    shiftRegisterData = shiftRegisterData & 0b11110111;  
    
    shiftWrite(); 
    xSemaphoreGive(shift_mutex);
  }
}


void backward() {
  if (xSemaphoreTake(shift_mutex, portMAX_DELAY) == pdTRUE) {

    shiftRegisterData = shiftRegisterData | 0b00001100; 
    shiftRegisterData = shiftRegisterData & 0b11101101;
    
    shiftWrite(); 
    xSemaphoreGive(shift_mutex);
  }
}


void right() {
  if (xSemaphoreTake(shift_mutex, portMAX_DELAY) == pdTRUE) {

    shiftRegisterData = shiftRegisterData | 0b00001010; 
    shiftRegisterData = shiftRegisterData & 0b11101011; 
    
    shiftWrite(); 
    xSemaphoreGive(shift_mutex);
  }
}


void left() {
  if (xSemaphoreTake(shift_mutex, portMAX_DELAY) == pdTRUE) {
    
    shiftRegisterData = shiftRegisterData | 0b00010100; 
    shiftRegisterData = shiftRegisterData & 0b11110101; 
    
    shiftWrite();
    xSemaphoreGive(shift_mutex);
  }
}


void stop() {
  if (xSemaphoreTake(shift_mutex, portMAX_DELAY) == pdTRUE) {
    shiftRegisterData = shiftRegisterData & 0b11100001; 
    shiftWrite(); 
    xSemaphoreGive(shift_mutex);
  }
}


