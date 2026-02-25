#ifndef SENSORS_H
#define SENSORS_H

#include <Arduino.h>
#include "display.h"

// Sensor simulation parameters
struct SensorConfig {
    // Temperature range (Celsius)
    float temp_min = 35.0;
    float temp_max = 42.0;
    float temp_variance = 0.5;
    
    // Heart rate range (BPM)
    int hr_min = 50;
    int hr_max = 120;
    int hr_variance = 10;
    
    // Weight range (kg)
    float weight_min = 40.0;
    float weight_max = 120.0;
    float weight_variance = 0.5;
    
    // Height range (cm) - Updated based on sensor
    float height_min = 140.0;
    float height_max = 200.0;
    float height_variance = 0.5;
    
    // Blood pressure ranges
    int bp_sys_min = 90;
    int bp_sys_max = 180;
    int bp_dia_min = 60;
    int bp_dia_max = 120;
    int bp_variance = 5;
    
    // Ultrasonic sensor mounting height (cm from ground)
    float sensor_mounting_height = 250.0; // Default 2.5m
};

// Measurement structure
struct HealthData {
    String timestamp;
    String name;
    String age;
    String gender;
    String address;
    float weight;
    float height;
    float temperature;
    float bmi;
    int heart_rate;
    int bp_sys;
    int bp_dia;
    
    // For real sensor data
    bool height_measured = false;
    bool weight_measured = false;
    bool temp_measured = false;
    bool hr_measured = false;
    bool bp_measured = false;
    
    String toCSV() {
        return String(timestamp + "," + 
                     name + "," + 
                     age + "," + 
                     gender + "," + 
                     address + "," + 
                     String(weight) + "," + 
                     String(height) + "," + 
                     String(temperature) + "," + 
                     String(bmi) + "," + 
                     String(heart_rate) + "," + 
                     String(bp_sys) + "," + 
                     String(bp_dia));
    }
    
    String toJSON() {
        return "{\"timestamp\":\"" + timestamp + 
               "\",\"name\":\"" + name + 
               "\",\"age\":\"" + age + 
               "\",\"gender\":\"" + gender + 
               "\",\"address\":\"" + address + 
               "\",\"weight\":" + String(weight) + 
               ",\"height\":" + String(height) + 
               ",\"temperature\":" + String(temperature) + 
               ",\"bmi\":" + String(bmi) + 
               ",\"heart_rate\":" + String(heart_rate) + 
               ",\"bp_sys\":" + String(bp_sys) + 
               ",\"bp_dia\":" + String(bp_dia) + "}";
    }
    
    void resetMeasurements() {
        height = 0;
        weight = 0;
        temperature = 0;
        heart_rate = 0;
        bp_sys = 0;
        bp_dia = 0;
        bmi = 0;
        height_measured = false;
        weight_measured = false;
        temp_measured = false;
        hr_measured = false;
        bp_measured = false;
    }
};

extern SensorConfig sensorConfig;
extern HealthData currentHealthData;

// Simulation functions
void simulateSensors(HealthData& data);
float calculateBMI(float weight, float height);

// Real sensor functions (to be implemented)
bool measureRealHeight(HealthData& data);
bool measureRealWeight(HealthData& data);
bool measureRealTemperature(HealthData& data);
bool measureRealHeartRate(HealthData& data);
bool measureRealBloodPressure(HealthData& data);

#endif // SENSORS_H