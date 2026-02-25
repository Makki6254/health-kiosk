#include "display.h"
#include "sensors.h"
#include "printer.h"  // Add this line
#include <lvgl.h>
#include <Arduino.h>

// ==================== 1. Hardware & Global State ====================
TAMC_GT911 ts(TOUCH_GT911_SDA, TOUCH_GT911_SCL, TOUCH_GT911_INT, TOUCH_GT911_RST, 
              max(TOUCH_MAP_X1, TOUCH_MAP_X2), max(TOUCH_MAP_Y1, TOUCH_MAP_Y2));

Arduino_ESP32RGBPanel rgbpanel(
    41, 40, 39, 42, 14, 21, 47, 48, 45, 9, 46, 3, 8, 16, 1, 15, 7, 6, 5, 4,
    0, 20, 30, 16, 0, 22, 13, 10, true);

Arduino_RGB_Display gfx(800, 480, &rgbpanel, 0, true);

uint32_t screenWidth, screenHeight;
lv_obj_t *kb = NULL;

// Screen Objects
lv_obj_t *scr_welcome, *scr_info, *scr_bp, *scr_height, *scr_weight, *scr_temp, *scr_pulse, *scr_results;
lv_obj_t *scr_data_view;
lv_obj_t *name_ta, *age_ta, *gender_dd, *address_ta;

// Global health data
HealthData healthData;

// SD Card status
bool sdCardInitialized = false;
bool printerInitialized = false;  // Add this line

// Measurement flags
bool measurements_done[5] = {false, false, false, false, false};

// Ultrasonic sensor mounting height (cm)
const float SENSOR_MOUNTING_HEIGHT = 250.0; // 2.5 meters

// ==================== FORWARD DECLARATIONS ====================
void create_welcome_screen();
void create_info_screen();
void create_results_screen();
void create_data_view_screen();
float measurePersonHeight();

// ==================== 2. LVGL Core Callbacks ====================
uint32_t millis_cb(void) { return millis(); }

void my_disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    gfx.draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)px_map, lv_area_get_width(area), lv_area_get_height(area));
    lv_disp_flush_ready(disp);
}

void my_touchpad_read(lv_indev_t *indev, lv_indev_data_t *data) {
    ts.read();
    if (ts.isTouched) {
        data->state = LV_INDEV_STATE_PRESSED;
        int32_t tx = ts.points[0].x, ty = ts.points[0].y;
        if (DISPLAY_ROTATION == 1) { int32_t tmp = tx; tx = ty; ty = 800 - tmp; }
        data->point.x = tx; data->point.y = ty;
    } else { data->state = LV_INDEV_STATE_RELEASED; }
}

static void kb_event_cb(lv_event_t * e) {
    if(lv_event_get_code(e) == LV_EVENT_READY || lv_event_get_code(e) == LV_EVENT_CANCEL) {
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
        lv_indev_reset(lv_indev_active(), NULL);
    }
}

static void ta_event_cb(lv_event_t * e) {
    if(lv_event_get_code(e) == LV_EVENT_CLICKED) {
        lv_keyboard_set_textarea(kb, (lv_obj_t *)lv_event_get_target(e));
        lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(kb); 
    }
}

// ==================== 3. Screen Factory & Navigation ====================
void switch_scr(lv_obj_t *new_scr) { 
    lv_screen_load_anim(new_scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false); 
}

// ==================== Ultrasonic Height Measurement Function ====================
float measurePersonHeight() {
    Serial.println("=== Measuring Height with Ultrasonic Sensor ===");
    
    // Take 5 samples and average for accuracy
    float totalDistance = 0;
    int validSamples = 0;
    
    for (int i = 0; i < 5; i++) {
        // Send trigger pulse
        digitalWrite(TRIG_PIN, LOW);
        delayMicroseconds(2);
        digitalWrite(TRIG_PIN, HIGH);
        delayMicroseconds(10);
        digitalWrite(TRIG_PIN, LOW);
        
        // Measure echo pulse
        unsigned long duration = pulseIn(ECHO_PIN, HIGH, 30000); // 30ms timeout
        
        if (duration > 0) {
            // Convert to distance in cm
            float distance = duration * 0.0343 / 2.0;
            
            // Validate distance (JSN-SR04T range: 25-450cm)
            if (distance >= 25.0 && distance <= 450.0) {
                totalDistance += distance;
                validSamples++;
                Serial.printf("Sample %d: %.1f cm\n", i+1, distance);
            } else {
                Serial.printf("Sample %d: Invalid distance %.1f cm\n", i+1, distance);
            }
        } else {
            Serial.printf("Sample %d: No echo received\n", i+1);
        }
        
        delay(100); // Wait between measurements
    }
    
    if (validSamples == 0) {
        Serial.println("ERROR: No valid height measurements!");
        return 0.0;
    }
    
    float avgDistance = totalDistance / validSamples;
    float personHeight = SENSOR_MOUNTING_HEIGHT - avgDistance;
    
    Serial.printf("Average distance: %.1f cm\n", avgDistance);
    Serial.printf("Person height: %.1f cm\n", personHeight);
    
    // Validate reasonable human height
    if (personHeight < 50.0 || personHeight > 250.0) {
        Serial.printf("ERROR: Height %.1f cm is outside reasonable range!\n", personHeight);
        return 0.0;
    }
    
    return personHeight;
}

