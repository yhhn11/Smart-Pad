#include <Wire.h>
#include <Adafruit_AS7341.h>
#include <AccelStepper.h>
#include <WiFi.h>
#include <WiFiManager.h>  
#include <HTTPClient.h>

#define dirPin 27
#define stepPin 26
#define enablePin 25

#define endStopPin 33


String ID_script = "";
String ready_str = ""; 
String gain_str = "";
String currentLed_str = "";
String samples_str = "";
String ReadingType = "";

Adafruit_AS7341 as7341;

String inputString = "";      

int count;

bool flagDone = true, flagId = false,ready = false;

AccelStepper stepper(1, stepPin, dirPin);

void setup() {

  pinMode(enablePin, OUTPUT);
  digitalWrite(enablePin, HIGH);
  // initialize serial and wait for HMI confirmation command
  Serial.begin(115200);
  //delay(5000);
  while(!ready){
    ready_str = Serial.readStringUntil(';');
    if(ready_str == "tdok"){
      ready = true;
      Serial.print("espok@");
    }
    //delay(50);
    ready_str = "";
  }
  // setup for WiFi
  WiFiManager wm;
  wm.setDebugOutput(false);
  //wm.resetSettings();
  bool res;
  res = wm.autoConnect("Smart Table","smart7923"); // password protected ap

    if(WiFi.status() != WL_CONNECTED || !res) {
        Serial.print("wifinotok@");
        // ESP.restart();
    } 
    else {
        //if you get here you have connected to the WiFi    
        Serial.print("wifiok@");
    }
  // setup for Google sheets 
  while(!flagId){
    ID_script = Serial.readStringUntil('#');
    if(ID_script.length() > 10){
      flagId = true;
      Serial.print("idscriptok@");
      }
    }
  //Sensor startup    
  while(!as7341.begin()){
    Serial.print("asnotfound@");
    delay(500);
  }
  as7341.setATIME(100);
  as7341.setASTEP(999);
  as7341.setGain(AS7341_GAIN_4X);
  as7341.enableLED(true); 
  as7341.setLEDCurrent(10);
  //configure the enable and limit switch pins
  pinMode(endStopPin, INPUT_PULLUP);
  
  // enable stepper motor
  digitalWrite(enablePin, LOW);
  
  // Define the motor speed
  stepper.setMaxSpeed(300);
  stepper.setAcceleration(20);
  stepper.setSpeed(50);
}

void resetPosition(){
    // Move o motor até que o interruptor de fim de curso seja acionado
  while (digitalRead(endStopPin) == HIGH) {
    stepper.moveTo(5000);
    stepper.run();
  }
  
  // Reseta a posição do motor
  stepper.setCurrentPosition(0); 
}

void moveStepper(int cells){
  
  int steps = cells * 20;
  
  if (steps != 0) { 
  // Move o motor para a posição correspondente ao número digitado
  stepper.moveTo(-steps);

  // Loop para mover o motor de passo gradualmente para a posição desejada
  while (stepper.distanceToGo() != 0) {
      stepper.run();
    }
  }
}

