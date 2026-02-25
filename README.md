# ESP32-8048S070C-LVGL-9
Kickstart 🚀 PlatformIO project (Arduino framework) for  
ESP32-8048S070C - 7 inch TFT-LCD Display 800*480 pixels with LVGL 9.2.2

# Requirements
PlatformIO  

*Libraries:*  
LVGL 9.2.2  
TAMC_GT911 1.0.2  
GFX Library for Arduino 1.5.0

# How to use?
Just clone or download this repo and open the folder ESP32-8048S070C-LVGL-9-main with PlatformIO.

# Resolving the Build Error

When you first compile the project, you may encounter the following error:  
`.pio/libdeps/esp32s3box/lvgl/src/lv_conf_internal.h:60:18: fatal error: ../../lv_conf.h: No such file or directory`

#### How to Fix

1. Navigate to the folder `.pio/libdeps/esp32s3box/lvgl`.
2. Locate the file named `lv_conf_template.h`.
3. Copy this file to the parent directory: `.pio/libdeps/esp32s3box`.
4. Rename the copied file to `lv_conf.h`.

After completing these steps, you should have the file located at:  
`.pio/libdeps/esp32s3box/lv_conf.h`  


# Screenshot
![IMG20241230110834(1)](https://github.com/user-attachments/assets/1f680a9a-5f0a-4f3b-81db-6ae69ed318a4)
