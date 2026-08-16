#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <Adafruit_AS7341.h>
#include <AccelStepper.h>
#include <ArduinoJson.h>
#include "webpage.h"

// Definições dos pinos
#define IN1 27
#define IN2 25
#define IN3 33
#define IN4 32
#define ENDSTOP_PIN 18

// Constantes de configuração
#define MIN_LED_CURRENT 4
#define MAX_LED_CURRENT 258
#define MIN_GAIN 1
#define MAX_GAIN 512
#define MAX_SAMPLES 17
#define NUM_CHANNELS 8
#define READINGS_PER_SAMPLE 10
#define STEPS_TO_FIRST_CEL 115
#define STEPS_NEXT_CEL 85
#define STEPS_CORRECTION 3

// Configuração WiFi
const char* ssid = "SmartPad_Network";
const char* password = "smarteza";

// Objetos globais

AccelStepper stepper(AccelStepper::FULL4WIRE, IN1, IN3, IN2, IN4);
AsyncWebServer server(80);
Adafruit_AS7341 as7341;

// Estruturas de dados
struct MeasurementConfig {
    uint16_t gain = 1;
    uint16_t current = 100;
    uint16_t numSamples = 1;
    uint16_t sampleType = 0;
    bool isValid = false;
};

struct MeasurementData {
    float channels[NUM_CHANNELS];
    int currentSample = 0;
    bool isComplete = false;
    bool inProgress = false;
    bool positionReset = false;
};

// Variáveis globais
MeasurementConfig config;
MeasurementData measurementData;
float whiteValues[NUM_CHANNELS] = {0};

// Funções auxiliares
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
    //enableStepper();
    while (digitalRead(ENDSTOP_PIN) == HIGH) {
        stepper.moveTo(-5000);
        stepper.run();
    }
    stepper.setCurrentPosition(0);
    //disableStepper();
    measurementData.positionReset = true;
}

void moveToNextPosition(int currentSample) {
    int passos;
    
    if(currentSample == 0){
        passos = STEPS_TO_FIRST_CEL;
    }
    else if(currentSample % 3 == 0 && currentSample < 18){ 
        passos = STEPS_NEXT_CEL + STEPS_CORRECTION;
    }
    else{
        passos = STEPS_NEXT_CEL;
    }

    //enableStepper();
    
    stepper.move(passos);
    while (stepper.distanceToGo() != 0) {
        stepper.run();
    }

    //disableStepper();

}

float calculateAverage(uint16_t readings[], int size) {
    float sum = 0;
    int validReadings = 0;
    
    for (int i = 0; i < size; i++) {
        if (readings[i] > 0) {
            sum += readings[i];
            validReadings++;
        }
    }
    
    return validReadings > 0 ? sum / validReadings : 0;
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

void performSingleMeasurement() {
    uint16_t readings[READINGS_PER_SAMPLE][NUM_CHANNELS] = {0};
    
    // Realizar as leituras
    for (int i = 0; i < READINGS_PER_SAMPLE; i++) {
        uint16_t rawReadings[12];
        if (as7341.readAllChannels(rawReadings)) {
            const int channelMap[] = {0, 1, 2, 3, 6, 7, 8, 9}; // 415-680nm
            for (int j = 0; j < NUM_CHANNELS; j++) {
                readings[i][j] = rawReadings[channelMap[j]];
            }
        }
        delay(50);
    }

    // Calcular médias
    float averages[NUM_CHANNELS];
    for (int channel = 0; channel < NUM_CHANNELS; channel++) {
        uint16_t channelReadings[READINGS_PER_SAMPLE];
        for (int i = 0; i < READINGS_PER_SAMPLE; i++) {
            channelReadings[i] = readings[i][channel];
        }
        averages[channel] = calculateAverage(channelReadings, READINGS_PER_SAMPLE);
    }

    // Processar os resultados baseado no tipo de amostra
    if (config.sampleType == 0) { // Raw
        for (int i = 0; i < NUM_CHANNELS; i++) {
            measurementData.channels[i] = averages[i];
        }
    } else { // Absorbance
        if (measurementData.currentSample == 0) {
            // Armazenar valores em branco (primeira medida)
            for (int i = 0; i < NUM_CHANNELS; i++) {
                whiteValues[i] = averages[i];
                measurementData.channels[i] = 0; // Primeira leitura mostra zero
            }
        } else {
            // Calcular absorbância
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
            request->send(409, "application/json", "{\"error\":\"Medição em andamento\"}");
            return;
        }

        if (!request->hasParam("gain") || !request->hasParam("current") ||
            !request->hasParam("samples") || !request->hasParam("sampleType")) {
            request->send(400, "application/json", "{\"error\":\"Parâmetros incompletos\"}");
            return;
        }

        config.gain = request->getParam("gain")->value().toInt();
        config.current = request->getParam("current")->value().toInt();
        config.numSamples = request->getParam("samples")->value().toInt();
        config.sampleType = request->getParam("sampleType")->value().toInt();

        if (!configureSensor()) {
            request->send(500, "application/json", "{\"error\":\"Falha ao configurar sensor\"}");
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

    

    // Inicialização do sensor
    if (!as7341.begin()) {
        Serial.println("Falha ao inicializar sensor AS7341");
        return;
    }

    as7341.setATIME(100);
    as7341.setASTEP(999);
    as7341.enableLED(false);

    // Configuração WiFi
    WiFi.softAP(ssid, password);
    Serial.print("IP Address: ");
    Serial.println(WiFi.softAPIP());

    // Configura as rotas web
    setupWebRoutes();
    
    // Inicia o servidor
    server.begin();

    // Configuração do motor
    pinMode(ENDSTOP_PIN, INPUT_PULLUP);
    // Configuração dos pinos do motor como saída
    pinMode(IN1, OUTPUT);
    pinMode(IN2, OUTPUT);
    pinMode(IN3, OUTPUT);
    pinMode(IN4, OUTPUT);

    stepper.setMaxSpeed(200);
    stepper.setAcceleration(40);
    stepper.setSpeed(10);
    
    // Inicialmente desabilita as bobinas
    disableStepper();

    
}

void loop() {
    if (config.isValid && measurementData.inProgress) {
      as7341.enableLED(true);
      enableStepper();
        if (!measurementData.positionReset) {
            resetPosition();
            moveToNextPosition(0);  // Primeira posição
            measurementData.positionReset = true;
            delay(500);

        } else if (measurementData.currentSample > 0 && measurementData.currentSample <= config.numSamples) {
            moveToNextPosition(measurementData.currentSample);
            delay(500);
        }

        if (measurementData.currentSample <= config.numSamples) {
            delay(10000); // Aguarda 10 segundos antes da próxima amostra
            // Realiza a medição apenas após o movimento estar completo
            performSingleMeasurement();
            measurementData.isComplete = true;
            measurementData.currentSample++;

            
        } else {
            measurementData.inProgress = false;
            config.isValid = false;
            measurementData.positionReset = false;
            as7341.enableLED(false);    
            disableStepper();
        }
    }
}