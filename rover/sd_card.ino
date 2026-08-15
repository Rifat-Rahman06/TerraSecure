void writeFile(String data) {
  File dataFile = SD.open("/data.txt", FILE_APPEND);
  if (dataFile) {  // Check if the file is successfully opened
    dataFile.println(data);
    dataFile.close();  // Close the file after writing
  } else {
    Serial.println("Failed to open data.txt for writing.");
  }
}


void clearFile() {
  File dataFile = SD.open("/data.txt", FILE_WRITE);  
  if (dataFile) {
    dataFile.close();
    Serial.println("All data cleared from data.txt.");
  } else {
    Serial.println("Failed to open data.txt for clearing.");
  }
}
