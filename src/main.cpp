#include <Arduino.h>

#include <header.h>

#include <SPI.h>
#include <WiFi.h>
#include <LoRa.h>
#include <SPIFFS.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <ESP32Servo.h>
#include <ESPAsyncWebServer.h>

#define ss     5
#define rst   14
#define dio0   2
#define servo 13

// 433E6 for Asia
// 866E6 for Europe
// 915E6 for North America
#define BAND 433E6

const char* ssid     = MY_SSID;
const char* password = MY_PASSWORD;

Servo myservo;

byte MasterNode = 0xFF;
byte Node1      = NODE1_LORA_ADDRESS;
byte Node2      = 0x02;

// Define NTP Client to get time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "tw.pool.ntp.org");

String formattedDate;
String day;
String hour;
String timestamp;

String SenderNode = "";
String outgoing;

byte msgCount   = 0;
String incoming = "";

bool isWatering = false;

// Tracks the time since last event fired
unsigned long     previousMillis = 0;
unsigned long int previoussecs   = 0;
unsigned long int currentsecs    = 0;
unsigned long     currentMillis  = 0;
int interval = 1;
int Secs     = 0;

String rssi;
String loRaMessage;
String temperature;
String humidity;
String pressure;
String readingID;
String moisture;

AsyncWebServer server(80);

// define functions
void startLoRA();
void connectWiFi();
void getTimeStamp();
void getLoRaData();
void sendMessage(String outgoing, byte MasterNode, byte otherNode);
void onReceive(int packetSize);

String getValue(String data, char separator, int index);
String processor(const String& var);

void setup() {
    Serial.begin(115200);
    while (!Serial);

    Serial.println("LoRa For ESP32!!");

    startLoRA();
    connectWiFi();
    
    if(!SPIFFS.begin()){
        Serial.println("An Error has occurred while mounting SPIFFS");
        return;
    }
    // Route for root / web page
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(SPIFFS, "/index.html", String(), false, processor);
    });
    server.on("/temperature", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send_P(200, "text/plain", temperature.c_str());
    });
    server.on("/humidity", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send_P(200, "text/plain", humidity.c_str());
    });
    server.on("/moisture", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send_P(200, "text/plain", moisture.c_str());
    });
    server.on("/timestamp", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send_P(200, "text/plain", timestamp.c_str());
    });
    server.on("/rssi", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send_P(200, "text/plain", rssi.c_str());
    });
    server.on("/winter", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(SPIFFS, "/winter.jpg", "image/jpg");
    });
    server.on("/watering", HTTP_GET, [](AsyncWebServerRequest *request){
        Serial.println("Watering ON");
        isWatering = true;
        request->send_P(200, "text/plain", "Watering ON Susses");
    });
    server.on("/feeding", HTTP_GET, [](AsyncWebServerRequest *request){
        Serial.println("Feeding ON");
        myservo.attach(servo);
        myservo.write(45);
        delay(1000);
        myservo.detach();
        request->send_P(200, "text/plain", "Feeding ON Susses");
    });

    server.begin();
    
    // Initialize a NTPClient to get time
    timeClient.begin();
    timeClient.setTimeOffset(28800);

}

void loop() {
    currentMillis = millis();
    currentsecs = currentMillis / 1000;
    if((unsigned long)(currentsecs - previoussecs) >= interval) {
        Secs = Secs + 1;
        // Serial.println(Secs);
        if( Secs >= 10 ) {
            Secs = 0;
        }

        if( (Secs % 2 ) == 0 ) {
            String message;
            if(isWatering) {
                message = "10,1";
                isWatering = false;
            } else {
                message = "10,0";
            }
            sendMessage(message, MasterNode, Node1);
        }

        // if( (Secs >= 6 ) && (Secs <= 10)) {
        //     String message = "20";
        //     String message;
        //     if(isWatering) {
        //         message = "10,1";
        //         isWatering = false;
        //     } else {
        //         message = "10,0";
        //     }
        //     sendMessage(message, MasterNode, Node2);
        // }

        previoussecs = currentsecs;
    } 

    getTimeStamp();
    onReceive(LoRa.parsePacket());

    delay(30);
}

