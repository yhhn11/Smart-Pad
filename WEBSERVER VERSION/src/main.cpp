#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <Adafruit_AS7341.h>
#include <AccelStepper.h>
#include <ArduinoJson.h>
#include "webpage.h"
#include "config.h"

// Pin definitions
#define IN1 27
#define IN2 25
#define IN3 33
#define IN4 32
#define PHOTOINTERRUPTER_PIN 18

// Configuration constants
#define STEPS_TO_FIRST_CEL 5
#define STEPS_NEXT_CEL 85
#define STEPS_CORRECTION 3
#define STEPS_TO_FIRST_SAMPLE 95
#define MIN_LED_CURRENT 4
#define MAX_LED_CURRENT 258
#define MIN_GAIN 1
#define MAX_GAIN 512
#define MAX_SAMPLES 22
#define NUM_CHANNELS 8
#define READINGS_PER_SAMPLE 20

// Wi-Fi access point settings (see include/config.h)
const char* ssid = AP_SSID;
const char* password = AP_PASSWORD;

// Global objects

AccelStepper stepper(AccelStepper::FULL4WIRE, IN1, IN3, IN2, IN4);
AsyncWebServer server(80);
Adafruit_AS7341 as7341;

// Data structure holding the measurement configuration
struct MeasurementConfig {
    uint16_t gain = 1;
    uint16_t current = 100;
    uint16_t numSamples = 1;
    uint16_t sampleType = 0;
    bool isValid = false;
};

// Data structure holding the measurement results
struct MeasurementData {
    float channels[NUM_CHANNELS];
    int currentSample = 0;
    bool isComplete = false;
    bool inProgress = false;
    bool positionReset = false;
};

// Global variables
MeasurementConfig config;
MeasurementData measurementData;
float whiteValues[NUM_CHANNELS] = {0};

// Helper functions
void enableStepper() {
    digitalWrite(IN1, HIGH);
    digitalWrite(IN2, HIGH);
    digitalWrite(IN3, HIGH);
    digitalWrite(IN4, HIGH);
}

void disableStepper() {
    digitalWrite(IN1, LOW);
    digitalWrite(IN2, LOW);
    digitalWrite(IN3, LOW);
    digitalWrite(IN4, LOW);
}

void resetPosition() {
    enableStepper();
    stepper.setSpeed(-75);

    while (digitalRead(PHOTOINTERRUPTER_PIN) == LOW) {
        stepper.runSpeed();
    }

    stepper.stop();
    stepper.setCurrentPosition(0);
    measurementData.positionReset = true;
    Serial.println("Home position found, step counter reset.");
}

void moveToNextPosition(int currentSample) {
    int steps;

    if(currentSample == 0){
        steps = - STEPS_TO_FIRST_CEL;
    }
    else if(currentSample == 1){
        steps = STEPS_TO_FIRST_SAMPLE;
    }
    else if(currentSample % 3 == 0 && currentSample < MAX_SAMPLES + 1){
        steps = STEPS_NEXT_CEL + STEPS_CORRECTION;
    }
    else{
        steps = STEPS_NEXT_CEL;
    }

    stepper.move(steps);
    while (stepper.distanceToGo() != 0) {
        stepper.run();
    }
}

bool configureSensor() {

    as7341.setLEDCurrent(config.current);

    as7341_gain_t gainSetting;
    switch (config.gain) {
        case 1: gainSetting = AS7341_GAIN_1X; break;
        case 2: gainSetting = AS7341_GAIN_2X; break;
        case 4: gainSetting = AS7341_GAIN_4X; break;
        case 8: gainSetting = AS7341_GAIN_8X; break;
        case 16: gainSetting = AS7341_GAIN_16X; break;
        case 32: gainSetting = AS7341_GAIN_32X; break;
        case 64: gainSetting = AS7341_GAIN_64X; break;
        case 128: gainSetting = AS7341_GAIN_128X; break;
        case 256: gainSetting = AS7341_GAIN_256X; break;
        case 512: gainSetting = AS7341_GAIN_512X; break;
        default: return false;
    }

    as7341.setGain(gainSetting);
    return true;
}

float trimmedMeanFilter(uint16_t samples[], int size) {
    // Sort the sample array in ascending order
    for (int i = 0; i < size - 1; i++) {
        for (int j = i + 1; j < size; j++) {
            if (samples[j] < samples[i]) {
                uint16_t temp = samples[i];
                samples[i] = samples[j];
                samples[j] = temp;
            }
        }
    }
    // Index of the first and of the last quartile (25%)
    int firstQuartile = size / 4;
    int lastQuartile = firstQuartile * 3;

    // Sum the samples of the second and third quartiles (the middle 50%)
    float sum = 0.0;
    int count = 0;
    for (int i = firstQuartile; i < lastQuartile; i++) {
        sum += samples[i];
        count++;
    }
    // Return the mean of the second and third quartiles
    return sum / count;
}

