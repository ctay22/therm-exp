/*
AUTHOR: Charles Taylor
DATE: 28JAN2025
PURPOSE: Stream data from a qwiic Thermocouple amplifier via ESP32-S2 WROOM Thing Plus and MQTT. Updated topics and simplified data push.
Hardware: Thing Plus - Thermocouple Amplifier - Omega TC

Sources:  https://learn.sparkfun.com/tutorials/esp32-s2-thing-plus-hookup-guide/arduino-examples
            - WIFI Connection
          Arduino Examples
            - MQTT>WiFiSimpleReceiveCallback
            - MQTT>WiFiSimpleSenderCallback
          NTP Time Client: https://github.com/arduino-libraries/NTPClient
          ESP32 Timers: https://github.com/natnqweb/Simpletimer

*/

#include <ArduinoMqttClient.h>
#include <WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <SparkFun_MCP9600.h>
#include <Simpletimer.h>              // https://github.com/natnqweb/Simpletimer
#include <ArduinoJson.h>

//JSON PACKET INITIALIZE; need to do this before ISR/Callbacks 
//estimate packet sizes: https://arduinojson.org/v6/assistant/#/step2
StaticJsonDocument<3000> doc;


// WiFi network name and password:
const char * networkName = "TaylorLab";
const char * networkPswd = "asdfghjkl";
const int LED_PIN = 13;

// MQTT SETUP
WiFiClient wifiClient;
WiFiUDP ntpUDP;
MqttClient mqttClient(wifiClient);
NTPClient timeClient(ntpUDP);

const char broker[] = "test.mosquitto.org";
int        port     = 1883;
const char MQTT_Topic[]  = "thermal_exp/measurements";
const char MQTT_Control[]  = "thermal_exp/controls";
char tc1String[24];
int Tx_Blink = 0;

// QWIIC THERMOCOUPLE SETUP
MCP9600 tempSensor;
Shutdown_Mode mode = NORMAL;
Burst_Sample samples = SAMPLES_1;

//--------- TIMERS -----------
Simpletimer multicb{};
bool state = false;
int read_iter = 0;
int start_time = 0;

void callback1();
void callback2();
void callback3();

void callback1() {
 doc["data"][read_iter]=tempSensor.getThermocoupleTemp();
 doc["time"][read_iter]=millis();
 read_iter++;

 if (read_iter > 9) {read_iter=0;}
}

void callback2() {
  Serial.print(Tx_Blink);
  Serial.println(" Streaming");
}

void callback3() {
  mqttClient.poll();

  digitalWrite(LED_PIN, Tx_Blink);
  Tx_Blink = (Tx_Blink + 1) % 2; // Flip ledState
  
  doc["ambient"] = tempSensor.getAmbientTemp();

  //start_time = millis();
  mqttClient.beginMessage(MQTT_Topic);
  serializeJson(doc, mqttClient);
  mqttClient.endMessage();

  //Serial.println(millis()-start_time);
}


static const unsigned int number_of_callbacks = 3;
static Simpletimer::callback all_callbacks[number_of_callbacks]
{
  callback1,  //--  1
  callback2,  //--  2
  callback3   //--  3
};
static unsigned long timers[number_of_callbacks]
{
  50,  //callback1   -- 1
  250,  //callback2   -- 2
  2000  //callback3   -- 3
};

