#ifndef PRINTER_H
#define PRINTER_H

#include <Arduino.h>
#include <NimBLEDevice.h>
#include "sensors.h"

// Common thermal printer BLE service UUIDs
#define PRINTER_SERVICE_UUID        "000018f0-0000-1000-8000-00805f9b34fb" // Printer Service
#define PRINTER_CHARACTERISTIC_UUID "00002af1-0000-1000-8000-00805f9b34fb" // Write Characteristic

// Common printer BLE names
const String PRINTER_NAMES[] = {
    "KPrinter_12a6_BLE",  // Your printer name - PUT THIS FIRST
    "MP-420B", "MP-420", "RPP-58", 
    "BTPrinter", "BlueTooth Printer",
    "XP-P220", "POS-58", "58mm-Printer",
    "GT01", "GT02", "BlueTooth"
};
const int PRINTER_NAME_COUNT = 12;

class ThermalPrinterBLE {
public:
    ThermalPrinterBLE();
    bool begin();
    bool connect();
    bool isConnected();
    void disconnect();
    
    // Print functions
    void printLine(const String &text);
    void printCenter(const String &text);
    void printBold(const String &text);
    void feedLines(int lines = 1);
    void cutPaper();
    
    // Report printing - CHANGE THIS TO void (like in old working code)
    void printHealthReport(const HealthData &data);  // CHANGED FROM bool to void
    
private:
    bool connected;
    String deviceName;
    NimBLEClient* pClient;
    NimBLERemoteCharacteristic* pWriteCharacteristic;
    
    void writeString(const String &str);
    void writeRaw(const uint8_t *data, size_t length);
    
    // ESC/POS commands
    void setLeftAlign();
    void setCenterAlign();
    void setBold(bool enable);
    void setNormalSize();
    void setDoubleSize();
};

extern ThermalPrinterBLE thermalPrinter;

#endif // PRINTER_H