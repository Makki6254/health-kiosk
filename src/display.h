#ifndef DISPLAY_H
#define DISPLAY_H

#include <lvgl.h>
#include <Arduino_GFX_Library.h>
#include "TAMC_GT911.h"
#include <FS.h>
#include <SD.h>
#include <SPI.h>

// Configuration for Display and Touch
#define TOUCH_GT911_SCL 20
#define TOUCH_GT911_SDA 19
#define TOUCH_GT911_INT -1
#define TOUCH_GT911_RST 38
#define TOUCH_MAP_X1 800
#define TOUCH_MAP_X2 0
#define TOUCH_MAP_Y1 480
#define TOUCH_MAP_Y2 0

#define GFX_BL 2  // Backlight control

// Display rotation setting (0, 1, 2, 3)
#define DISPLAY_ROTATION 1  // 0=0°, 1=90°, 2=180°, 3=270°

// SD Card SPI pins (Based on your pinout)
#define SD_CS   10
#define SD_MOSI 11
#define SD_MISO 13
#define SD_SCK  12


#define TRIG_PIN 17
#define ECHO_PIN 18
// CSV File settings
#define DATA_FILENAME "/health_data.csv"
#define MAX_RECORDS 1000

// ==================== Display Objects ====================
extern TAMC_GT911 ts;
extern Arduino_ESP32RGBPanel rgbpanel;
extern Arduino_RGB_Display gfx;

// Display variables
extern uint32_t screenWidth;
extern uint32_t screenHeight;
extern uint32_t bufSize;

// Printer functions
bool initPrinter();
void printHealthReport();

// SD Card functions
bool initSDCard();
bool saveHealthData(const String& data);
String readHealthData();
bool deleteHealthData();
void updateReportPage();
void updateDisplay();
void update_welcome_printer_status();
void show_report();
void create_data_view_screen();
void addLog(const char* message);
#endif // DISPLAY_H