#include <Arduino.h>
#include <Adafruit_AS7341.h>
#include <AccelStepper.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ElegantOTA.h>
#include "config.h"

// Pin definitions
#define IN1 27
#define IN2 25
#define IN3 33
#define IN4 32
#define PHOTOINTERRUPTER_PIN 18

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

char ssid[] = WIFI_SSID;
char password[] = WIFI_PASSWORD;

const char* mqtt_server = MQTT_SERVER;
const char* mqttUser = MQTT_USER;
const char* mqttPassword = MQTT_PASSWORD;
const char* mqtt_id = MQTT_CLIENT_ID;
const int   mqtt_port = MQTT_PORT;
const char  mqttTopic[] = MQTT_TOPIC_DATA;
const char  mqttTopicSetup[] = MQTT_TOPIC_SETUP;
const char  mqttTopicDebug[] = MQTT_TOPIC_DEBUG;
const char  mqttTopicNotifications[] = MQTT_TOPIC_NOTIFICATIONS;

WiFiClient wifiClient;
PubSubClient client(wifiClient);

// OTA system for web-based firmware updates
AsyncWebServer server(80);
const char* userOTA = OTA_USER;
const char* userPassWordOTA = OTA_PASSWORD;

// Global objects
AccelStepper stepper(AccelStepper::FULL4WIRE, IN1, IN3, IN2, IN4);
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

MeasurementConfig config;
MeasurementData measurementData;
float whiteValues[NUM_CHANNELS] = {0};

void enableStepper();
void disableStepper();
void resetPosition();
void moveToNextPosition(int currentSample);
bool configureSensor();
float trimmedMeanFilter(uint16_t samples[], int size);
void performSingleMeasurement();
void mqtt_callback(char* topic, byte* payload, unsigned int length);

void setup() {

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    uint8_t attemptsWifi = 0;
    while (WiFi.status() != WL_CONNECTED){
        delay(500);
        attemptsWifi++;
        if (attemptsWifi > 20) {
            ESP.restart();
        }
    }

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "Smart Pad 01");
    });
    ElegantOTA.setAutoReboot(true);
    ElegantOTA.setAuth(userOTA, userPassWordOTA);
    ElegantOTA.begin(&server);
    server.begin();

    client.setServer(mqtt_server, mqtt_port);
    client.setKeepAlive(3600);
    client.setCallback(mqtt_callback);
    while(!client.connected()){
        client.connect(mqtt_id, mqttUser, mqttPassword);
        delay(5000);
    }
    client.subscribe(mqttTopicSetup);

    while(!as7341.begin()) {
        client.publish(mqttTopicNotifications, "AS7341 sensor initialization failed, check the wiring");
    }
    as7341.setATIME(100);
    as7341.setASTEP(999);
    as7341.enableLED(false);

    pinMode(PHOTOINTERRUPTER_PIN, INPUT);
    pinMode(IN1, OUTPUT);
    pinMode(IN2, OUTPUT);
    pinMode(IN3, OUTPUT);
    pinMode(IN4, OUTPUT);

    stepper.setMaxSpeed(200);
    stepper.setAcceleration(40);
    stepper.setSpeed(20);

    disableStepper();

    String ipMessage;
    ipMessage =  "IP Smart_Pad_01 : " + WiFi.localIP().toString();
    client.publish(mqttTopicDebug, ipMessage.c_str());

    client.publish(mqttTopicNotifications, "Smart Pad 01 initialized and ready");
}

void loop() {
    client.loop();
    ElegantOTA.loop();
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
            // Build the result string that gets published
            String result = String(measurementData.currentSample);
            for (int i = 0; i < NUM_CHANNELS; i++) {
                result += "," + String(measurementData.channels[i], 3);
            }
            client.publish(mqttTopic, result.c_str());
            measurementData.isComplete = true;
            measurementData.currentSample++;
        } else {
            measurementData.inProgress = false;
            config.isValid = false;
            measurementData.positionReset = false;
            as7341.enableLED(false);
            disableStepper();
            client.publish(mqttTopicNotifications, "Measurements finished");
        }
    }
}

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
    client.publish(mqttTopicDebug, "Home position found, step counter reset.");
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

void mqtt_callback(char* topic, byte* payload, unsigned int length) {

    char msg_mqtt[length + 1];
    memcpy(msg_mqtt, payload, length);
    msg_mqtt[length] = '\0';

    if(strcmp(topic, mqttTopicSetup) == 0){
        int type = 0, samples = 0, gain = 0, current = 0;
        int parsedFields = sscanf(msg_mqtt, "%d,%d,%d,%d", &type, &samples, &gain, &current);
        if(parsedFields == 4){
            if(samples <= 0 || samples > MAX_SAMPLES){
                client.publish(mqttTopicDebug, "Invalid number of samples");
                return;
            }
            if(type > 1 || type < 0){
                client.publish(mqttTopicDebug, "Invalid sample type");
                return;
            }
            if(current < MIN_LED_CURRENT || current > MAX_LED_CURRENT){
                client.publish(mqttTopicDebug, "Invalid LED current");
                return;
            }
            if(gain < MIN_GAIN || gain > MAX_GAIN){
                client.publish(mqttTopicDebug, "Invalid gain");
                return;
            }
            config.gain = gain;
            config.current = current;
            config.numSamples = samples;
            config.sampleType = type;
            if (!configureSensor()) {
                client.publish(mqttTopicDebug, "Sensor configuration failed");
                return;
            }
            measurementData.currentSample = 0;
            measurementData.isComplete = false;
            measurementData.inProgress = true;
            measurementData.positionReset = false;
            config.isValid = true;
            client.publish(mqttTopicNotifications, "Setup OK, starting measurement");
        } else {
            client.publish(mqttTopicNotifications, "Setup message not recognized!");
        }
    }
}
