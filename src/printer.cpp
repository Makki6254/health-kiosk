#include "printer.h"

// Global printer instance
ThermalPrinterBLE thermalPrinter;

ThermalPrinterBLE::ThermalPrinterBLE() 
    : connected(false), pClient(nullptr), pWriteCharacteristic(nullptr) {
}

bool ThermalPrinterBLE::begin() {
    Serial.println("=== Initializing BLE Thermal Printer ===");
    
    // Initialize BLE
    NimBLEDevice::init("HealthKiosk");
    NimBLEDevice::setSecurityAuth(true, true, true);
    
    Serial.println("✓ BLE initialized");
    Serial.println("Device Name: HealthKiosk");
    Serial.println("Scanning for thermal printers...");
    
    return true;
}

bool ThermalPrinterBLE::connect() {
    Serial.println("Connecting to thermal printer...");
    
    // Create scan object
    NimBLEScan* pScan = NimBLEDevice::getScan();
    pScan->setActiveScan(true);
    pScan->setInterval(100);
    pScan->setWindow(99);
    
    // Scan for devices
    NimBLEScanResults results = pScan->start(10, false); // Scan for 10 seconds
    
    // Look for printer
    for(int i = 0; i < results.getCount(); i++) {
        NimBLEAdvertisedDevice device = results.getDevice(i);
        String devName = device.getName().c_str();
        
        Serial.printf("Found device: %s [%s]\n", 
                      devName.c_str(), 
                      device.getAddress().toString().c_str());
        
        // Check if it's a printer
        for(int j = 0; j < PRINTER_NAME_COUNT; j++) {
            if(devName.indexOf(PRINTER_NAMES[j]) >= 0) {
                deviceName = devName;
                Serial.println("Found printer: " + deviceName);
                
                // Connect to this device
                pClient = NimBLEDevice::createClient();
                
                if(pClient->connect(&device)) {
                    Serial.println("Connected to printer!");
                    
                    // Get printer service
                    NimBLERemoteService* pService = pClient->getService(PRINTER_SERVICE_UUID);
                    if(pService != nullptr) {
                        pWriteCharacteristic = pService->getCharacteristic(PRINTER_CHARACTERISTIC_UUID);
                        
                        if(pWriteCharacteristic != nullptr) {
                            connected = true;
                            Serial.println("✓ Printer service found and ready");
                            
                            // Initialize printer
                            setNormalSize();
                            setLeftAlign();
                            printLine("Health Kiosk Connected");
                            feedLines(2);
                            
                            pScan->stop();
                            return true;
                        } else {
                            Serial.println("✗ Write characteristic not found");
                        }
                    } else {
                        Serial.println("✗ Printer service not found");
                    }
                    
                    // If we got here, service or characteristic not found
                    pClient->disconnect();
                    pClient = nullptr;
                } else {
                    Serial.println("✗ Failed to connect to device");
                }
            }
        }
    }
    
    pScan->stop();
    Serial.println("✗ No printer found");
    return false;
}

bool ThermalPrinterBLE::isConnected() {
    return connected && pClient && pClient->isConnected();
}

void ThermalPrinterBLE::disconnect() {
    if(pClient && pClient->isConnected()) {
        pClient->disconnect();
    }
    connected = false;
    pWriteCharacteristic = nullptr;
}

void ThermalPrinterBLE::writeString(const String &str) {
    if(isConnected() && pWriteCharacteristic) {
        pWriteCharacteristic->writeValue(str.c_str(), str.length());
        delay(10); // Small delay
    }
}

void ThermalPrinterBLE::writeRaw(const uint8_t *data, size_t length) {
    if(isConnected() && pWriteCharacteristic) {
        pWriteCharacteristic->writeValue(data, length, false);
        delay(10);
    }
}

// ESC/POS Commands
void ThermalPrinterBLE::setLeftAlign() {
    uint8_t cmd[] = {0x1B, 0x61, 0x00}; // Left alignment
    writeRaw(cmd, sizeof(cmd));
}