//----------SETUP AND LOOP STRUCTURES --------------
void setup(){
  Serial.begin(115200);

  pinMode(LED_PIN,OUTPUT);

  //Connect to thermocouple board (no MUX)
  connectToTC1();

  // Connect to the WiFi network (see function below loop)
  connectToWiFi(networkName, networkPswd);

  // Connect and Subscribe to MQTT Broker
  connectToMQTT();

  //SimpleTimer
  multicb.register_multiple_callbacks(all_callbacks, timers, number_of_callbacks);

  //Establish which sensor belongs to which JSON DOC
  doc["sensor"] = "tc1";

  //Getting time of the start of run and parameters for the experiment, post to MQTT topic
  timeClient.begin();
  timeClient.update();
  Serial.print('Linux EpochTime: ');
  Serial.print(timeClient.getEpochTime());
  Serial.print(".");
  Serial.println(millis());

  mqttClient.beginMessage("test/info");
  mqttClient.print("StartEpochTime: ");
  mqttClient.print(timeClient.getEpochTime());
  mqttClient.print(".");
  mqttClient.println(millis());
  mqttClient.println("TC Board Settings: ");
  mqttClient.print("1. Filter Coeff: ");
  mqttClient.println(tempSensor.getFilterCoefficient());
  mqttClient.print("2. Thermocouple Type: ");
  mqttClient.println(tempSensor.getThermocoupleType());
  mqttClient.endMessage();
  
}

void loop(){ //print the thermocouple, ambient and delta temperatures every 200ms if available

  //Callback structure run
  multicb.run();

}

//---------------- PRIVATE FUNCTIONS ----------------------
void connectToWiFi(const char * ssid, const char * pwd)
{
  int ledState = 0;

  Serial.println("Connecting to WiFi network: " + String(ssid));

  WiFi.begin(ssid, pwd);

  while (WiFi.status() != WL_CONNECTED) 
  {
    // Blink LED while we're connecting:
    digitalWrite(LED_PIN, ledState);
    ledState = (ledState + 1) % 2; // Flip ledState
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void connectToMQTT()
{
  
  Serial.print("Attempting to connect to the MQTT broker: ");
  Serial.println(broker);

  if (!mqttClient.connect(broker, port)) {
    Serial.print("MQTT connection failed! Error code = ");
    Serial.println(mqttClient.connectError());

    while (1);
  }

  Serial.println("You're connected to the MQTT broker!");
  Serial.println();

  // set the message receive callback
  mqttClient.onMessage(onMqttMessage);

  Serial.print("Subscribing to topic: ");
  Serial.println(MQTT_Control);
  Serial.println();

  // subscribe to a topic
  mqttClient.subscribe(MQTT_Control);

  // topics can be unsubscribed using:
  // mqttClient.unsubscribe(MQTT_Control);

  Serial.print("Waiting for messages on topic: ");
  Serial.println(MQTT_Control);
  Serial.println();
}

void connectToTC1(void)
{
  Wire.begin();
  
  // https://forum.arduino.cc/t/changing-i2c-clock-speed/358413/10
  // https://github.com/adafruit/Adafruit_CircuitPython_MCP9600/issues/10
  // Wire.setClock(100000);
  Wire.setClock(50000);
  tempSensor.begin();       // Uses the default address (0x60) for SparkFun Thermocouple Amplifier

  //check if the sensor is connected
  if(tempSensor.isConnected()){
      Serial.println("Device will acknowledge!");
  }
  else {
      Serial.println("Device did not acknowledge! Freezing.");
      while(1); //hang forever
  }

  //check if the Device ID is correct
  if(tempSensor.checkDeviceID()){
      Serial.print("Device ID: ");
      Serial.print(tempSensor.checkDeviceID());
      Serial.print(", Filter Coefficient: ");
      Serial.println(tempSensor.getFilterCoefficient());
  }
  else {
      Serial.println("Device ID is not correct! Freezing.");
      while(1);
  }

  tempSensor.setBurstSamples(samples); 
  tempSensor.setShutdownMode(mode);
  tempSensor.setThermocoupleResolution(RES_14_BIT);
  tempSensor.setAmbientResolution(RES_ZERO_POINT_0625);
  tempSensor.setThermocoupleType(TYPE_K);
}

void onMqttMessage(int messageSize) {
  // we received a message, print out the topic and contents
  Serial.println("Received a message with topic '");
  Serial.print(mqttClient.messageTopic());
  Serial.print("', length ");
  Serial.print(messageSize);
  Serial.println(" bytes:");

  // use the Stream interface to print the contents
  while (mqttClient.available()) {
    Serial.print((char)mqttClient.read());
  }
  Serial.println();

  Serial.println();
}