void startLoRA(){
    int counter;

    LoRa.setPins(ss, rst, dio0);
    
    while(!LoRa.begin(BAND) && counter < 10) {
        Serial.print(".");
        counter++;
        delay(500);
    }
    if(counter == 10) {
        Serial.println("Starting LoRa failed!"); 
    }
    Serial.println("LoRa Master Initialization OK!");
}

void connectWiFi() {
    Serial.print("Connecting to ");
    Serial.println(ssid);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    Serial.println("");
    Serial.println("WiFi connected.");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
}

String processor(const String& var) {
    if(var == "TEMPERATURE") {
        return temperature;
    }
    else if(var == "HUMIDITY") {
        return humidity;
    }
    else if(var == "PRESSURE") {
        return pressure;
    }
    else if(var == "MOISTURE") {
        return moisture;
    }
    else if(var == "TIMESTAMP") {
        return timestamp;
    }
    else if(var == "RSSI") {
        return String(rssi);
    }
    return String();
}

void getTimeStamp() {
    // while(!timeClient.update()) {
    //     timeClient.forceUpdate();
    // }

    timeClient.update();
    formattedDate = timeClient.getFormattedTime();
    timestamp = day + " " + timeClient.getHours() + ":" + timeClient.getMinutes() + ":" + timeClient.getSeconds();
}

void getLoRaData() {
    Serial.print("Lora packet received: ");
    
    while (LoRa.available()) {
        String LoRaData = LoRa.readString();
        // LoRaData format: readingID/temperature,humidity,moisture,rssi
        // String example: 1/27.43,654,95.34,-110
        Serial.print(LoRaData); 
        
        // decode the received data
        int pos1 = LoRaData.indexOf('/');
        int pos2 = LoRaData.indexOf('&');
        int pos3 = LoRaData.indexOf('#');
        readingID = LoRaData.substring(0, pos1);
        temperature = LoRaData.substring(pos1 +1, pos2);
        humidity = LoRaData.substring(pos2+1, pos3);
        pressure = LoRaData.substring(pos3+1, LoRaData.length());    
    }

    rssi = LoRa.packetRssi();
    Serial.print(" with RSSI ");    
    Serial.println(rssi);
}

String getValue(String data, char separator, int index) {
    int found = 0;
    int strIndex[] = { 0, -1 };
    int maxIndex = data.length() - 1;

    for (int i = 0; i <= maxIndex && found <= index; i++) {
        if (data.charAt(i) == separator || i == maxIndex) {
            found++;
            strIndex[0] = strIndex[1] + 1;
            strIndex[1] = (i == maxIndex) ? i + 1 : i;
        }
    }
    return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}

void sendMessage(String outgoing, byte MasterNode, byte otherNode) {
    // Serial.println("Sending " + outgoing);
    LoRa.beginPacket();
    LoRa.write(otherNode);
    LoRa.write(MasterNode);
    LoRa.write(msgCount);
    LoRa.write(outgoing.length());
    LoRa.print(outgoing);
    LoRa.endPacket();
    msgCount++;
}

void onReceive(int packetSize) {
    if (packetSize == 0) return; 
            
    int recipient = LoRa.read();
    byte sender = LoRa.read();
    if ( sender == 0X01 )
        SenderNode = "Node1";
    if ( sender == 0X02 )
        SenderNode = "Node2";
    byte incomingMsgId = LoRa.read();
    byte incomingLength = LoRa.read();

    while(LoRa.available()) {
        incoming += (char)LoRa.read();
    }

    if(incomingLength != incoming.length()) {  
        //Serial.println("error: message length does not match length");
        return;
    }

    // if the recipient isn't this device or broadcast,
    if(recipient != Node1 && recipient != MasterNode) {
        // Serial.println("This message is not for me.");
        return;
    }

    Serial.println(SenderNode + "/" +incoming);

    if( sender == 0X02 ) {
        String t = getValue(incoming, ',', 0); 
        String h = getValue(incoming, ',', 1); 

        temperature = t.toInt();
        humidity = h.toInt();
        incoming = "";
    }

    if( sender == 0X01 ) {
        temperature = getValue(incoming, ',', 0);
        humidity = getValue(incoming, ',', 1);
        moisture = getValue(incoming, ',', 2); 
        rssi = getValue(incoming, ',', 3);
        incoming = "";
    }

}