lv_obj_t* create_sensor_scr(const char* title, const char* icon, const char* instr, 
                            lv_obj_t* next_scr, int sensorType) {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0F172A), 0);
    
    // Title
    lv_obj_t* h = lv_label_create(scr);
    lv_label_set_text(h, title);
    lv_obj_set_style_text_font(h, &lv_font_montserrat_14, 0);
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
    lv_obj_set_style_bg_color(box, lv_color_hex(0x1E293B), 0);
    lv_obj_t* i = lv_label_create(box);
    lv_label_set_text(i, instr);
    lv_obj_set_style_text_font(i, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(i, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(i);

    // Result label
    lv_obj_t* result_label = lv_label_create(scr);
    lv_label_set_text(result_label, "Ready for measurement");
    lv_obj_set_style_text_font(result_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(result_label, lv_color_hex(0x94A3B8), 0);
    lv_obj_align(result_label, LV_ALIGN_CENTER, 0, -120);

    // Progress bar
    lv_obj_t* pb = lv_bar_create(scr);
    lv_obj_set_size(pb, 400, 25);
    lv_obj_align(pb, LV_ALIGN_CENTER, 0, 120);
    lv_bar_set_value(pb, 0, LV_ANIM_OFF);
    lv_obj_add_flag(pb, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* progress_text = lv_label_create(scr);
    lv_label_set_text(progress_text, "0%");
    lv_obj_set_style_text_font(progress_text, &lv_font_montserrat_14, 0);
    lv_obj_align_to(progress_text, pb, LV_ALIGN_OUT_TOP_MID, 0, -10);
    lv_obj_add_flag(progress_text, LV_OBJ_FLAG_HIDDEN);

    // Start button
    lv_obj_t* b = lv_btn_create(scr);
    lv_obj_set_size(b, 250, 60);
    lv_obj_align(b, LV_ALIGN_BOTTOM_MID, 0, -40);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x10B981), 0);
    lv_obj_t* lbl = lv_label_create(b);
    lv_label_set_text(lbl, "START MEASUREMENT");
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(lbl);

    // Store sensor data
    struct SensorData {
        int sensorType;
        lv_obj_t* resultLabel;
        lv_obj_t* progressBar;
        lv_obj_t* progressText;
        lv_obj_t* nextScreen;
        lv_obj_t* button;
        bool* measurementFlag;
    };
    
    SensorData* sensorData = new SensorData{
        sensorType, 
        result_label, 
        pb, 
        progress_text, 
        next_scr,
        b,
        &measurements_done[sensorType]
    };
    
    // Button click event
    lv_obj_add_event_cb(b, [](lv_event_t* e){
        SensorData* data = (SensorData*)lv_event_get_user_data(e);
        
        if(*(data->measurementFlag)) {
            // For the last measurement (pulse), recreate the results screen
            if(data->sensorType == 4) { // 4 is pulse sensor
                // Recreate results screen with updated data
                create_results_screen();
                switch_scr(scr_results);
            } else {
                // Navigate to next screen
                switch_scr(data->nextScreen);
            }
            return;
        }
        
        // Disable button during measurement
        lv_obj_add_state(data->button, LV_STATE_DISABLED);
        
        // Show progress bar
        lv_obj_clear_flag(data->progressBar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(data->progressText, LV_OBJ_FLAG_HIDDEN);
        
        // Update result label
        lv_label_set_text(data->resultLabel, "Measuring...");
        lv_obj_set_style_text_color(data->resultLabel, lv_color_hex(0xF59E0B), 0);
        lv_task_handler();
        
        // Simulate measurement progress
        for(int progress = 0; progress <= 100; progress += 2) {
            lv_bar_set_value(data->progressBar, progress, LV_ANIM_ON);
            lv_label_set_text_fmt(data->progressText, "%d%%", progress);
            lv_task_handler();
            delay(30);
        }
        
        // Generate measurement - UPDATED FOR REAL HEIGHT SENSOR
        switch(data->sensorType) {
            case 0: // Blood Pressure (simulated)
                healthData.bp_sys = 110 + random(10, 40);
                healthData.bp_dia = 70 + random(5, 20);
                lv_label_set_text_fmt(data->resultLabel, "BP: %d/%d mmHg", 
                    healthData.bp_sys, healthData.bp_dia);
                break;
                
            case 1: // Height - USING REAL ULTRASONIC SENSOR
                {
                    float measuredHeight = measurePersonHeight();
                    if (measuredHeight > 0) {
                        healthData.height = measuredHeight;
                        healthData.height_measured = true;
                        lv_label_set_text_fmt(data->resultLabel, "Height: %.1f cm", healthData.height);
                        Serial.printf("✓ Height measured: %.1f cm\n", healthData.height);
                    } else {
                        // Fallback to simulation if sensor fails
                        healthData.height = 160.0 + random(0, 40);
                        lv_label_set_text_fmt(data->resultLabel, "Height: %.1f cm (simulated)", healthData.height);
                        Serial.println("✗ Using simulated height");
                    }
                }
                break;
                
            case 2: // Weight (simulated for now)
                healthData.weight = 60.0 + random(0, 40);
                if (healthData.height > 0) {
                    healthData.bmi = calculateBMI(healthData.weight, healthData.height);
                    lv_label_set_text_fmt(data->resultLabel, "Weight: %.1f kg\nBMI: %.1f", 
                        healthData.weight, healthData.bmi);
                } else {
                    lv_label_set_text_fmt(data->resultLabel, "Weight: %.1f kg", healthData.weight);
                }
                break;
                
            case 3: // Temperature (simulated)
                healthData.temperature = 36.5 + (random(0, 15) / 10.0);
                lv_label_set_text_fmt(data->resultLabel, "Temp: %.1f °C", healthData.temperature);
                break;
                
            case 4: // Pulse (simulated)
                healthData.heart_rate = 65 + random(0, 40);
                lv_label_set_text_fmt(data->resultLabel, "Heart Rate: %d BPM", healthData.heart_rate);
                break;
        }
        
        // Update result display
        lv_obj_set_style_text_color(data->resultLabel, lv_color_hex(0x10B981), 0);
        
        // Hide progress bar
        lv_obj_add_flag(data->progressBar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(data->progressText, LV_OBJ_FLAG_HIDDEN);
        
        // Re-enable button with continue text
        lv_obj_clear_state(data->button, LV_STATE_DISABLED);
        lv_label_set_text(lv_obj_get_child(data->button, 0), "CONTINUE →");
        
        // Mark measurement as done
        *(data->measurementFlag) = true;
        
    }, LV_EVENT_CLICKED, sensorData);
    
    return scr;
}

// ==================== 4. Screen Definitions ====================
// ==================== 4. Screen Definitions ====================
void create_data_view_screen() {
    scr_data_view = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_data_view, lv_color_hex(0x0F172A), 0);
    lv_obj_clear_flag(scr_data_view, LV_OBJ_FLAG_SCROLLABLE); // Add this line
    
    // Title
    lv_obj_t *title = lv_label_create(scr_data_view);
    lv_label_set_text(title, "STORED HEALTH DATA");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x3B82F6), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);
    
    // Create a container for the table
    lv_obj_t *table_container = lv_obj_create(scr_data_view);
    lv_obj_set_size(table_container, 750, 350);
    lv_obj_align(table_container, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_border_width(table_container, 0, 0);
    lv_obj_set_style_bg_opa(table_container, LV_OPA_TRANSP, 0);
    
    // Read data and create table
    String data = readHealthData();
    String lines[50];
    int lineCount = 0;
    
    // Parse the data into lines
    int startIdx = 0;
    for(int i = 0; i < data.length(); i++) {
        if(data.charAt(i) == '\n') {
            lines[lineCount++] = data.substring(startIdx, i);
            startIdx = i + 1;
            if(lineCount >= 49) break;
        }
    }
    
    // Create column headers
    const char* headers[] = {"Timestamp", "Name", "Age", "Gender", "Address", "Weight", "Height", "Temp", "BMI", "HR", "BP Sys", "BP Dia"};
    const int colWidths[] = {120, 100, 50, 70, 150, 70, 70, 70, 70, 60, 80, 80};
    
    int yPos = 0;
    // Create header row
    lv_obj_t *header_row = lv_obj_create(table_container);
    lv_obj_set_size(header_row, 730, 40);
    lv_obj_set_pos(header_row, 10, yPos);
    lv_obj_set_style_bg_color(header_row, lv_color_hex(0x1E293B), 0);
    lv_obj_set_style_border_width(header_row, 0, 0);
    
    int xPos = 0;
    for(int col = 0; col < 12; col++) {
        lv_obj_t *header = lv_label_create(header_row);
        lv_label_set_text(header, headers[col]);
        lv_obj_set_style_text_font(header, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(header, lv_color_hex(0x3B82F6), 0);
        lv_obj_set_size(header, colWidths[col], 40);
        lv_obj_set_pos(header, xPos, 0);
        xPos += colWidths[col];
    }
    
    yPos += 45;
    
    // Create data rows
    for(int row = 1; row < lineCount && row < 8; row++) {
        // Parse CSV line
        String fields[12];
        int fieldIdx = 0;
        int start = 0;
        String line = lines[row];
        
        for(int i = 0; i < line.length(); i++) {
            if(line.charAt(i) == ',') {
                fields[fieldIdx++] = line.substring(start, i);
                start = i + 1;
                if(fieldIdx >= 11) break;
            }
        }
        fields[fieldIdx] = line.substring(start);
        
        // Create row
        lv_obj_t *data_row = lv_obj_create(table_container);
        lv_obj_set_size(data_row, 730, 35);
        lv_obj_set_pos(data_row, 10, yPos);
        lv_obj_set_style_bg_color(data_row, row % 2 == 0 ? lv_color_hex(0x1E293B) : lv_color_hex(0x0F172A), 0);
        lv_obj_set_style_border_width(data_row, 0, 0);
        
        xPos = 0;
        for(int col = 0; col < 12; col++) {
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
    
    // Button container - MODIFIED TO PREVENT SCROLLING
    lv_obj_t *btn_container = lv_obj_create(scr_data_view);
    lv_obj_set_size(btn_container, 500, 60);
    lv_obj_align(btn_container, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_set_flex_flow(btn_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_container, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(btn_container, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(btn_container, LV_OBJ_FLAG_SCROLLABLE); // Add this line
    
    // Refresh button
    lv_obj_t *btn_load = lv_btn_create(btn_container);
    lv_obj_set_size(btn_load, 150, 50);
    lv_obj_set_style_bg_color(btn_load, lv_color_hex(0x3B82F6), 0);
    lv_obj_clear_flag(btn_load, LV_OBJ_FLAG_SCROLLABLE); // Add this line
    lv_obj_t *load_lbl = lv_label_create(btn_load);
    lv_label_set_text(load_lbl, "REFRESH");
    lv_obj_set_style_text_font(load_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(load_lbl);
    
    lv_obj_add_event_cb(btn_load, [](lv_event_t* e){
        // Instead of deleting and recreating, just reload the data
        // Delete the current screen and create a new one
        lv_obj_del(scr_data_view);
        create_data_view_screen();
        lv_scr_load(scr_data_view);
    }, LV_EVENT_CLICKED, NULL);
    
    // Clear data button
    lv_obj_t *btn_clear = lv_btn_create(btn_container);
    lv_obj_set_size(btn_clear, 150, 50);
    lv_obj_set_style_bg_color(btn_clear, lv_color_hex(0xEF4444), 0);
    lv_obj_clear_flag(btn_clear, LV_OBJ_FLAG_SCROLLABLE); // Add this line
    lv_obj_t *clear_lbl = lv_label_create(btn_clear);
    lv_label_set_text(clear_lbl, "CLEAR ALL");
    lv_obj_set_style_text_font(clear_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(clear_lbl);
    
    lv_obj_add_event_cb(btn_clear, [](lv_event_t* e){
        if(deleteHealthData()){
            // Show success message
            lv_obj_t *msg_box = lv_obj_create(lv_layer_top());
            lv_obj_set_size(msg_box, 300, 80);
            lv_obj_center(msg_box);
            lv_obj_set_style_bg_color(msg_box, lv_color_hex(0x10B981), 0);
            lv_obj_set_style_radius(msg_box, 10, 0);
            
            lv_obj_t *msg = lv_label_create(msg_box);
            lv_label_set_text(msg, "✓ All data cleared!");
            lv_obj_set_style_text_font(msg, &lv_font_montserrat_14, 0);
            lv_obj_center(msg);
            
            // Wait a bit then delete the message box
            delay(2000);
            lv_obj_del(msg_box);
            
            // Refresh the screen
            lv_obj_del(scr_data_view);
            create_data_view_screen();
            lv_scr_load(scr_data_view);
        }
    }, LV_EVENT_CLICKED, NULL);
    
    // Back button - FIXED: Use correct event handling
    lv_obj_t *btn_back = lv_btn_create(btn_container);
    lv_obj_set_size(btn_back, 150, 50);
    lv_obj_clear_flag(btn_back, LV_OBJ_FLAG_SCROLLABLE); // Add this line
    lv_obj_t *back_lbl = lv_label_create(btn_back);
    lv_label_set_text(back_lbl, "BACK");
    lv_obj_set_style_text_font(back_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(back_lbl);
    
    // Fix: Store a static variable to track if we need to recreate the welcome screen
    static bool welcome_screen_created = false;
    lv_obj_add_event_cb(btn_back, [](lv_event_t* e){ 
        // Make sure welcome screen exists
        if(scr_welcome == NULL) {
            create_welcome_screen();
        }
        switch_scr(scr_welcome); 
    }, LV_EVENT_CLICKED, NULL);
}
void create_welcome_screen() {
    scr_welcome = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_welcome, lv_color_hex(0x0F172A), 0);
    
    // Title
    lv_obj_t *t = lv_label_create(scr_welcome);
    lv_label_set_text(t, "SMART HEALTH KIOSK");
    lv_obj_set_style_text_font(t, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(t, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(t, LV_ALIGN_CENTER, 0, -80);
    
    // SD Card status
    lv_obj_t *sd_status = lv_label_create(scr_welcome);
    if(sdCardInitialized) {
        lv_label_set_text(sd_status, "✓ SD Card Ready");
        lv_obj_set_style_text_color(sd_status, lv_color_hex(0x10B981), 0);
    } else {
        lv_label_set_text(sd_status, "✗ SD Card Not Found");
        lv_obj_set_style_text_color(sd_status, lv_color_hex(0xEF4444), 0);
    }
    lv_obj_set_style_text_font(sd_status, &lv_font_montserrat_14, 0);
    lv_obj_align(sd_status, LV_ALIGN_CENTER, 0, -30);
    
    // Start button
    lv_obj_t *b = lv_btn_create(scr_welcome);
    lv_obj_set_size(b, 250, 70);
    lv_obj_align(b, LV_ALIGN_CENTER, 0, 40);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x10B981), 0);
    lv_obj_t *btn_label = lv_label_create(b);
    lv_label_set_text(btn_label, "START NEW CHECKUP");
    lv_obj_set_style_text_font(btn_label, &lv_font_montserrat_14, 0);
    lv_obj_center(btn_label);
    lv_obj_add_event_cb(b, [](lv_event_t*e){ 
        // Reset measurement flags
        for(int i = 0; i < 5; i++) measurements_done[i] = false;
        switch_scr(scr_info); 
    }, LV_EVENT_CLICKED, NULL);
    
    // View Data button
    lv_obj_t *data_btn = lv_btn_create(scr_welcome);
    lv_obj_set_size(data_btn, 250, 70);
    lv_obj_align(data_btn, LV_ALIGN_CENTER, 0, 130);
    lv_obj_set_style_bg_color(data_btn, lv_color_hex(0x3B82F6), 0);
    lv_obj_t *data_label = lv_label_create(data_btn);
    lv_label_set_text(data_label, "VIEW HEALTH DATA");
    lv_obj_set_style_text_font(data_label, &lv_font_montserrat_14, 0);
    lv_obj_center(data_label);
    lv_obj_add_event_cb(data_btn, [](lv_event_t*e){ 
        create_data_view_screen();
        switch_scr(scr_data_view); 
    }, LV_EVENT_CLICKED, NULL);
}

void create_info_screen() {
    scr_info = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_info, lv_color_hex(0x0F172A), 0);

    // Title
    lv_obj_t* title = lv_label_create(scr_info);
    lv_label_set_text(title, "PATIENT INFORMATION");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x3B82F6), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);

    // Form container
    lv_obj_t *form = lv_obj_create(scr_info);
    lv_obj_set_size(form, 470, 450);
    lv_obj_align(form, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_flex_flow(form, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(form, 20, 0);
    lv_obj_set_style_pad_gap(form, 15, 0);

    // Full Name
    lv_obj_t *name_label = lv_label_create(form);
    lv_label_set_text(name_label, "Full Name:");
    lv_obj_set_style_text_font(name_label, &lv_font_montserrat_14, 0);
    name_ta = lv_textarea_create(form);
    lv_obj_set_width(name_ta, LV_PCT(100));
    lv_obj_set_height(name_ta, 50);
    lv_textarea_set_placeholder_text(name_ta, "Enter full name");
    lv_obj_set_style_text_font(name_ta, &lv_font_montserrat_14, 0);
    lv_obj_add_event_cb(name_ta, ta_event_cb, LV_EVENT_CLICKED, NULL);

    // Age
    lv_obj_t *age_label = lv_label_create(form);
    lv_label_set_text(age_label, "Age:");
    lv_obj_set_style_text_font(age_label, &lv_font_montserrat_14, 0);
    age_ta = lv_textarea_create(form);
    lv_obj_set_width(age_ta, LV_PCT(100));
    lv_obj_set_height(age_ta, 50);
    lv_textarea_set_placeholder_text(age_ta, "Enter age");
    lv_obj_set_style_text_font(age_ta, &lv_font_montserrat_14, 0);
    lv_obj_add_event_cb(age_ta, ta_event_cb, LV_EVENT_CLICKED, NULL);

    // Gender
    lv_obj_t *gender_label = lv_label_create(form);
    lv_label_set_text(gender_label, "Gender:");
    lv_obj_set_style_text_font(gender_label, &lv_font_montserrat_14, 0);
    gender_dd = lv_dropdown_create(form);
    lv_obj_set_width(gender_dd, LV_PCT(100));
    lv_obj_set_height(gender_dd, 50);
    lv_dropdown_set_options(gender_dd, "Male\nFemale\nOther\nPrefer not to say");
    lv_obj_set_style_text_font(gender_dd, &lv_font_montserrat_14, 0);

    // Address
    lv_obj_t *address_label = lv_label_create(form);
    lv_label_set_text(address_label, "Address:");
    lv_obj_set_style_text_font(address_label, &lv_font_montserrat_14, 0);
    address_ta = lv_textarea_create(form);
    lv_obj_set_width(address_ta, LV_PCT(100));
    lv_obj_set_height(address_ta, 80);
    lv_textarea_set_placeholder_text(address_ta, "Enter address");
    lv_obj_set_style_text_font(address_ta, &lv_font_montserrat_14, 0);
    lv_obj_add_event_cb(address_ta, ta_event_cb, LV_EVENT_CLICKED, NULL);

    // Next Button
    lv_obj_t *n = lv_btn_create(scr_info);
    lv_obj_set_size(n, 150, 60);
    lv_obj_align(n, LV_ALIGN_BOTTOM_RIGHT, -30, -20);
    lv_obj_set_style_bg_color(n, lv_color_hex(0x10B981), 0);
    lv_obj_t *btn_lbl = lv_label_create(n);
    lv_label_set_text(btn_lbl, "NEXT →");
    lv_obj_set_style_text_font(btn_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(btn_lbl);
    
    lv_obj_add_event_cb(n, [](lv_event_t* e){ 
        // Store user info
        healthData.name = lv_textarea_get_text(name_ta);
        healthData.age = lv_textarea_get_text(age_ta);
        char gender[20];
        lv_dropdown_get_selected_str(gender_dd, gender, sizeof(gender));
        healthData.gender = String(gender);
        healthData.address = lv_textarea_get_text(address_ta);
        
        // Reset all measurements
        healthData.bp_sys = 0;
        healthData.bp_dia = 0;
        healthData.height = 0;
        healthData.weight = 0;
        healthData.temperature = 0;
        healthData.heart_rate = 0;
        healthData.bmi = 0;
        
        switch_scr(scr_bp); 
    }, LV_EVENT_CLICKED, NULL);
}

void create_results_screen() {
    // Delete old screen if it exists
    if(scr_results != NULL) {
        lv_obj_del(scr_results);
    }
    
    scr_results = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_results, lv_color_hex(0x0F172A), 0);
    
    // Title
    lv_obj_t *title = lv_label_create(scr_results);
    lv_label_set_text(title, "HEALTH CHECKUP REPORT");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x3B82F6), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);
    
    // Patient info - FIXED: Check if data is available before displaying
    lv_obj_t *patient_info = lv_label_create(scr_results);
    String patientInfoText = "Patient: ";
    
    // Check if name exists and is not empty
    if(healthData.name.length() > 0) {
        patientInfoText += healthData.name;
    } else {
        patientInfoText += "Not provided";
    }
    
    patientInfoText += " | Age: ";
    if(healthData.age.length() > 0) {
        patientInfoText += healthData.age;
    } else {
        patientInfoText += "N/A";
    }
    
    patientInfoText += " | Gender: ";
    if(healthData.gender.length() > 0) {
        patientInfoText += healthData.gender;
    } else {
        patientInfoText += "Not specified";
    }
    
    lv_label_set_text(patient_info, patientInfoText.c_str());
    lv_obj_set_style_text_font(patient_info, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(patient_info, lv_color_hex(0x94A3B8), 0);
    lv_obj_align(patient_info, LV_ALIGN_TOP_MID, 0, 70);
    
    // Measurements container
    lv_obj_t *measurements = lv_obj_create(scr_results);
    lv_obj_set_size(measurements, 470, 350);
    lv_obj_align(measurements, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_flex_flow(measurements, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(measurements, 20, 0);
    lv_obj_set_style_pad_gap(measurements, 10, 0);
    
    // Display all measurements (check if they exist)
    lv_obj_t *bp_label = lv_label_create(measurements);
    if(healthData.bp_sys > 0 && healthData.bp_dia > 0) {
        lv_label_set_text_fmt(bp_label, "Blood Pressure: %d/%d mmHg", healthData.bp_sys, healthData.bp_dia);
    } else {
        lv_label_set_text(bp_label, "Blood Pressure: Not measured");
    }
    lv_obj_set_style_text_font(bp_label, &lv_font_montserrat_14, 0);
    
    lv_obj_t *height_label = lv_label_create(measurements);
    if(healthData.height > 0) {
        lv_label_set_text_fmt(height_label, "Height: %.1f cm", healthData.height);
    } else {
        lv_label_set_text(height_label, "Height: Not measured");
    }
    lv_obj_set_style_text_font(height_label, &lv_font_montserrat_14, 0);
    
    lv_obj_t *weight_label = lv_label_create(measurements);
    if(healthData.weight > 0) {
        lv_label_set_text_fmt(weight_label, "Weight: %.1f kg", healthData.weight);
    } else {
        lv_label_set_text(weight_label, "Weight: Not measured");
    }
    lv_obj_set_style_text_font(weight_label, &lv_font_montserrat_14, 0);
    
    lv_obj_t *temp_label = lv_label_create(measurements);
    if(healthData.temperature > 0) {
        lv_label_set_text_fmt(temp_label, "Temperature: %.1f °C", healthData.temperature);
    } else {
        lv_label_set_text(temp_label, "Temperature: Not measured");
    }
    lv_obj_set_style_text_font(temp_label, &lv_font_montserrat_14, 0);
    
    lv_obj_t *hr_label = lv_label_create(measurements);
    if(healthData.heart_rate > 0) {
        lv_label_set_text_fmt(hr_label, "Heart Rate: %d BPM", healthData.heart_rate);
    } else {
        lv_label_set_text(hr_label, "Heart Rate: Not measured");
    }
    lv_obj_set_style_text_font(hr_label, &lv_font_montserrat_14, 0);
    
    lv_obj_t *bmi_label = lv_label_create(measurements);
    if(healthData.bmi > 0) {
        lv_label_set_text_fmt(bmi_label, "BMI: %.1f", healthData.bmi);
    } else {
        lv_label_set_text(bmi_label, "BMI: Not calculated");
    }
    lv_obj_set_style_text_font(bmi_label, &lv_font_montserrat_14, 0);
    
    // BMI status
    lv_obj_t *bmi_status = lv_label_create(scr_results);
    String status_text = "";
    lv_color_t status_color = lv_color_hex(0x94A3B8);
    
    if (healthData.bmi > 0) {
        if (healthData.bmi < 18.5) {
            status_text = "Underweight";
            status_color = lv_color_hex(0x3B82F6); // Blue
        } else if (healthData.bmi < 25) {
            status_text = "Normal";
            status_color = lv_color_hex(0x10B981); // Green
        } else if (healthData.bmi < 30) {
            status_text = "Overweight";
            status_color = lv_color_hex(0xF59E0B); // Yellow
        } else {
            status_text = "Obese";
            status_color = lv_color_hex(0xEF4444); // Red
        }
        lv_label_set_text_fmt(bmi_status, "Status: %s", status_text.c_str());
    } else {
        lv_label_set_text(bmi_status, "Complete measurements for BMI");
    }
    
    lv_obj_set_style_text_font(bmi_status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(bmi_status, status_color, 0);
    lv_obj_align(bmi_status, LV_ALIGN_TOP_MID, 0, 200);
    
    // Button container
    lv_obj_t *btn_container = lv_obj_create(scr_results);
    lv_obj_set_size(btn_container, 400, 80);
    lv_obj_align(btn_container, LV_ALIGN_BOTTOM_MID, 0, -30);
    lv_obj_set_flex_flow(btn_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(btn_container, 20, 0);
    lv_obj_set_style_bg_opa(btn_container, LV_OPA_TRANSP, 0);
    
    // Save button (only if SD card available)
    if(sdCardInitialized) {
        lv_obj_t *btn_save = lv_btn_create(btn_container);
        lv_obj_set_size(btn_save, 180, 60);
        lv_obj_set_style_bg_color(btn_save, lv_color_hex(0x10B981), 0);
        lv_obj_t *save_lbl = lv_label_create(btn_save);
        lv_label_set_text(save_lbl, "SAVE DATA");
        lv_obj_set_style_text_font(save_lbl, &lv_font_montserrat_14, 0);
        lv_obj_center(save_lbl);
        
        lv_obj_add_event_cb(btn_save, [](lv_event_t* e){
            // Get current timestamp
            struct tm timeinfo;
            if(!getLocalTime(&timeinfo)){
                healthData.timestamp = "N/A";
            } else {
                char timeString[64];
                strftime(timeString, sizeof(timeString), "%Y-%m-%d %H:%M:%S", &timeinfo);
                healthData.timestamp = String(timeString);
            }
            
            // Save to SD card
            if(saveHealthData(healthData.toCSV())){
                // Show success message
                lv_obj_t *msg_box = lv_obj_create(lv_layer_top());
                lv_obj_set_size(msg_box, 300, 80);
                lv_obj_center(msg_box);
                lv_obj_set_style_bg_color(msg_box, lv_color_hex(0x10B981), 0);
                lv_obj_set_style_radius(msg_box, 10, 0);
                
                lv_obj_t *msg = lv_label_create(msg_box);
                lv_label_set_text(msg, "✓ Data saved successfully!");
                lv_obj_set_style_text_font(msg, &lv_font_montserrat_14, 0);
                lv_obj_center(msg);
                
                delay(2000);
                lv_obj_del(msg_box);
            }
        }, LV_EVENT_CLICKED, NULL);
    } else {
        // Show SD card warning
        lv_obj_t *sd_warning = lv_label_create(scr_results);
        lv_label_set_text(sd_warning, "⚠ SD Card not available for saving");
        lv_obj_set_style_text_color(sd_warning, lv_color_hex(0xF59E0B), 0);
        lv_obj_set_style_text_font(sd_warning, &lv_font_montserrat_14, 0);
        lv_obj_align(sd_warning, LV_ALIGN_BOTTOM_MID, 0, -60);
    }
    
    // Done button
    lv_obj_t *btn_done = lv_btn_create(btn_container);
    lv_obj_set_size(btn_done, 180, 60);
    lv_obj_t *done_lbl = lv_label_create(btn_done);
    lv_label_set_text(done_lbl, "DONE");
    lv_obj_set_style_text_font(done_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(done_lbl);
    lv_obj_add_event_cb(btn_done, [](lv_event_t* e){ 
        // Reset health data for next session
        healthData = HealthData();
        switch_scr(scr_welcome); 
    }, LV_EVENT_CLICKED, NULL);
}

void create_results_screen() {
    // Delete old screen if it exists
    if(scr_results != NULL) {
        lv_obj_del(scr_results);
    }
    
    scr_results = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_results, lv_color_hex(0x0F172A), 0);
    
    // Title
    lv_obj_t *title = lv_label_create(scr_results);
    lv_label_set_text(title, "HEALTH CHECKUP REPORT");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x3B82F6), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);
    
    // Patient info
    lv_obj_t *patient_info = lv_label_create(scr_results);
    String patientInfoText = "Patient: ";
    
    if(healthData.name.length() > 0) {
        patientInfoText += healthData.name;
    } else {
        patientInfoText += "Not provided";
    }
    
    patientInfoText += " | Age: ";
    if(healthData.age.length() > 0) {
        patientInfoText += healthData.age;
    } else {
        patientInfoText += "N/A";
    }
    
    patientInfoText += " | Gender: ";
    if(healthData.gender.length() > 0) {
        patientInfoText += healthData.gender;
    } else {
        patientInfoText += "Not specified";
    }
    
    lv_label_set_text(patient_info, patientInfoText.c_str());
    lv_obj_set_style_text_font(patient_info, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(patient_info, lv_color_hex(0x94A3B8), 0);
    lv_obj_align(patient_info, LV_ALIGN_TOP_MID, 0, 70);
    
    // Measurements container
    lv_obj_t *measurements = lv_obj_create(scr_results);
    lv_obj_set_size(measurements, 470, 350);
    lv_obj_align(measurements, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_flex_flow(measurements, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(measurements, 20, 0);
    lv_obj_set_style_pad_gap(measurements, 10, 0);
    
    // Display all measurements (check if they exist)
    lv_obj_t *bp_label = lv_label_create(measurements);
    if(healthData.bp_sys > 0 && healthData.bp_dia > 0) {
        lv_label_set_text_fmt(bp_label, "Blood Pressure: %d/%d mmHg", healthData.bp_sys, healthData.bp_dia);
    } else {
        lv_label_set_text(bp_label, "Blood Pressure: Not measured");
    }
    lv_obj_set_style_text_font(bp_label, &lv_font_montserrat_14, 0);
    
    lv_obj_t *height_label = lv_label_create(measurements);
    if(healthData.height > 0) {
        lv_label_set_text_fmt(height_label, "Height: %.1f cm", healthData.height);
    } else {
        lv_label_set_text(height_label, "Height: Not measured");
    }
    lv_obj_set_style_text_font(height_label, &lv_font_montserrat_14, 0);
    
    lv_obj_t *weight_label = lv_label_create(measurements);
    if(healthData.weight > 0) {
        lv_label_set_text_fmt(weight_label, "Weight: %.1f kg", healthData.weight);
    } else {
        lv_label_set_text(weight_label, "Weight: Not measured");
    }
    lv_obj_set_style_text_font(weight_label, &lv_font_montserrat_14, 0);
    
    lv_obj_t *temp_label = lv_label_create(measurements);
    if(healthData.temperature > 0) {
        lv_label_set_text_fmt(temp_label, "Temperature: %.1f °C", healthData.temperature);
    } else {
        lv_label_set_text(temp_label, "Temperature: Not measured");
    }
    lv_obj_set_style_text_font(temp_label, &lv_font_montserrat_14, 0);
    
    lv_obj_t *hr_label = lv_label_create(measurements);
    if(healthData.heart_rate > 0) {
        lv_label_set_text_fmt(hr_label, "Heart Rate: %d BPM", healthData.heart_rate);
    } else {
        lv_label_set_text(hr_label, "Heart Rate: Not measured");
    }
    lv_obj_set_style_text_font(hr_label, &lv_font_montserrat_14, 0);
    
    lv_obj_t *bmi_label = lv_label_create(measurements);
    if(healthData.bmi > 0) {
        lv_label_set_text_fmt(bmi_label, "BMI: %.1f", healthData.bmi);
    } else {
        lv_label_set_text(bmi_label, "BMI: Not calculated");
    }
    lv_obj_set_style_text_font(bmi_label, &lv_font_montserrat_14, 0);
    
    // BMI status
    lv_obj_t *bmi_status = lv_label_create(scr_results);
    String status_text = "";
    lv_color_t status_color = lv_color_hex(0x94A3B8);
    
    if (healthData.bmi > 0) {
        if (healthData.bmi < 18.5) {
            status_text = "Underweight";
            status_color = lv_color_hex(0x3B82F6); // Blue
        } else if (healthData.bmi < 25) {
            status_text = "Normal";
            status_color = lv_color_hex(0x10B981); // Green
        } else if (healthData.bmi < 30) {
            status_text = "Overweight";
            status_color = lv_color_hex(0xF59E0B); // Yellow
        } else {
            status_text = "Obese";
            status_color = lv_color_hex(0xEF4444); // Red
        }
        lv_label_set_text_fmt(bmi_status, "Status: %s", status_text.c_str());
    } else {
        lv_label_set_text(bmi_status, "Complete measurements for BMI");
    }
    
    lv_obj_set_style_text_font(bmi_status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(bmi_status, status_color, 0);
    lv_obj_align(bmi_status, LV_ALIGN_TOP_MID, 0, 200);
    
    // Button container - MODIFIED TO ADD PRINT BUTTON
    lv_obj_t *btn_container = lv_obj_create(scr_results);
    lv_obj_set_size(btn_container, 400, 180);  // Increased height
    lv_obj_align(btn_container, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_set_flex_flow(btn_container, LV_FLEX_FLOW_COLUMN);  // Changed to column
    lv_obj_set_style_pad_gap(btn_container, 10, 0);
    lv_obj_set_style_bg_opa(btn_container, LV_OPA_TRANSP, 0);
    
    // First row: Print and Save buttons
    lv_obj_t *btn_row1 = lv_obj_create(btn_container);
    lv_obj_set_size(btn_row1, 400, 60);
    lv_obj_set_flex_flow(btn_row1, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row1, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(btn_row1, LV_OPA_TRANSP, 0);
    
    // Print button (if printer available)
    if(printerInitialized) {
        lv_obj_t *btn_print = lv_btn_create(btn_row1);
        lv_obj_set_size(btn_print, 180, 50);
        lv_obj_set_style_bg_color(btn_print, lv_color_hex(0x8B5CF6), 0); // Purple
        lv_obj_t *print_lbl = lv_label_create(btn_print);
        lv_label_set_text(print_lbl, "PRINT REPORT");
        lv_obj_set_style_text_font(print_lbl, &lv_font_montserrat_14, 0);
        lv_obj_center(print_lbl);
        
        lv_obj_add_event_cb(btn_print, [](lv_event_t* e){
            // Print the report
            thermalPrinter.printHealthReport(healthData);
            
            // Show success message
            lv_obj_t *msg_box = lv_obj_create(lv_layer_top());
            lv_obj_set_size(msg_box, 300, 80);
            lv_obj_center(msg_box);
            lv_obj_set_style_bg_color(msg_box, lv_color_hex(0x8B5CF6), 0);
            lv_obj_set_style_radius(msg_box, 10, 0);
            
            lv_obj_t *msg = lv_label_create(msg_box);
            lv_label_set_text(msg, "✓ Report printing...");
            lv_obj_set_style_text_font(msg, &lv_font_montserrat_14, 0);
            lv_obj_center(msg);
            
            delay(2000);
            lv_obj_del(msg_box);
        }, LV_EVENT_CLICKED, NULL);
    }
    
    // Save button (only if SD card available)
    if(sdCardInitialized) {
        lv_obj_t *btn_save = lv_btn_create(btn_row1);
        lv_obj_set_size(btn_save, 180, 50);
        lv_obj_set_style_bg_color(btn_save, lv_color_hex(0x10B981), 0);
        lv_obj_t *save_lbl = lv_label_create(btn_save);
        lv_label_set_text(save_lbl, "SAVE DATA");
        lv_obj_set_style_text_font(save_lbl, &lv_font_montserrat_14, 0);
        lv_obj_center(save_lbl);
        
        lv_obj_add_event_cb(btn_save, [](lv_event_t* e){
            // Get current timestamp
            struct tm timeinfo;
            if(!getLocalTime(&timeinfo)){
                healthData.timestamp = "N/A";
            } else {
                char timeString[64];
                strftime(timeString, sizeof(timeString), "%Y-%m-%d %H:%M:%S", &timeinfo);
                healthData.timestamp = String(timeString);
            }
            
            // Save to SD card
            if(saveHealthData(healthData.toCSV())){
                // Show success message
                lv_obj_t *msg_box = lv_obj_create(lv_layer_top());
                lv_obj_set_size(msg_box, 300, 80);
                lv_obj_center(msg_box);
                lv_obj_set_style_bg_color(msg_box, lv_color_hex(0x10B981), 0);
                lv_obj_set_style_radius(msg_box, 10, 0);
                
                lv_obj_t *msg = lv_label_create(msg_box);
                lv_label_set_text(msg, "✓ Data saved successfully!");
                lv_obj_set_style_text_font(msg, &lv_font_montserrat_14, 0);
                lv_obj_center(msg);
                
                delay(2000);
                lv_obj_del(msg_box);
            }
        }, LV_EVENT_CLICKED, NULL);
    }
    
    // Second row: Done button
    lv_obj_t *btn_row2 = lv_obj_create(btn_container);
    lv_obj_set_size(btn_row2, 400, 60);
    lv_obj_set_flex_flow(btn_row2, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row2, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(btn_row2, LV_OPA_TRANSP, 0);
    
    // Done button
    lv_obj_t *btn_done = lv_btn_create(btn_row2);
    lv_obj_set_size(btn_done, 180, 50);
    lv_obj_t *done_lbl = lv_label_create(btn_done);
    lv_label_set_text(done_lbl, "DONE");
    lv_obj_set_style_text_font(done_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(done_lbl);
    lv_obj_add_event_cb(btn_done, [](lv_event_t* e){ 
        // Reset health data for next session
        healthData = HealthData();
        switch_scr(scr_welcome); 
    }, LV_EVENT_CLICKED, NULL);
    
    // Printer status indicator
    if(!printerInitialized) {
        lv_obj_t *printer_warning = lv_label_create(scr_results);
        lv_label_set_text(printer_warning, "⚠ Printer not connected");
        lv_obj_set_style_text_color(printer_warning, lv_color_hex(0xF59E0B), 0);
        lv_obj_set_style_text_font(printer_warning, &lv_font_montserrat_14, 0);
        lv_obj_align(printer_warning, LV_ALIGN_BOTTOM_MID, 0, -60);
    }
}

// ==================== 5. Setup & Main Loop ====================

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("==================================");
    Serial.println("   SMART HEALTH KIOSK STARTUP");
    Serial.println("==================================");
    
    // Initialize display
    gfx.begin();
    gfx.setRotation(DISPLAY_ROTATION);
    gfx.fillScreen(0x000000);
    pinMode(GFX_BL, OUTPUT); 
    digitalWrite(GFX_BL, HIGH); // Backlight
    
    // Initialize touch
    ts.begin();
    ts.setRotation(DISPLAY_ROTATION);
    Serial.println("✓ Touch screen initialized");
    
    // Initialize ultrasonic sensor
    Serial.println("\n=== Initializing Ultrasonic Sensor ===");
    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);
    digitalWrite(TRIG_PIN, LOW);
    Serial.println("✓ Ultrasonic sensor initialized");
    Serial.printf("Sensor mounting height: %.1f cm\n", SENSOR_MOUNTING_HEIGHT);
    Serial.println("Note: Make sure sensor is mounted above person's head");
    
    // Initialize thermal printer
    Serial.println("\n=== Initializing Thermal Printer ===");
    if (thermalPrinter.begin()) {
        if (thermalPrinter.connect()) {
            printerInitialized = true;
            Serial.println("✓ Thermal printer connected and ready");
        } else {
            Serial.println("✗ Could not connect to printer");
            Serial.println("Please make sure:");
            Serial.println("1. Printer is turned ON");
            Serial.println("2. Bluetooth is enabled on printer");
            Serial.println("3. Printer is paired with ESP32");
        }
    } else {
        Serial.println("✗ Failed to initialize Bluetooth");
    }
    
    // Initialize SD card
    Serial.println("\n=== Initializing SD Card ===");
    Serial.printf("SD Pins: CS=%d, MOSI=%d, MISO=%d, SCK=%d\n", 
                  SD_CS, SD_MOSI, SD_MISO, SD_SCK);
    
    sdCardInitialized = initSDCard();
    if(sdCardInitialized) {
        Serial.println("✓ SD Card initialized successfully");
    } else {
        Serial.println("✗ SD Card initialization failed");
    }
    
    // Initialize LVGL
    lv_init();
    lv_tick_set_cb(millis_cb);
    screenWidth = 480; 
    screenHeight = 800;
    
    // Create display buffer
    static lv_color_t buf[480 * 40];
    lv_display_t *disp = lv_display_create(screenWidth, screenHeight);
    lv_display_set_flush_cb(disp, my_disp_flush);
    lv_display_set_buffers(disp, buf, NULL, sizeof(buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
    
    // Initialize input device
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, my_touchpad_read);
    
    // Global Keyboard
    kb = lv_keyboard_create(lv_layer_sys());
    lv_obj_set_size(kb, 480, 240);
    lv_obj_set_style_text_font(kb, &lv_font_montserrat_14, 0);
    lv_obj_add_event_cb(kb, kb_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    
    // Initialize random seed for other sensor simulations
    randomSeed(analogRead(0));
    
    // Create screens
    Serial.println("\n=== Creating UI Screens ===");
    create_welcome_screen();
    create_info_screen();
    create_results_screen();
    
    // Create sensor screens
    scr_pulse = create_sensor_scr("PULSE RATE", "❤️", "Place finger on sensor for 10 seconds", scr_results, 4);
    scr_temp = create_sensor_scr("TEMPERATURE", "🌡️", "Look at thermal sensor from 5cm away", scr_pulse, 3);
    scr_weight = create_sensor_scr("WEIGHT SCALE", "⚖️", "Step onto scale platform", scr_temp, 2);
    scr_height = create_sensor_scr("HEIGHT SENSOR", "📏", "Stand straight under sensor\nMake sure head is directly under", scr_weight, 1);
    scr_bp = create_sensor_scr("BLOOD PRESSURE", "💓", "Wrap cuff around upper arm", scr_height, 0);
    
    Serial.println("✓ All screens created");
    
    // Load welcome screen
    lv_scr_load(scr_welcome);
    
    Serial.println("\n==================================");
    Serial.println("        SYSTEM READY");
    Serial.println("==================================");
    Serial.println("Connected Devices:");
    if (printerInitialized) Serial.println("✓ Thermal Printer");
    if (sdCardInitialized) Serial.println("✓ SD Card");
    Serial.println("✓ Ultrasonic Height Sensor");
    Serial.println("✓ Touch Screen Display");
}

void loop() {
    lv_task_handler();
    delay(5);
}