void performSingleMeasurement() {

    uint16_t readings[READINGS_PER_SAMPLE][NUM_CHANNELS];

    for (int i = 0; i < READINGS_PER_SAMPLE; i++) {
        for (int j = 0; j < NUM_CHANNELS; j++) {
            readings[i][j] = 0;
        }
    }

    for (int i = 0; i < READINGS_PER_SAMPLE; i++) {
        uint16_t rawReadings[12];
        if (as7341.readAllChannels(rawReadings)) {
            const int channelMap[] = {0, 1, 2, 3, 6, 7, 8, 9}; // 415-680nm
            for (int j = 0; j < NUM_CHANNELS; j++) {
                readings[i][j] = rawReadings[channelMap[j]];
            }
        }
        delay(100);
    }

    float averages[NUM_CHANNELS];
    for (int i = 0; i < NUM_CHANNELS; i++) {
        averages[i] = 0.0;
    }

    for (int channel = 0; channel < NUM_CHANNELS; channel++) {
        uint16_t channelReadings[READINGS_PER_SAMPLE];
        for (int i = 0; i < READINGS_PER_SAMPLE; i++) {
            channelReadings[i] = 0;
        }
        for (int i = 0; i < READINGS_PER_SAMPLE; i++) {
            channelReadings[i] = readings[i][channel];
        }
        averages[channel] = trimmedMeanFilter(channelReadings, READINGS_PER_SAMPLE);
    }

    if (config.sampleType == 0) { // Raw
        for (int i = 0; i < NUM_CHANNELS; i++) {
            measurementData.channels[i] = averages[i];
        }
    } else { // Absorbance
        if (measurementData.currentSample == 0) {
            for (int i = 0; i < NUM_CHANNELS; i++) {
                whiteValues[i] = averages[i];
                measurementData.channels[i] = 0;
            }
        } else {
            for (int i = 0; i < NUM_CHANNELS; i++) {
                if (whiteValues[i] > 0 && averages[i] > 0) {
                    measurementData.channels[i] = -log10(averages[i] / whiteValues[i]);
                } else {
                    measurementData.channels[i] = 0;
                }
            }
        }
    }
}

void setupWebRoutes() {
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/html", webpage);
    });

    server.on("/setup", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (measurementData.inProgress) {
            request->send(409, "application/json", "{\"error\":\"Measurement in progress\"}");
            return;
        }

        if (!request->hasParam("gain") || !request->hasParam("current") ||
            !request->hasParam("samples") || !request->hasParam("sampleType")) {
            request->send(400, "application/json", "{\"error\":\"Incomplete parameters\"}");
            return;
        }

        int gain = request->getParam("gain")->value().toInt();
        int current = request->getParam("current")->value().toInt();
        int samples = request->getParam("samples")->value().toInt();
        int type = request->getParam("sampleType")->value().toInt();

        if(samples <= 0 || samples > MAX_SAMPLES){
            request->send(400, "application/json", "{\"error\":\"Invalid number of samples\"}");
            return;
        }
        if(type > 1 || type < 0){
            request->send(400, "application/json", "{\"error\":\"Invalid sample type\"}");
            return;
        }
        if(current < MIN_LED_CURRENT || current > MAX_LED_CURRENT){
            request->send(400, "application/json", "{\"error\":\"Invalid LED current\"}");
            return;
        }
        if(gain < MIN_GAIN || gain > MAX_GAIN){
            request->send(400, "application/json", "{\"error\":\"Invalid gain\"}");
            return;
        }

        config.gain = gain;
        config.current = current;
        config.numSamples = samples;
        config.sampleType = type;

        if (!configureSensor()) {
            request->send(500, "application/json", "{\"error\":\"Sensor configuration failed\"}");
            return;
        }

        measurementData.currentSample = 0;
        measurementData.isComplete = false;
        measurementData.inProgress = true;
        measurementData.positionReset = false;
        config.isValid = true;

        request->send(200, "application/json", "{\"status\":\"success\"}");
    });

    server.on("/data", HTTP_GET, [](AsyncWebServerRequest *request) {
        JsonDocument doc;

        if (!measurementData.inProgress) {
            doc["status"] = "idle";
            doc["isComplete"] = true;
        } else {
            doc["status"] = "measuring";
            doc["sampleNumber"] = measurementData.currentSample;
            doc["isComplete"] = measurementData.isComplete;

            doc["channels"] = JsonArray();
            JsonArray channels = doc["channels"].to<JsonArray>();
            for (int i = 0; i < NUM_CHANNELS; i++) {
                channels.add(measurementData.channels[i]);
            }
        }

        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });
}

void setup() {
    Serial.begin(115200);

    // Wi-Fi configuration
    WiFi.softAP(ssid, password);
    Serial.print("IP Address: ");
    Serial.println(WiFi.softAPIP());

    // Set up the web routes
    setupWebRoutes();

    // Start the server
    server.begin();

    // Sensor initialization - keeps retrying so the control page stays reachable
    while (!as7341.begin()) {
        Serial.println("AS7341 sensor initialization failed, check the wiring");
        delay(1000);
    }

    as7341.setATIME(100);
    as7341.setASTEP(999);
    as7341.enableLED(false);

    // Motor configuration
    pinMode(PHOTOINTERRUPTER_PIN, INPUT);
    // Motor pins configured as outputs
    pinMode(IN1, OUTPUT);
    pinMode(IN2, OUTPUT);
    pinMode(IN3, OUTPUT);
    pinMode(IN4, OUTPUT);

    stepper.setMaxSpeed(200);
    stepper.setAcceleration(40);
    stepper.setSpeed(20);

    // Coils start de-energized
    disableStepper();

    Serial.println("Smart Pad initialized and ready");
}

void loop() {
    if (config.isValid && measurementData.inProgress) {
        as7341.enableLED(true);
        enableStepper();
        if (!measurementData.positionReset) {
            resetPosition();
            moveToNextPosition(0);
            measurementData.positionReset = true;
            delay(500);
        } else if (measurementData.currentSample > 0 && measurementData.currentSample <= config.numSamples) {
            moveToNextPosition(measurementData.currentSample);
            delay(500);
        }
        if (measurementData.currentSample <= config.numSamples) {
            performSingleMeasurement();
            measurementData.isComplete = true;
            measurementData.currentSample++;
        } else {
            measurementData.inProgress = false;
            config.isValid = false;
            measurementData.positionReset = false;
            as7341.enableLED(false);
            disableStepper();
            Serial.println("Measurements finished");
        }
    }
}
