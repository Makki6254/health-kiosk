#include <Arduino.h>
#include <HardwareSerial.h>
#include <lvgl.h>
#include <stdlib.h>
#include "display.h"
#include "sensors.h"
#include "printer.h"

/* ==================== HARDWARE ==================== */
TAMC_GT911 ts(TOUCH_GT911_SDA, TOUCH_GT911_SCL, TOUCH_GT911_INT, TOUCH_GT911_RST, 
              max(TOUCH_MAP_Y1, TOUCH_MAP_Y2), max(TOUCH_MAP_X1, TOUCH_MAP_X2));

Arduino_ESP32RGBPanel rgbpanel(
    41, 40, 39, 42, 14, 21, 47, 48, 45, 9, 46, 3, 8, 16, 1, 15, 7, 6, 5, 4,
    0, 20, 30, 16, 0, 22, 13, 10, true);

Arduino_RGB_Display gfx(800, 480, &rgbpanel, 0, true);

/* ==================== UART ==================== */
#define UART_RX_PIN 18
#define UART_TX_PIN 17
#define UART_BAUD 115200
HardwareSerial SerialUART(1);

// Packed struct – MUST match sensor hub!
#pragma pack(push, 1)
struct SensorData {
  float distance_cm;
  float height_cm;
  float temperature_c;
  float ambient_temp_c;
  uint16_t heart_rate;
  float weight_kg;
  float bmi;
  uint8_t sensor_status;
  uint32_t timestamp;
};
#pragma pack(pop)

SensorData sensorData;
bool dataReceived = false;
unsigned long lastDataTime = 0;
uint32_t packetCount = 0;

uint8_t uartBuffer[sizeof(SensorData) + 4];
uint8_t bufferIndex = 0;
bool receivingPacket = false;

/* ==================== PATIENT DATA ==================== */
HealthData healthData;

bool sdCardInitialized = false;
bool printerInitialized = false;
bool printerConnected = false;
bool printingInProgress = false;

// Printer status on welcome screen
lv_obj_t *printer_status_label = NULL;
lv_obj_t *printer_connect_btn = NULL;

// Measurement flags (5 sensors: 0=BP,1=Height,2=Weight,3=Temp,4=Pulse)
bool measurements_done[5] = {false, false, false, false, false};

// Results screen labels
lv_obj_t *results_name, *results_age, *results_gender, *results_addr;
lv_obj_t *results_bp, *results_height, *results_weight, *results_temp, *results_hr, *results_bmi, *results_bmi_cat;
lv_obj_t *scr_bp;
lv_obj_t *bp_sys_ta, *bp_dia_ta;

// Ultrasonic mounting height (for simulation fallback)
const float SENSOR_MOUNTING_HEIGHT = 250.0;

/* ==================== STREAMING ==================== */
bool receivingStream = false;
uint8_t streamBuffer[16];
uint8_t streamIndex = 0;
float latestStreamValue = 0;
int currentStreamSensor = 0;

// Live labels for each sensor screen
lv_obj_t* live_label_height = NULL;
lv_obj_t* live_label_weight = NULL;
lv_obj_t* live_label_temp = NULL;
lv_obj_t* live_label_pulse = NULL;

/* ==================== LVGL CALLBACKS ==================== */
uint32_t millis_cb(void) { return millis(); }

void my_disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    gfx.draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)px_map,
                           lv_area_get_width(area), lv_area_get_height(area));
    lv_disp_flush_ready(disp);
}