void doReadings(){
  resetPosition();
  int turns = samples_str.toInt();
  int count = 1;
  float storeReadings[turns] [8] = {{0}} ;
  float whiteValues[8] = {0};
  for(count;count<turns + 1;count++){
    moveStepper(count);
    for(int i=0;i<10;i++){
      if (!as7341.readAllChannels()){
        Serial.print("erroras@"); 
      }
      storeReadings[count][0] += as7341.getChannel(AS7341_CHANNEL_415nm_F1);
      storeReadings[count][1] += as7341.getChannel(AS7341_CHANNEL_445nm_F2);
      storeReadings[count][2] += as7341.getChannel(AS7341_CHANNEL_480nm_F3);
      storeReadings[count][3] += as7341.getChannel(AS7341_CHANNEL_515nm_F4);
      storeReadings[count][4] += as7341.getChannel(AS7341_CHANNEL_555nm_F5);
      storeReadings[count][5] += as7341.getChannel(AS7341_CHANNEL_590nm_F6);
      storeReadings[count][6] += as7341.getChannel(AS7341_CHANNEL_630nm_F7);
      storeReadings[count][7] += as7341.getChannel(AS7341_CHANNEL_680nm_F8);
      delay(500);
    }
    storeReadings[count] [0] = storeReadings[count] [0] / 10;
    storeReadings[count] [1] = storeReadings[count] [1] / 10;
    storeReadings[count] [2] = storeReadings[count] [2] / 10;
    storeReadings[count] [3] = storeReadings[count] [3] / 10;
    storeReadings[count] [4] = storeReadings[count] [4] / 10;
    storeReadings[count] [5] = storeReadings[count] [5] / 10;
    storeReadings[count] [6] = storeReadings[count] [6] / 10;
    storeReadings[count] [7] = storeReadings[count] [7] / 10; 

    if(ReadingType == "ABSORBANCE"){
      if(count == 1){
        whiteValues[0] = storeReadings[count][0];
        whiteValues[1] = storeReadings[count][1];
        whiteValues[2] = storeReadings[count][2];
        whiteValues[3] = storeReadings[count][3];
        whiteValues[4] = storeReadings[count][4];
        whiteValues[5] = storeReadings[count][5];
        whiteValues[6] = storeReadings[count][6];
        whiteValues[7] = storeReadings[count][7];
      }
      storeReadings[count][0] = -log10(storeReadings[count][0]/whiteValues[0]);
      storeReadings[count][1] = -log10(storeReadings[count][1]/whiteValues[1]);
      storeReadings[count][2] = -log10(storeReadings[count][2]/whiteValues[2]);
      storeReadings[count][3] = -log10(storeReadings[count][3]/whiteValues[3]);
      storeReadings[count][4] = -log10(storeReadings[count][4]/whiteValues[4]);
      storeReadings[count][5] = -log10(storeReadings[count][5]/whiteValues[5]);
      storeReadings[count][6] = -log10(storeReadings[count][6]/whiteValues[6]);
      storeReadings[count][7] = -log10(storeReadings[count][7]/whiteValues[7]);
    }

    if (WiFi.status() == WL_CONNECTED)
    {
    String urlfinal = "https://script.google.com/macros/s/" + ID_script + "/exec?val1=" + String(storeReadings[count][0]) + "&val2=" + String(storeReadings[count][1]) + "&val3=" + String(storeReadings[count][2]) + "&val4=" + String(storeReadings[count][3]) + "&val5=" + String(storeReadings[count][4]) + "&val6=" + String(storeReadings[count][5]) + "&val7=" + String(storeReadings[count][6]) + "&val8=" + String(storeReadings[count][7]);
    HTTPClient http;
    http.begin(urlfinal.c_str());
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    int httpCode = http.GET();
    http.end();
    }
    delay(300);   
  }  
}

void setLedCurrent(){
  int LedValue = currentLed_str.toInt();
  if(LedValue < 101){
    as7341.setLEDCurrent(LedValue);    
  }
}

void setGain(){
  String strGain = gain_str.substring(0, gain_str.length() - 1);
  int gainValue = strGain.toInt();

  switch (gainValue){
    
    case 1:
    as7341.setGain(AS7341_GAIN_1X);
    break;

    case 2:
    as7341.setGain(AS7341_GAIN_2X);
    break;
    
    case 4:
    as7341.setGain(AS7341_GAIN_4X);
    break;

    case 8:
    as7341.setGain(AS7341_GAIN_8X);
    break;

    case 16:
    as7341.setGain(AS7341_GAIN_16X);
    break;

    case 32:
    as7341.setGain(AS7341_GAIN_32X);
    break;

    case 64:
    as7341.setGain(AS7341_GAIN_64X);
    break;

    case 128:
    as7341.setGain(AS7341_GAIN_128X);
    break;

    case 256:
    as7341.setGain(AS7341_GAIN_256X);
    break;

    case 512:
    as7341.setGain(AS7341_GAIN_512X);
    break;

    default:
    Serial.println("Invalido");
  }
}

void breakMesssage() {
  int scIndex1 = inputString.indexOf(',');
  int scIndex2 = inputString.indexOf(',', scIndex1 + 1);
  int scIndex3 = inputString.indexOf(',', scIndex2 + 1);

  samples_str = inputString.substring(0, scIndex1);
  gain_str = inputString.substring(scIndex1 + 1, scIndex2);
  currentLed_str = inputString.substring(scIndex2 + 1, scIndex3);
  ReadingType = inputString.substring(scIndex3 + 1);

}


void loop() {
  if (Serial.available() && flagDone) {
    flagDone = false;
    inputString = Serial.readStringUntil('*');
    inputString.trim();
    if(inputString.length() > 3){
      breakMesssage();
      setLedCurrent();
      setGain();
      doReadings();
    }
  }
  flagDone = true;
}
