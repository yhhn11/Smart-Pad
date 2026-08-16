#include <Arduino.h>
#include <Adafruit_AS7341.h>
#include <AccelStepper.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ElegantOTA.h>

// Definições dos pinos
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

char ssid[] = "TP-Link_7201";
char password[] = "83345173";

const char* mqtt_server = "192.168.0.110";
const char* mqttUser = "yugo";
const char* mqttPassword = "1234";
const char* mqtt_id = "Smart_Pad01";
const int   mqtt_port = 1883;
const char  mqttTopic[] = "Smart/Pad01";
const char  mqttTopicSetup[] = "Smart/Pad01/Setup";
const char  mqttTopicDebug[] = "Smart/Pad01/Debug";
const char  mqttTopicNotifications[] = "Smart/Pad01/Notifications";

WiFiClient wifiClient;
PubSubClient client(wifiClient);

// Sistema OTA para Update web
AsyncWebServer server(80);
const char* userOTA = "smartufu";
const char* userPassWordOTA = "smartufu";

// Objetos globais
AccelStepper stepper(AccelStepper::FULL4WIRE, IN1, IN3, IN2, IN4);
Adafruit_AS7341 as7341;

// Estruturas de dados para configuração das medidas
struct MeasurementConfig {
    uint16_t gain = 1;
    uint16_t current = 100;
    uint16_t numSamples = 1;
    uint16_t sampleType = 0;
    bool isValid = false;
};

// Estruturas de dados para armazenar os dados das medidas
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
float filtroMediaMovel(uint16_t amostras[], int tamanho);
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
        client.publish(mqttTopicNotifications, "Falha ao inicializar sensor AS7341, verifique as conexões");
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

    String msg_str_ip;
    msg_str_ip =  "IP Smart_Pad_01 : " + WiFi.localIP().toString();
    client.publish(mqttTopicDebug, msg_str_ip.c_str());

    client.publish(mqttTopicNotifications, "Smart Pad 01 inicializado e pronto");
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
            // Monta string para publicar resultado
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
            client.publish(mqttTopicNotifications, "Medidas finalizadas");
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
    client.publish(mqttTopicDebug, "Home encontrado e posição zerada.");
}

void moveToNextPosition(int currentSample) {
    int passos;

    if(currentSample == 0){
        passos = - STEPS_TO_FIRST_CEL;
    }
    else if(currentSample == 1){
        passos = STEPS_TO_FIRST_SAMPLE;
    }
    else if(currentSample % 3 == 0 && currentSample < MAX_SAMPLES + 1){ 
        passos = STEPS_NEXT_CEL + STEPS_CORRECTION;
    }
    else{
        passos = STEPS_NEXT_CEL;
    }

    stepper.move(passos);
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

float filtroMediaMovel(uint16_t amostras[], int tamanho) {
    // Ordena o array de amostras em ordem crescente
    for (int i = 0; i < tamanho - 1; i++) {
        for (int j = i + 1; j < tamanho; j++) {
            if (amostras[j] < amostras[i]) {
                uint16_t temp = amostras[i];
                amostras[i] = amostras[j];
                amostras[j] = temp;
            }
        }
    }
    // Calcula o índice do primeiro e último quartil (25%)
    int primeiroQuartil = tamanho / 4;
    int ultimoQuartil = primeiroQuartil * 3;

    // Calcula a soma das amostras do segundo e terceiro quartil (50%)
    float soma = 0.0;
    int contagem = 0;
    for (int i = primeiroQuartil; i < ultimoQuartil; i++) {
        soma += amostras[i];
        contagem++;
    }
    // Calcula e retorna a média das amostras do segundo e terceiro quartil
    return soma / contagem;
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
        averages[channel] = filtroMediaMovel(channelReadings, READINGS_PER_SAMPLE);
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
        int tipo = 0, amostras = 0, ganho = 0, corrente = 0;
        int numlidos = sscanf(msg_mqtt, "%d,%d,%d,%d", &tipo, &amostras, &ganho, &corrente);
        if(numlidos == 4){
            if(amostras <= 0 || amostras > MAX_SAMPLES){
                client.publish(mqttTopicDebug, "Numero de amostras invalido");
                return;
            }
            if(tipo > 1 || tipo < 0){
                client.publish(mqttTopicDebug, "Tipo de amostra invalido");
                return;
            }
            if(corrente < MIN_LED_CURRENT || corrente > MAX_LED_CURRENT){
                client.publish(mqttTopicDebug, "Corrente invalida");
                return;
            }
            if(ganho < MIN_GAIN || ganho > MAX_GAIN){
                client.publish(mqttTopicDebug, "Ganho invalido");
                return;
            }
            config.gain = ganho;
            config.current = corrente;
            config.numSamples = amostras;
            config.sampleType = tipo;
            if (!configureSensor()) {
                client.publish(mqttTopicDebug, "Falha ao configurar sensor");
                return;
            }
            measurementData.currentSample = 0;
            measurementData.isComplete = false;
            measurementData.inProgress = true;
            measurementData.positionReset = false;
            config.isValid = true;
            client.publish(mqttTopicNotifications, "Setup OK, iniciando medição");
        } else {
            client.publish(mqttTopicNotifications, "Setup não reconhecido!");
        }
    }
}
