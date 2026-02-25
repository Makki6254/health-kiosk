#include "display.h"
#include "sensors.h"

bool initSDCard() {
    Serial.println("=== Initializing SD Card ===");
    Serial.printf("Using pins: CS=%d, MOSI=%d, MISO=%d, SCK=%d\n", 
                  SD_CS, SD_MOSI, SD_MISO, SD_SCK);
    
    // Initialize SPI with correct pins
    SPI.begin(SD_SCK, SD_MISO, SD_MOSI);
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);
    delay(100);
    
    Serial.print("Mounting SD card...");
    if (!SD.begin(SD_CS)) {
        Serial.println(" FAILED!");
        return false;
    }
    Serial.println(" SUCCESS!");
    
    uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE) {
        Serial.println("No SD card found");
        return false;
    }
    
    Serial.print("SD Card Type: ");
    if (cardType == CARD_MMC) {
        Serial.println("MMC");
    } else if (cardType == CARD_SD) {
        Serial.println("SDSC");
    } else if (cardType == CARD_SDHC) {
        Serial.println("SDHC");
    } else {
        Serial.println("UNKNOWN");
    }
    
    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    Serial.printf("Card Size: %lluMB\n", cardSize);
    
    // Create data file if it doesn't exist
    if (!SD.exists(DATA_FILENAME)) {
        Serial.println("Creating health data file...");
        File file = SD.open(DATA_FILENAME, FILE_WRITE);
        if (file) {
            String header = "Timestamp,Name,Age,Gender,Address,Weight(kg),Height(cm),Temperature(C),BMI,HeartRate(BPM),BP_Sys,BP_Dia";
            file.println(header);
            file.close();
            Serial.println("Created data file with headers");
        } else {
            Serial.println("Failed to create data file");
            return false;
        }
    } else {
        Serial.println("Health data file already exists");
    }
    
    return true;
}

bool saveHealthData(const String& data) {
    Serial.println("Saving health data to SD card...");
    Serial.println(data);
    
    File file = SD.open(DATA_FILENAME, FILE_APPEND);
    if (!file) {
        Serial.println("Failed to open file for writing");
        return false;
    }
    
    file.println(data);
    file.close();
    Serial.println("Health data saved successfully!");
    
    return true;
}

String readHealthData() {
    Serial.println("Reading health data from SD card...");
    
    if (!SD.exists(DATA_FILENAME)) {
        return "No health data file found";
    }
    
    File file = SD.open(DATA_FILENAME);
    if (!file) {
        return "Error: Could not open file";
    }
    
    String content = "";
    int lineCount = 0;
    
    // Read all lines
    while (file.available() && lineCount < 50) {
        String line = file.readStringUntil('\n');
        content += line + "\n";
        lineCount++;
    }
    file.close();
    
    Serial.printf("Read %d records\n", lineCount);
    return content;
}

bool deleteHealthData() {
    Serial.println("Deleting all health data...");
    
    if (SD.remove(DATA_FILENAME)) {
        // Recreate empty file
        File file = SD.open(DATA_FILENAME, FILE_WRITE);
        if (file) {
            String header = "Timestamp,Name,Age,Gender,Address,Weight(kg),Height(cm),Temperature(C),BMI,HeartRate(BPM),BP_Sys,BP_Dia";
            file.println(header);
            file.close();
            Serial.println("All data cleared successfully");
            return true;
        }
    }
    
    Serial.println("Failed to delete data");
    return false;
}