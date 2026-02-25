#include "sensors.h"

SensorConfig sensorConfig;
HealthData currentHealthData;

// Simulate realistic sensor readings
float simulateTemperature() {
    // Normal range with some variance
    float baseTemp = 36.5;
    float variance = random(-100, 100) / 100.0 * sensorConfig.temp_variance;
    return constrain(baseTemp + variance, sensorConfig.temp_min, sensorConfig.temp_max);
}

int simulateHeartRate() {
    int baseHR = 72;
    int variance = random(-sensorConfig.hr_variance, sensorConfig.hr_variance);
    return constrain(baseHR + variance, sensorConfig.hr_min, sensorConfig.hr_max);
}

float simulateWeight() {
    // Simulate weight with small daily variations
    float baseWeight = 70.0;
    float variance = random(-100, 100) / 100.0 * sensorConfig.weight_variance;
    return constrain(baseWeight + variance, sensorConfig.weight_min, sensorConfig.weight_max);
}

float simulateHeight() {
    // Height is usually stable for adults
    float baseHeight = 170.0;
    float variance = random(-100, 100) / 100.0 * sensorConfig.height_variance;
    return constrain(baseHeight + variance, sensorConfig.height_min, sensorConfig.height_max);
}

int simulateBPSystolic() {
    int baseBP = 120;
    int variance = random(-sensorConfig.bp_variance, sensorConfig.bp_variance);
    return constrain(baseBP + variance, sensorConfig.bp_sys_min, sensorConfig.bp_sys_max);
}

int simulateBPDiastolic() {
    int baseBP = 80;
    int variance = random(-sensorConfig.bp_variance, sensorConfig.bp_variance);
    return constrain(baseBP + variance, sensorConfig.bp_dia_min, sensorConfig.bp_dia_max);
}

float calculateBMI(float weight, float height) {
    // Height in meters
    float heightM = height / 100.0;
    return weight / (heightM * heightM);
}

void simulateSensors(HealthData& data) {
    data.weight = simulateWeight();
    data.height = simulateHeight();
    data.temperature = simulateTemperature();
    data.heart_rate = simulateHeartRate();
    data.bp_sys = simulateBPSystolic();
    data.bp_dia = simulateBPDiastolic();
    data.bmi = calculateBMI(data.weight, data.height);
    
    // Get current timestamp
    struct tm timeinfo;
    if(getLocalTime(&timeinfo)){
        char timeString[64];
        strftime(timeString, sizeof(timeString), "%Y-%m-%d %H:%M:%S", &timeinfo);
        data.timestamp = String(timeString);
    } else {
        data.timestamp = "N/A";
    }
}