void ThermalPrinterBLE::setCenterAlign() {
    uint8_t cmd[] = {0x1B, 0x61, 0x01}; // Center alignment
    writeRaw(cmd, sizeof(cmd));
}

void ThermalPrinterBLE::setBold(bool enable) {
    uint8_t cmd[] = {0x1B, 0x45, enable ? (uint8_t)0x01 : (uint8_t)0x00}; // Bold
    writeRaw(cmd, sizeof(cmd));
}

void ThermalPrinterBLE::setNormalSize() {
    uint8_t cmd[] = {0x1D, 0x21, 0x00}; // Normal size
    writeRaw(cmd, sizeof(cmd));
}

void ThermalPrinterBLE::setDoubleSize() {
    uint8_t cmd[] = {0x1D, 0x21, 0x11}; // Double height & width
    writeRaw(cmd, sizeof(cmd));
}

// Print functions
void ThermalPrinterBLE::printLine(const String &text) {
    writeString(text + "\n");
}

void ThermalPrinterBLE::printCenter(const String &text) {
    setCenterAlign();
    writeString(text + "\n");
    setLeftAlign();
}

void ThermalPrinterBLE::printBold(const String &text) {
    setBold(true);
    writeString(text + "\n");
    setBold(false);
}

void ThermalPrinterBLE::feedLines(int lines) {
    uint8_t cmd[] = {0x1B, 0x64, (uint8_t)lines}; // Feed lines
    writeRaw(cmd, sizeof(cmd));
}

void ThermalPrinterBLE::cutPaper() {
    // Full cut
    uint8_t cmd[] = {0x1D, 0x56, 0x00};
    writeRaw(cmd, sizeof(cmd));
}

// CHANGED FROM bool TO void (to match old working code)
void ThermalPrinterBLE::printHealthReport(const HealthData &data) {
    if (!isConnected()) {
        Serial.println("Cannot print: Printer not connected");
        return;
    }
    
    Serial.println("Printing health report...");
    
    // Header
    setCenterAlign();
    setDoubleSize();
    printLine("HEALTH REPORT");
    setNormalSize();
    setLeftAlign();
    
    printLine("========================");
    feedLines(1);
    
    // Patient Info
    printBold("PATIENT INFO");
    printLine("Name: " + data.name);
    printLine("Age: " + data.age);
    printLine("Gender: " + data.gender);
    if (data.address.length() > 0) {
        printLine("Address: " + data.address);
    }
    printLine("Date: " + data.timestamp);
    feedLines(1);
    
    // Measurements
    printBold("MEASUREMENTS");
    printLine("----------------");
    
    if (data.height > 0) {
        printLine("Height: " + String(data.height, 1) + " cm");
    }
    
    if (data.weight > 0) {
        printLine("Weight: " + String(data.weight, 1) + " kg");
    }
    
    if (data.bmi > 0) {
        String bmiStr = String(data.bmi, 1);
        String status = "";
        if (data.bmi < 18.5) status = " (Underweight)";
        else if (data.bmi < 25) status = " (Normal)";
        else if (data.bmi < 30) status = " (Overweight)";
        else status = " (Obese)";
        printLine("BMI: " + bmiStr + status);
    }
    
    if (data.temperature > 0) {
        printLine("Temp: " + String(data.temperature, 1) + " °C");
    }
    
    if (data.heart_rate > 0) {
        printLine("Heart Rate: " + String(data.heart_rate) + " BPM");
    }
    
    if (data.bp_sys > 0 && data.bp_dia > 0) {
        printLine("BP: " + String(data.bp_sys) + "/" + String(data.bp_dia) + " mmHg");
    }
    
    feedLines(1);
    
    // Notes
    printBold("NOTES");
    printLine("----------------");
    printLine("This is a screening");
    printLine("report only. Please");
    printLine("consult a doctor for");
    printLine("proper diagnosis.");
    feedLines(1);
    
    // Footer
    setCenterAlign();
    printLine("Thank You!");
    printLine("Get well soon!");
    feedLines(2);
    
    // Cut paper
    cutPaper();
    
}