void my_touchpad_read(lv_indev_t *indev, lv_indev_data_t *data) {
    ts.read();
    if (ts.isTouched) {
        data->state = LV_INDEV_STATE_PRESSED;
        int32_t tx = ts.points[0].x, ty = ts.points[0].y;
        if (DISPLAY_ROTATION == 1) { int32_t tmp = tx; tx = ty; ty = 800 - tmp; }
        data->point.x = tx; data->point.y = ty;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

/* ==================== GLOBAL KEYBOARD ==================== */
lv_obj_t *kb = NULL;

static void kb_event_cb(lv_event_t * e) {
    if(lv_event_get_code(e) == LV_EVENT_READY || lv_event_get_code(e) == LV_EVENT_CANCEL) {
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }
}

static void ta_event_cb(lv_event_t * e) {
    if(lv_event_get_code(e) == LV_EVENT_CLICKED) {
        lv_keyboard_set_textarea(kb, (lv_obj_t *)lv_event_get_target(e));
        lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(kb);
    }
}

/* ==================== SCREEN GLOBALS ==================== */
lv_obj_t *scr_welcome;
lv_obj_t *scr_info;
lv_obj_t *scr_height;
lv_obj_t *scr_weight;
lv_obj_t *scr_temp;
lv_obj_t *scr_pulse;
lv_obj_t *scr_results;
lv_obj_t *scr_data_view;

// Patient info widgets
lv_obj_t *name_ta;
lv_obj_t *age_ta;
lv_obj_t *gender_dd;
lv_obj_t *address_ta;

/* ==================== COMMAND DEFINITIONS ==================== */
#define CMD_MEASURE   0x01
#define CMD_START_STREAM 0x05
#define CMD_STOP_STREAM  0x06

/* ==================== UART FUNCTIONS ==================== */
bool parseUARTPacket() {
  if (uartBuffer[0] != 0xAA || uartBuffer[sizeof(SensorData) + 2] != 0x55) return false;
  uint8_t checksum = 0;
  for (uint16_t i = 1; i <= sizeof(SensorData); i++) checksum ^= uartBuffer[i];
  if (checksum != uartBuffer[sizeof(SensorData) + 1]) return false;
  memcpy(&sensorData, &uartBuffer[1], sizeof(SensorData));
  Serial.printf("📥 Data: H=%.1f T=%.1f HR=%d W=%.1f BMI=%.1f ST=0x%02X\n",
                sensorData.height_cm, sensorData.temperature_c, sensorData.heart_rate,
                sensorData.weight_kg, sensorData.bmi, sensorData.sensor_status);
  return true;
}

void updateLiveLabel(int sensorType, float value) {
    lv_obj_t* target = NULL;
    switch (sensorType) {
        case 1: target = live_label_height; break;
        case 2: target = live_label_weight; break;
        case 3: target = live_label_temp; break;
        case 4: target = live_label_pulse; break;
    }
    if (target && lv_obj_is_valid(target)) {
        char buf[32];
        if (sensorType == 4)
            lv_label_set_text_fmt(target, "Live: %d BPM", (int)value);
        else {
            const char* unit = (sensorType==1?"cm":(sensorType==2?"kg":"°C"));
            dtostrf(value, 5, 1, buf);
            lv_label_set_text_fmt(target, "Live: %s %s", buf, unit);
        }
    }
}

void processUART() {
  while (SerialUART.available()) {
    uint8_t byte = SerialUART.read();
    if (byte == 0xAA) {
      receivingPacket = true;
      bufferIndex = 0;
      uartBuffer[bufferIndex++] = byte;
    } else if (receivingPacket) {
      uartBuffer[bufferIndex++] = byte;
      if (bufferIndex >= sizeof(SensorData) + 3) {
        receivingPacket = false;
        if (parseUARTPacket()) {
          dataReceived = true;
          lastDataTime = millis();
          packetCount++;
          healthData.height = sensorData.height_cm;
          healthData.temperature = sensorData.temperature_c;
          healthData.heart_rate = sensorData.heart_rate;
          healthData.weight = sensorData.weight_kg;
          healthData.bmi = sensorData.bmi;
          healthData.height_measured = (sensorData.sensor_status & 0x01) != 0;
          healthData.temp_measured    = (sensorData.sensor_status & 0x02) != 0;
          healthData.hr_measured      = (sensorData.sensor_status & 0x04) != 0;
          healthData.weight_measured  = (sensorData.sensor_status & 0x08) != 0;
        }
      }
    } else if (byte == 0xCC) {
      // Start of stream packet
      receivingStream = true;
      streamIndex = 0;
      streamBuffer[streamIndex++] = byte;
    } else if (receivingStream) {
      streamBuffer[streamIndex++] = byte;
      if (streamIndex >= 12) { // 1+1+4+4+1+1 = 12 bytes
        receivingStream = false;
        uint8_t checksum = 0;
        for (int i = 1; i < 10; i++) checksum ^= streamBuffer[i];
        if (checksum == streamBuffer[10] && streamBuffer[11] == 0x55) {
          uint8_t sensorType = streamBuffer[1];
          float val;
          memcpy(&val, &streamBuffer[2], 4);
          latestStreamValue = val;
          currentStreamSensor = sensorType;
          updateLiveLabel(sensorType, val);
        }
      }
    }
  }
}

void sendMeasureCommand() {
  uint8_t cmd = CMD_MEASURE;
  SerialUART.write(cmd);
  Serial.println("📤 Sent CMD_MEASURE");
}

void sendStartStreamCommand(int sensorType) {
  uint8_t cmd[2] = {CMD_START_STREAM, (uint8_t)sensorType};
  SerialUART.write(cmd, 2);
  Serial.printf("📤 Sent START_STREAM for sensor %d\n", sensorType);
}

void sendStopStreamCommand() {
  uint8_t cmd = CMD_STOP_STREAM;
  SerialUART.write(&cmd, 1);
  Serial.println("📤 Sent STOP_STREAM");
}

/* ==================== NAVIGATION ==================== */
void switch_scr(lv_obj_t *new_scr) {
    lv_screen_load_anim(new_scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
}

/* ==================== BMI UTILITIES ==================== */
String getBMICategory(float bmi) {
  if (bmi < 18.5) return "Underweight";
  if (bmi < 25)   return "Normal";
  if (bmi < 30)   return "Overweight";
  return "Obese";
}

lv_color_t getBMIColor(float bmi) {
  if (bmi < 18.5) return lv_color_hex(0x3B82F6);
  if (bmi < 25)   return lv_color_hex(0x10B981);
  if (bmi < 30)   return lv_color_hex(0xF59E0B);
  return lv_color_hex(0xEF4444);
}

/* ==================== CREATE SENSOR SCREEN (generic) ==================== */
lv_obj_t* create_sensor_scr(const char* title, const char* icon, const char* instr,
                            lv_obj_t* next_scr, int sensorType) {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0F172A), 0);

    // Title
    lv_obj_t* h = lv_label_create(scr);
    lv_label_set_text(h, title);
    lv_obj_set_style_text_font(h, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(h, lv_color_hex(0x3B82F6), 0);
    lv_obj_align(h, LV_ALIGN_TOP_MID, 0, 40);

    // Icon
    lv_obj_t* icon_label = lv_label_create(scr);
    lv_label_set_text(icon_label, icon);
    lv_obj_set_style_text_font(icon_label, &lv_font_montserrat_14, 0);
    lv_obj_align(icon_label, LV_ALIGN_CENTER, 0, -50);

    // Instruction box
    lv_obj_t* box = lv_obj_create(scr);
    lv_obj_set_size(box, 600, 120);
    lv_obj_align(box, LV_ALIGN_CENTER, 0, 30);
    lv_obj_set_style_bg_color(box, lv_color_hex(0xFFFFFF), 0);
    lv_obj_t* i = lv_label_create(box);
    lv_label_set_text(i, instr);
    lv_obj_set_style_text_font(i, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_align(i, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(i);

    // Result label (final value)
    lv_obj_t* result_label = lv_label_create(scr);
    lv_label_set_text(result_label, "Ready for measurement");
    lv_obj_set_style_text_font(result_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(result_label, lv_color_hex(0x94A3B8), 0);
    lv_obj_align(result_label, LV_ALIGN_CENTER, 0, -120);

    // Live label
    lv_obj_t* live_label = lv_label_create(scr);
    lv_label_set_text(live_label, "Live: --");
    lv_obj_set_style_text_font(live_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(live_label, lv_color_hex(0xF59E0B), 0);
    lv_obj_align(live_label, LV_ALIGN_CENTER, 0, -80);

    // Progress bar (optional, kept for compatibility)
    lv_obj_t* pb = lv_bar_create(scr);
    lv_obj_set_size(pb, 400, 25);
    lv_obj_align(pb, LV_ALIGN_CENTER, 0, 120);
    lv_bar_set_value(pb, 0, LV_ANIM_OFF);
    lv_obj_add_flag(pb, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* progress_text = lv_label_create(scr);
    lv_label_set_text(progress_text, "0%");
    lv_obj_set_style_text_font(progress_text, &lv_font_montserrat_24, 0);
    lv_obj_align_to(progress_text, pb, LV_ALIGN_OUT_TOP_MID, 0, -10);
    lv_obj_add_flag(progress_text, LV_OBJ_FLAG_HIDDEN);

    // Start button
    lv_obj_t* start_btn = lv_btn_create(scr);
    lv_obj_set_size(start_btn, 250, 60);
    lv_obj_align(start_btn, LV_ALIGN_BOTTOM_MID, 0, -110);
    lv_obj_set_style_bg_color(start_btn, lv_color_hex(0x10B981), 0);
    lv_obj_t* start_lbl = lv_label_create(start_btn);
    lv_label_set_text(start_lbl, "START");
    lv_obj_set_style_text_font(start_lbl, &lv_font_montserrat_18, 0);
    lv_obj_center(start_lbl);

    // Capture button (initially hidden)
    lv_obj_t* capture_btn = lv_btn_create(scr);
    lv_obj_set_size(capture_btn, 200, 60);
    lv_obj_align(capture_btn, LV_ALIGN_BOTTOM_MID, 0, -40);
    lv_obj_set_style_bg_color(capture_btn, lv_color_hex(0x8B5CF6), 0);
    lv_obj_t* capture_lbl = lv_label_create(capture_btn);
    lv_label_set_text(capture_lbl, "CAPTURE");
    lv_obj_set_style_text_font(capture_lbl, &lv_font_montserrat_20,0);
    lv_obj_center(capture_lbl);
    lv_obj_add_flag(capture_btn, LV_OBJ_FLAG_HIDDEN);

    // Store live label globally
    if (sensorType == 1) live_label_height = live_label;
    else if (sensorType == 2) live_label_weight = live_label;
    else if (sensorType == 3) live_label_temp = live_label;
    else if (sensorType == 4) live_label_pulse = live_label;

    struct SensorScreenData {
        int sensorType;
        lv_obj_t* resultLabel;
        lv_obj_t* liveLabel;
        lv_obj_t* progressBar;
        lv_obj_t* progressText;
        lv_obj_t* nextScreen;
        lv_obj_t* startButton;
        lv_obj_t* captureButton;
        bool* measurementFlag;
    };

    SensorScreenData* data = new SensorScreenData{
        sensorType,
        result_label,
        live_label,
        pb,
        progress_text,
        next_scr,
        start_btn,
        capture_btn,
        &measurements_done[sensorType]
    };

    // Start button event
    lv_obj_add_event_cb(start_btn, [](lv_event_t* e) {
        auto* d = (SensorScreenData*)lv_event_get_user_data(e);
if (*(d->measurementFlag)) {
    // Already measured: go to next screen
    if (d->nextScreen == scr_results) {
        show_report();   // updates and loads results
    } else {
        switch_scr(d->nextScreen);
    }
    return;
}
        // Disable start button, show capture
        lv_obj_add_state(d->startButton, LV_STATE_DISABLED);
        lv_obj_clear_flag(d->captureButton, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(d->resultLabel, "Position yourself...");
        lv_obj_set_style_text_color(d->resultLabel, lv_color_hex(0xF59E0B), 0);
        // Send start stream command
        sendStartStreamCommand(d->sensorType);
    }, LV_EVENT_CLICKED, data);

    // Capture button event
    lv_obj_add_event_cb(capture_btn, [](lv_event_t* e) {
        auto* d = (SensorScreenData*)lv_event_get_user_data(e);
        // Send stop stream command
        sendStopStreamCommand();
        // Store the latest value
        float captured = latestStreamValue;
        if (captured <= 0) captured = 0; // fallback
        switch (d->sensorType) {
            case 1: healthData.height = captured; healthData.height_measured = true; break;
            case 2: healthData.weight = captured; healthData.weight_measured = true; if (healthData.height > 0) {
        healthData.bmi = calculateBMI(healthData.weight, healthData.height);
    } break;
            case 3: healthData.temperature = captured; healthData.temp_measured = true; break;
            case 4: healthData.heart_rate = (int)captured; healthData.hr_measured = true; break;
        }
        // Update result label
        char buf[32];
        if (captured > 0) {
            if (d->sensorType == 4)
                lv_label_set_text_fmt(d->resultLabel, "Heart Rate: %d BPM", (int)captured);
            else {
                const char* unit = (d->sensorType==1?"cm":(d->sensorType==2?"kg":"°C"));
                dtostrf(captured, 5, 1, buf);
                lv_label_set_text_fmt(d->resultLabel, "%s: %s %s",
                    (d->sensorType==1?"Height":(d->sensorType==2?"Weight":"Temp")), buf, unit);
            }
            lv_obj_set_style_text_color(d->resultLabel, lv_color_hex(0x10B981), 0);
        } else {
            lv_label_set_text(d->resultLabel, "No reading, try again");
            lv_obj_set_style_text_color(d->resultLabel, lv_color_hex(0xEF4444), 0);
        }
        // Hide capture, show continue on start button
        lv_obj_add_flag(d->captureButton, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_state(d->startButton, LV_STATE_DISABLED);
        lv_label_set_text(lv_obj_get_child(d->startButton, 0), "CONTINUE");
        *(d->measurementFlag) = true;
    }, LV_EVENT_CLICKED, data);

    return scr;
}

/* ==================== WELCOME SCREEN ==================== */
void create_welcome_screen() {
    scr_welcome = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_welcome, lv_color_hex(0x0F172A), 0);

    // Title
    lv_obj_t *t = lv_label_create(scr_welcome);
    lv_label_set_text(t, "SMART HEALTH KIOSK");
    lv_obj_set_style_text_font(t, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(t, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(t, LV_ALIGN_CENTER, 0, -120);

    // SD Card status
    lv_obj_t *sd_status = lv_label_create(scr_welcome);
    if (sdCardInitialized) {
        lv_label_set_text(sd_status, "SD Card: Ready");
        lv_obj_set_style_text_color(sd_status, lv_color_hex(0x10B981), 0);
    } else {
        lv_label_set_text(sd_status, "SD Card: Not Found");
        lv_obj_set_style_text_color(sd_status, lv_color_hex(0xEF4444), 0);
    }
    lv_obj_set_style_text_font(sd_status, &lv_font_montserrat_18, 0);
    lv_obj_align(sd_status, LV_ALIGN_CENTER, 0, -60);

    // Printer status + connect button
    lv_obj_t *printer_row = lv_obj_create(scr_welcome);
    lv_obj_set_size(printer_row, 400, 50);
    lv_obj_align(printer_row, LV_ALIGN_CENTER, 0, -10);
    lv_obj_set_flex_flow(printer_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(printer_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(printer_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(printer_row, 0, 0);

    printer_status_label = lv_label_create(printer_row);
    lv_label_set_text(printer_status_label, "Printer:");
    lv_obj_set_style_text_font(printer_status_label, &lv_font_montserrat_18, 0);

    printer_connect_btn = lv_btn_create(printer_row);
    lv_obj_set_size(printer_connect_btn, 100, 40);
    lv_obj_set_style_bg_color(printer_connect_btn, lv_color_hex(0x3B82F6), 0);
    lv_obj_t *btn_lbl = lv_label_create(printer_connect_btn);
    lv_label_set_text(btn_lbl, "CONNECT");
    lv_obj_set_style_text_font(btn_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(btn_lbl);
    lv_obj_add_event_cb(printer_connect_btn, [](lv_event_t*) {
        if (!printerInitialized) {
            lv_label_set_text(printer_status_label, "BLE not ready!");
            lv_obj_set_style_text_color(printer_status_label, lv_color_hex(0xEF4444), 0);
            return;
        }
        lv_label_set_text(printer_status_label, "Connecting...");
        lv_obj_set_style_text_color(printer_status_label, lv_color_hex(0xF59E0B), 0);
        lv_task_handler();
        if (thermalPrinter.connect()) {
            printerConnected = true;
        } else {
            printerConnected = false;
        }
        update_welcome_printer_status();
    }, LV_EVENT_CLICKED, NULL);

    // Start new checkup button
    lv_obj_t *b = lv_btn_create(scr_welcome);
    lv_obj_set_size(b, 250, 70);
    lv_obj_align(b, LV_ALIGN_CENTER, 0, 70);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x10B981), 0);
    lv_obj_t *btn_label = lv_label_create(b);
    lv_label_set_text(btn_label, "START NEW CHECKUP");
    lv_obj_set_style_text_font(btn_label, &lv_font_montserrat_18, 0);
    lv_obj_center(btn_label);
    lv_obj_add_event_cb(b, [](lv_event_t*) {
        for (int i = 0; i < 5; i++) measurements_done[i] = false;
        switch_scr(scr_info);
    }, LV_EVENT_CLICKED, NULL);

    // View saved data button
    lv_obj_t *data_btn = lv_btn_create(scr_welcome);
    lv_obj_set_size(data_btn, 250, 70);
    lv_obj_align(data_btn, LV_ALIGN_CENTER, 0, 160);
    lv_obj_set_style_bg_color(data_btn, lv_color_hex(0x3B82F6), 0);
    lv_obj_t *data_label = lv_label_create(data_btn);
    lv_label_set_text(data_label, "VIEW HEALTH DATA");
    lv_obj_set_style_text_font(data_label, &lv_font_montserrat_18, 0);
    lv_obj_center(data_label);
    lv_obj_add_event_cb(data_btn, [](lv_event_t*) {
        create_data_view_screen();
        switch_scr(scr_data_view);
    }, LV_EVENT_CLICKED, NULL);

    // Initial update
    update_welcome_printer_status();
}

void update_results_screen() {
    if (!scr_results) return;

    // --- SET FONT SIZE TO 20 FOR ALL LABELS ---
    const lv_font_t * font20 = &lv_font_montserrat_20;
    lv_obj_set_style_text_font(results_name, font20, 0);
    lv_obj_set_style_text_font(results_age, font20, 0);
    lv_obj_set_style_text_font(results_gender, font20, 0);
    lv_obj_set_style_text_font(results_addr, font20, 0);
    lv_obj_set_style_text_font(results_bp, font20, 0);
    lv_obj_set_style_text_font(results_height, font20, 0);
    lv_obj_set_style_text_font(results_weight, font20, 0);
    lv_obj_set_style_text_font(results_temp, font20, 0);
    lv_obj_set_style_text_font(results_hr, font20, 0);
    lv_obj_set_style_text_font(results_bmi, font20, 0);
    lv_obj_set_style_text_font(results_bmi_cat, font20, 0);
    // ------------------------------------------

    // Patient info
    lv_label_set_text_fmt(results_name, "Name: %s", healthData.name.c_str());
    lv_label_set_text_fmt(results_age, "Age: %s", healthData.age.c_str());
    lv_label_set_text_fmt(results_gender, "Gender: %s", healthData.gender.c_str());
    lv_label_set_text_fmt(results_addr, "Address: %s", healthData.address.c_str());

    char buf[32];

    // BP
    if (healthData.bp_measured)
        lv_label_set_text_fmt(results_bp, "BP: %d/%d mmHg", healthData.bp_sys, healthData.bp_dia);
    else
        lv_label_set_text(results_bp, "BP: Not measured");

    // Height
    if (healthData.height_measured) {
        dtostrf(healthData.height, 5, 1, buf);
        lv_label_set_text_fmt(results_height, "Height: %s cm", buf);
    } else {
        lv_label_set_text(results_height, "Height: Not measured");
    }

    // Weight
    if (healthData.weight_measured) {
        dtostrf(healthData.weight, 5, 1, buf);
        lv_label_set_text_fmt(results_weight, "Weight: %s kg", buf);
    } else {
        lv_label_set_text(results_weight, "Weight: Not measured");
    }

    // Temperature
    if (healthData.temp_measured) {
        dtostrf(healthData.temperature, 4, 1, buf);
        lv_label_set_text_fmt(results_temp, "Temp: %s °C", buf);
    } else {
        lv_label_set_text(results_temp, "Temp: Not measured");
    }

    // Heart rate
    if (healthData.hr_measured) {
        lv_label_set_text_fmt(results_hr, "HR: %d BPM", healthData.heart_rate);
    } else {
        lv_label_set_text(results_hr, "HR: Not measured");
    }

    // BMI
    if (healthData.bmi > 0) {
        dtostrf(healthData.bmi, 4, 1, buf);
        lv_label_set_text_fmt(results_bmi, "BMI: %s", buf);
        String cat = getBMICategory(healthData.bmi);
        lv_label_set_text_fmt(results_bmi_cat, "Category: %s", cat.c_str());
        lv_obj_set_style_text_color(results_bmi_cat, getBMIColor(healthData.bmi), 0);
    } else {
        lv_label_set_text(results_bmi, "BMI: --");
        lv_label_set_text(results_bmi_cat, "Category: --");
    }
}

/* ==================== PATIENT INFO SCREEN ==================== */
void create_info_screen() {
    scr_info = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_info, lv_color_hex(0x0F172A), 0);

    lv_obj_t* title = lv_label_create(scr_info);
    lv_label_set_text(title, "PATIENT INFORMATION");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x3B82F6), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);

    lv_obj_t *form = lv_obj_create(scr_info);
    lv_obj_set_size(form, 470, 450);
    lv_obj_align(form, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_flex_flow(form, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(form, 20, 0);
    lv_obj_set_style_pad_gap(form, 15, 0);

    // Name
    lv_obj_t *name_label = lv_label_create(form);
    lv_label_set_text(name_label, "Full Name:");
    lv_obj_set_style_text_font(name_label, &lv_font_montserrat_18, 0);
    name_ta = lv_textarea_create(form);
    lv_obj_set_width(name_ta, LV_PCT(100));
    lv_obj_set_height(name_ta, 50);
    lv_textarea_set_placeholder_text(name_ta, "Enter full name");
    lv_obj_set_style_text_font(name_ta, &lv_font_montserrat_18, 0);
    lv_obj_add_event_cb(name_ta, ta_event_cb, LV_EVENT_CLICKED, NULL);

    // Age
    lv_obj_t *age_label = lv_label_create(form);
    lv_label_set_text(age_label, "Age:");
    lv_obj_set_style_text_font(age_label, &lv_font_montserrat_18, 0);
    age_ta = lv_textarea_create(form);
    lv_obj_set_width(age_ta, LV_PCT(100));
    lv_obj_set_height(age_ta, 50);
    lv_textarea_set_placeholder_text(age_ta, "Enter age");
    lv_obj_set_style_text_font(age_ta, &lv_font_montserrat_18, 0);
    lv_obj_add_event_cb(age_ta, ta_event_cb, LV_EVENT_CLICKED, NULL);

    // Gender
    lv_obj_t *gender_label = lv_label_create(form);
    lv_label_set_text(gender_label, "Gender:");
    lv_obj_set_style_text_font(gender_label, &lv_font_montserrat_18, 0);
    gender_dd = lv_dropdown_create(form);
    lv_obj_set_width(gender_dd, LV_PCT(100));
    lv_obj_set_height(gender_dd, 50);
    lv_dropdown_set_options(gender_dd, "Male\nFemale\nOther\nPrefer not to say");
    lv_obj_set_style_text_font(gender_dd, &lv_font_montserrat_18, 0);

    // Address
    lv_obj_t *address_label = lv_label_create(form);
    lv_label_set_text(address_label, "Address:");
    lv_obj_set_style_text_font(address_label, &lv_font_montserrat_18, 0);
    address_ta = lv_textarea_create(form);
    lv_obj_set_width(address_ta, LV_PCT(100));
    lv_obj_set_height(address_ta, 80);
    lv_textarea_set_placeholder_text(address_ta, "Enter address");
    lv_obj_set_style_text_font(address_ta, &lv_font_montserrat_18, 0);
    lv_obj_add_event_cb(address_ta, ta_event_cb, LV_EVENT_CLICKED, NULL);

    // Next button
    lv_obj_t *n = lv_btn_create(scr_info);
    lv_obj_set_size(n, 150, 60);
    lv_obj_align(n, LV_ALIGN_BOTTOM_RIGHT, -30, -20);
    lv_obj_set_style_bg_color(n, lv_color_hex(0x10B981), 0);
    lv_obj_t *btn_lbl = lv_label_create(n);
    lv_label_set_text(btn_lbl, "NEXT →");
    lv_obj_set_style_text_font(btn_lbl, &lv_font_montserrat_18, 0);
    lv_obj_center(btn_lbl);

    lv_obj_add_event_cb(n, [](lv_event_t*) {
        // Save patient info
        healthData.name = lv_textarea_get_text(name_ta);
        healthData.age = lv_textarea_get_text(age_ta);
        char gender[20];
        lv_dropdown_get_selected_str(gender_dd, gender, sizeof(gender));
        healthData.gender = String(gender);
        healthData.address = lv_textarea_get_text(address_ta);
        // Reset all sensor data
        healthData.resetMeasurements();
        for (int i = 0; i < 5; i++) measurements_done[i] = false;
        switch_scr(scr_bp);
    }, LV_EVENT_CLICKED, NULL);
}

/* ==================== BLOOD PRESSURE SCREEN ==================== */
void create_bp_screen() {
    scr_bp = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_bp, lv_color_hex(0x0F172A), 0);

    // Title
    lv_obj_t *title = lv_label_create(scr_bp);
    lv_label_set_text(title, "BLOOD PRESSURE");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x3B82F6), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);

    // Instruction
    lv_obj_t *instr = lv_label_create(scr_bp);
    lv_label_set_text(instr, "Enter your blood pressure manually");
    lv_obj_set_style_text_font(instr, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(instr, lv_color_hex(0x94A3B8), 0);
    lv_obj_align(instr, LV_ALIGN_TOP_MID, 0, 80);

    // Container
    lv_obj_t *box = lv_obj_create(scr_bp);
    lv_obj_set_size(box, 400, 200);
    lv_obj_align(box, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(box, lv_color_hex(0x1E293B), 0);
    lv_obj_set_style_border_width(box, 0, 0);

    // Systolic
    lv_obj_t *sys_lbl = lv_label_create(box);
    lv_label_set_text(sys_lbl, "Systolic (mmHg):");
    lv_obj_set_style_text_font(sys_lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(sys_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_pos(sys_lbl, 20, 30);

    bp_sys_ta = lv_textarea_create(box);
    lv_obj_set_size(bp_sys_ta, 150, 40);
    lv_obj_set_pos(bp_sys_ta, 200, 25);
    lv_textarea_set_one_line(bp_sys_ta, true);
    lv_textarea_set_placeholder_text(bp_sys_ta, "e.g. 120");
    lv_obj_add_event_cb(bp_sys_ta, ta_event_cb, LV_EVENT_CLICKED, NULL);

    // Diastolic
    lv_obj_t *dia_lbl = lv_label_create(box);
    lv_label_set_text(dia_lbl, "Diastolic (mmHg):");
    lv_obj_set_style_text_font(dia_lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(dia_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_pos(dia_lbl, 20, 90);

    bp_dia_ta = lv_textarea_create(box);
    lv_obj_set_size(bp_dia_ta, 150, 40);
    lv_obj_set_pos(bp_dia_ta, 200, 85);
    lv_textarea_set_one_line(bp_dia_ta, true);
    lv_textarea_set_placeholder_text(bp_dia_ta, "e.g. 80");
    lv_obj_add_event_cb(bp_dia_ta, ta_event_cb, LV_EVENT_CLICKED, NULL);

    // Save & Continue button
    lv_obj_t *btn_save = lv_btn_create(scr_bp);
    lv_obj_set_size(btn_save, 200, 60);
    lv_obj_align(btn_save, LV_ALIGN_BOTTOM_MID, 0, -50);
    lv_obj_set_style_bg_color(btn_save, lv_color_hex(0x10B981), 0);
    lv_obj_t *btn_lbl = lv_label_create(btn_save);
    lv_label_set_text(btn_lbl, "SAVE & CONTINUE");
    lv_obj_set_style_text_font(btn_lbl, &lv_font_montserrat_16, 0);
    lv_obj_center(btn_lbl);

    lv_obj_add_event_cb(btn_save, [](lv_event_t*) {
        String sys_str = lv_textarea_get_text(bp_sys_ta);
        String dia_str = lv_textarea_get_text(bp_dia_ta);
        healthData.bp_sys = sys_str.toInt();
        healthData.bp_dia = dia_str.toInt();
        healthData.bp_measured = true;
        measurements_done[0] = true;
        switch_scr(scr_height);
    }, LV_EVENT_CLICKED, NULL);
}

/* ==================== RESULTS SCREEN ==================== */
void create_results_screen() {
    if (scr_results != NULL) lv_obj_del(scr_results);
    scr_results = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_results, lv_color_hex(0x0F172A), 0);

    // Title
    lv_obj_t *title = lv_label_create(scr_results);
    lv_label_set_text(title, "HEALTH CHECKUP REPORT");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x3B82F6), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    // Measurements container
    lv_obj_t *measurements = lv_obj_create(scr_results);
    lv_obj_set_size(measurements, 450, 400);
    lv_obj_align(measurements, LV_ALIGN_CENTER, 0, -10);
    lv_obj_set_style_bg_color(measurements, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(measurements, 0, 0);
    lv_obj_set_style_pad_all(measurements, 20, 0);

    int y = 0;

    // Create all result labels inside the container and store them globally
    results_name = lv_label_create(measurements);
    lv_label_set_text(results_name, "Name: --");
    lv_obj_set_pos(results_name, 0, y); y += 25;

    results_age = lv_label_create(measurements);
    lv_label_set_text(results_age, "Age: --");
    lv_obj_set_pos(results_age, 0, y); y += 25;

    results_gender = lv_label_create(measurements);
    lv_label_set_text(results_gender, "Gender: --");
    lv_obj_set_pos(results_gender, 0, y); y += 25;

    results_addr = lv_label_create(measurements);
    lv_label_set_text(results_addr, "Address: --");
    lv_obj_set_pos(results_addr, 0, y); y += 70;

    results_bp = lv_label_create(measurements);
    lv_label_set_text(results_bp, "BP: --");
    lv_obj_set_pos(results_bp, 0, y); y += 25;

    results_height = lv_label_create(measurements);
    lv_label_set_text(results_height, "Height: --");
    lv_obj_set_pos(results_height, 0, y); y += 25;

    results_weight = lv_label_create(measurements);
    lv_label_set_text(results_weight, "Weight: --");
    lv_obj_set_pos(results_weight, 0, y); y += 25;

    results_temp = lv_label_create(measurements);
    lv_label_set_text(results_temp, "Temp: --");
    lv_obj_set_pos(results_temp, 0, y); y += 25;

    results_hr = lv_label_create(measurements);
    lv_label_set_text(results_hr, "HR: --");
    lv_obj_set_pos(results_hr, 0, y); y += 25;

    results_bmi = lv_label_create(measurements);
    lv_label_set_text(results_bmi, "BMI: --");
    lv_obj_set_pos(results_bmi, 0, y); y += 25;

    results_bmi_cat = lv_label_create(measurements);
    lv_label_set_text(results_bmi_cat, "Category: --");
    lv_obj_set_pos(results_bmi_cat, 0, y);

    // Button row at bottom
    lv_obj_t *btn_row = lv_obj_create(scr_results);
    lv_obj_set_size(btn_row, 400, 80);
    lv_obj_align(btn_row, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_row, 0, 0);

    // Print button
    lv_obj_t *btn_print = lv_btn_create(btn_row);
    lv_obj_set_size(btn_print, 160, 60);
    lv_obj_set_style_bg_color(btn_print, lv_color_hex(0x8B5CF6), 0);
    lv_obj_t *print_lbl = lv_label_create(btn_print);
    lv_label_set_text(print_lbl, "PRINT");
    lv_obj_set_style_text_font(print_lbl, &lv_font_montserrat_18, 0);
    lv_obj_center(print_lbl);
    lv_obj_add_event_cb(btn_print, [](lv_event_t*) {
        if (!printerConnected) {
            lv_obj_t *msg = lv_obj_create(lv_layer_top());
            lv_obj_set_size(msg, 300, 80);
            lv_obj_center(msg);
            lv_obj_set_style_bg_color(msg, lv_color_hex(0xEF4444), 0);
            lv_obj_set_style_radius(msg, 10, 0);
            lv_obj_t *txt = lv_label_create(msg);
            lv_label_set_text(txt, "Printer not connected!");
            lv_obj_set_style_text_font(txt, &lv_font_montserrat_14, 0);
            lv_obj_center(txt);
            delay(2000);
            lv_obj_del(msg);
            return;
        }
        thermalPrinter.printHealthReport(healthData);
        lv_obj_t *msg = lv_obj_create(lv_layer_top());
        lv_obj_set_size(msg, 300, 80);
        lv_obj_center(msg);
        lv_obj_set_style_bg_color(msg, lv_color_hex(0x10B981), 0);
        lv_obj_set_style_radius(msg, 10, 0);
        lv_obj_t *txt = lv_label_create(msg);
        lv_label_set_text(txt, "✓ Report sent");
        lv_obj_set_style_text_font(txt, &lv_font_montserrat_14, 0);
        lv_obj_center(txt);
        delay(1500);
        lv_obj_del(msg);
    }, LV_EVENT_CLICKED, NULL);

    // Done button (saves and exits)
    lv_obj_t *btn_done = lv_btn_create(btn_row);
    lv_obj_set_size(btn_done, 160, 60);
    lv_obj_set_style_bg_color(btn_done, lv_color_hex(0x10B981), 0);
    lv_obj_t *done_lbl = lv_label_create(btn_done);
    lv_label_set_text(done_lbl, "DONE");
    lv_obj_set_style_text_font(done_lbl, &lv_font_montserrat_18, 0);
    lv_obj_center(done_lbl);
    lv_obj_add_event_cb(btn_done, [](lv_event_t*) {
        if (sdCardInitialized) {
            struct tm timeinfo;
            if (getLocalTime(&timeinfo)) {
                char ts[64];
                strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &timeinfo);
                healthData.timestamp = String(ts);
            } else {
                healthData.timestamp = String(millis()/1000);
            }
            if (saveHealthData(healthData.toCSV())) {
                lv_obj_t *msg = lv_obj_create(lv_layer_top());
                lv_obj_set_size(msg, 250, 60);
                lv_obj_center(msg);
                lv_obj_set_style_bg_color(msg, lv_color_hex(0x10B981), 0);
                lv_obj_set_style_radius(msg, 10, 0);
                lv_obj_t *txt = lv_label_create(msg);
                lv_label_set_text(txt, "Data saved");
                lv_obj_set_style_text_font(txt, &lv_font_montserrat_14, 0);
                lv_obj_center(txt);
                delay(1000);
                lv_obj_del(msg);
            }
        }
        healthData = HealthData();
        switch_scr(scr_welcome);
    }, LV_EVENT_CLICKED, NULL);
}

/* ==================== DATA VIEW SCREEN ==================== */
void create_data_view_screen() {
    scr_data_view = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_data_view, lv_color_hex(0x0F172A), 0);
    lv_obj_clear_flag(scr_data_view, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(scr_data_view);
    lv_label_set_text(title, "STORED HEALTH DATA");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x3B82F6), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);

    lv_obj_t *table_container = lv_obj_create(scr_data_view);
    lv_obj_set_size(table_container, 750, 350);
    lv_obj_align(table_container, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_border_width(table_container, 0, 0);
    lv_obj_set_style_bg_opa(table_container, LV_OPA_TRANSP, 0);

    String data = readHealthData();
    String lines[50];
    int lineCount = 0;
    int startIdx = 0;
    for (int i = 0; i < data.length(); i++) {
        if (data.charAt(i) == '\n') {
            lines[lineCount++] = data.substring(startIdx, i);
            startIdx = i + 1;
            if (lineCount >= 49) break;
        }
    }

    const char* headers[] = {"Timestamp", "Name", "Age", "Gender", "Address", "Weight", "Height", "Temp", "BMI", "HR", "BP Sys", "BP Dia"};
    const int colWidths[] = {120, 100, 50, 70, 150, 70, 70, 70, 70, 60, 80, 80};

    int yPos = 0;

    // Header row
    lv_obj_t *header_row = lv_obj_create(table_container);
    lv_obj_set_size(header_row, 730, 40);
    lv_obj_set_pos(header_row, 10, yPos);
    lv_obj_set_style_bg_color(header_row, lv_color_hex(0x1E293B), 0);
    lv_obj_set_style_border_width(header_row, 0, 0);

    int xPos = 0;
    for (int col = 0; col < 12; col++) {
        lv_obj_t *h = lv_label_create(header_row);
        lv_label_set_text(h, headers[col]);
        lv_obj_set_style_text_font(h, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(h, lv_color_hex(0x3B82F6), 0);
        lv_obj_set_size(h, colWidths[col], 40);
        lv_obj_set_pos(h, xPos, 0);
        xPos += colWidths[col];
    }

    yPos += 45;

    // Data rows (max 8)
    for (int row = 1; row < lineCount && row < 8; row++) {
        String fields[12];
        int fieldIdx = 0;
        int start = 0;
        String line = lines[row];
        for (int i = 0; i < line.length(); i++) {
            if (line.charAt(i) == ',') {
                fields[fieldIdx++] = line.substring(start, i);
                start = i + 1;
                if (fieldIdx >= 11) break;
            }
        }
        fields[fieldIdx] = line.substring(start);

        lv_obj_t *data_row = lv_obj_create(table_container);
        lv_obj_set_size(data_row, 730, 35);
        lv_obj_set_pos(data_row, 10, yPos);
        lv_obj_set_style_bg_color(data_row, row % 2 == 0 ? lv_color_hex(0x1E293B) : lv_color_hex(0x0F172A), 0);
        lv_obj_set_style_border_width(data_row, 0, 0);

        xPos = 0;
        for (int col = 0; col < 12; col++) {
            lv_obj_t *cell = lv_label_create(data_row);
            lv_label_set_text(cell, fields[col].c_str());
            lv_obj_set_style_text_font(cell, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(cell, lv_color_hex(0xE2E8F0), 0);
            lv_obj_set_size(cell, colWidths[col], 35);
            lv_obj_set_pos(cell, xPos, 0);
            xPos += colWidths[col];
        }
        yPos += 40;
    }

    // Button container
    lv_obj_t *btn_container = lv_obj_create(scr_data_view);
    lv_obj_set_size(btn_container, 500, 60);
    lv_obj_align(btn_container, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_set_flex_flow(btn_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_container, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(btn_container, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(btn_container, LV_OBJ_FLAG_SCROLLABLE);

    // Refresh
    lv_obj_t *btn_load = lv_btn_create(btn_container);
    lv_obj_set_size(btn_load, 150, 50);
    lv_obj_set_style_bg_color(btn_load, lv_color_hex(0x3B82F6), 0);
    lv_obj_clear_flag(btn_load, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *load_lbl = lv_label_create(btn_load);
    lv_label_set_text(load_lbl, "REFRESH");
    lv_obj_set_style_text_font(load_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(load_lbl);
    lv_obj_add_event_cb(btn_load, [](lv_event_t*) {
        lv_obj_del(scr_data_view);
        create_data_view_screen();
        lv_scr_load(scr_data_view);
    }, LV_EVENT_CLICKED, NULL);

    // Clear all
    lv_obj_t *btn_clear = lv_btn_create(btn_container);
    lv_obj_set_size(btn_clear, 150, 50);
    lv_obj_set_style_bg_color(btn_clear, lv_color_hex(0xEF4444), 0);
    lv_obj_clear_flag(btn_clear, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *clear_lbl = lv_label_create(btn_clear);
    lv_label_set_text(clear_lbl, "CLEAR ALL");
    lv_obj_set_style_text_font(clear_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(clear_lbl);
    lv_obj_add_event_cb(btn_clear, [](lv_event_t*) {
        if (deleteHealthData()) {
            lv_obj_t *msg = lv_obj_create(lv_layer_top());
            lv_obj_set_size(msg, 300, 80);
            lv_obj_center(msg);
            lv_obj_set_style_bg_color(msg, lv_color_hex(0x10B981), 0);
            lv_obj_set_style_radius(msg, 10, 0);
            lv_obj_t *txt = lv_label_create(msg);
            lv_label_set_text(txt, "✓ All data cleared!");
            lv_obj_set_style_text_font(txt, &lv_font_montserrat_14, 0);
            lv_obj_center(txt);
            delay(2000);
            lv_obj_del(msg);
            lv_obj_del(scr_data_view);
            create_data_view_screen();
            lv_scr_load(scr_data_view);
        }
    }, LV_EVENT_CLICKED, NULL);

    // Back to welcome
    lv_obj_t *btn_back = lv_btn_create(btn_container);
    lv_obj_set_size(btn_back, 150, 50);
    lv_obj_clear_flag(btn_back, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *back_lbl = lv_label_create(btn_back);
    lv_label_set_text(back_lbl, "BACK");
    lv_obj_set_style_text_font(back_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(back_lbl);
    lv_obj_add_event_cb(btn_back, [](lv_event_t*) { switch_scr(scr_welcome); }, LV_EVENT_CLICKED, NULL);
}

void update_welcome_printer_status() {
    if (!printer_status_label) return;
    if (printerConnected) {
        lv_label_set_text(printer_status_label, "Printer: Connected");
        lv_obj_set_style_text_color(printer_status_label, lv_color_hex(0x10B981), 0);
        if (printer_connect_btn) lv_obj_add_flag(printer_connect_btn, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text(printer_status_label, "Printer: Disconnected");
        lv_obj_set_style_text_color(printer_status_label, lv_color_hex(0xEF4444), 0);
        if (printer_connect_btn) lv_obj_clear_flag(printer_connect_btn, LV_OBJ_FLAG_HIDDEN);
    }
}

void show_report() {
    update_results_screen();
    lv_scr_load(scr_results);
}

/* ==================== SETUP ==================== */
void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("==================================");
    Serial.println("   SMART HEALTH KIOSK (STREAMING)");
    Serial.println("==================================");

    gfx.begin();
    gfx.setRotation(DISPLAY_ROTATION);
    gfx.fillScreen(0x000000);
    pinMode(GFX_BL, OUTPUT);
    digitalWrite(GFX_BL, HIGH);

    ts.begin();
    ts.setRotation(DISPLAY_ROTATION);

    SerialUART.begin(UART_BAUD, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
    Serial.println("✓ UART ready (RX=18, TX=17)");

    sdCardInitialized = initSDCard();

    printerInitialized = thermalPrinter.begin();
    if (printerInitialized) {
        Serial.println("✓ Printer BLE initialized");
    } else {
        Serial.println("✗ Printer BLE init failed");
    }

    lv_init();
    lv_tick_set_cb(millis_cb);

    static lv_color_t buf[480 * 40];
    lv_display_t *disp = lv_display_create(480, 800);
    lv_display_set_flush_cb(disp, my_disp_flush);
    lv_display_set_buffers(disp, buf, NULL, sizeof(buf), LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, my_touchpad_read);

    kb = lv_keyboard_create(lv_layer_sys());
    lv_obj_set_size(kb, 480, 240);
    lv_obj_set_style_text_font(kb, &lv_font_montserrat_14, 0);
    lv_obj_add_event_cb(kb, kb_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);

    // Create screens in correct order
    create_welcome_screen();
    create_info_screen();
    create_results_screen();
    create_bp_screen();

    scr_pulse  = create_sensor_scr("PULSE RATE", "❤️", "Place finger on sensor", scr_results, 4);
    scr_temp   = create_sensor_scr("TEMPERATURE", "🌡️", "Look at thermal sensor from 5cm away", scr_pulse, 3);
    scr_weight = create_sensor_scr("WEIGHT SCALE", "⚖️", "Step onto scale platform", scr_temp, 2);
    scr_height = create_sensor_scr("HEIGHT SENSOR", "📏", "Stand straight under sensor", scr_weight, 1);

    create_results_screen();
    lv_scr_load(scr_welcome);
    Serial.println("System ready.");
}

/* ==================== LOOP ==================== */
void loop() {
    lv_task_handler();
    processUART();

    static unsigned long lastPrinterCheck = 0;
    if (millis() - lastPrinterCheck > 2000) {
        if (printerInitialized) {
            bool wasConnected = printerConnected;
            printerConnected = thermalPrinter.isConnected();
            if (wasConnected != printerConnected && lv_scr_act() == scr_welcome) {
                update_welcome_printer_status();
            }
        }
        lastPrinterCheck = millis();
    }

    delay(5);
}