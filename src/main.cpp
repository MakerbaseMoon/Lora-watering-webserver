#include <Arduino.h>

#include <header.h>

#include <SPI.h>
#include <LoRa.h>
#include <WiFi.h>
#include "ESPAsyncWebServer.h"
#include <SPIFFS.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

#define ss 5    // GPIO 15
#define rst 14  // GPIO 16
#define dio0 2  // GPIO 4

// 433E6 for Asia
// 866E6 for Europe
// 915E6 for North America
#define BAND 433E6

const char* ssid     = MY_SSID;
const char* password = MY_PASSWORD;

byte MasterNode = 0xFF;
byte Node1 = 0x01;
byte Node2 = 0x02;

// Define NTP Client to get time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "tw.pool.ntp.org");

// Variables to save date and time
String formattedDate;
String day;
String hour;
String timestamp;

String SenderNode = "";
String outgoing;    // outgoing message

byte msgCount = 0;  // count of outgoing messages
String incoming = "";

// Tracks the time since last event fired
unsigned long previousMillis = 0;
unsigned long int previoussecs = 0;
unsigned long int currentsecs = 0;
unsigned long currentMillis = 0;
int interval = 1 ; // updated every 1 second
int Secs = 0;

// Initialize variables to get and save LoRa data
int rssi;
String loRaMessage;
String temperature;
String humidity;
String pressure;
String readingID;
String soilmoisturepercent;
String soilMoistureValue;
String wateringSW;

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
    Serial.begin(115200);                   // initialize serial
    //  while (!Serial);
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
        request->send_P(200, "text/plain", soilMoistureValue.c_str());
    });
    server.on("/humidity", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send_P(200, "text/plain", soilmoisturepercent.c_str());
    });
    server.on("/pressure", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send_P(200, "text/plain", pressure.c_str());
    });
    server.on("/timestamp", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send_P(200, "text/plain", timestamp.c_str());
    });
    server.on("/rssi", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send_P(200, "text/plain", String(rssi).c_str());
    });
    server.on("/winter", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(SPIFFS, "/winter.jpg", "image/jpg");
    });
    // Start server
    server.begin();
    
    // Initialize a NTPClient to get time
    timeClient.begin();
    timeClient.setTimeOffset(28800);

}

void loop() {

    currentMillis = millis();
    currentsecs = currentMillis / 1000;
    if ((unsigned long)(currentsecs - previoussecs) >= interval) {
        Secs = Secs + 1;
        //Serial.println(Secs);
        if ( Secs >= 11 )
        {
        Secs = 0;
        }
        if ( (Secs >= 1) && (Secs <= 5) )
        {

        String message = "10";
        sendMessage(message, MasterNode, Node1);
        }

        if ( (Secs >= 6 ) && (Secs <= 10))
        {

        String message = "20";
        sendMessage(message, MasterNode, Node2);
        }

        previoussecs = currentsecs;
    }

    // parse for a packet, and call onReceive with the result:
    getTimeStamp();
    onReceive(LoRa.parsePacket());

}

void startLoRA(){
    int counter;

    LoRa.setPins(ss, rst, dio0);
    
    while (!LoRa.begin(BAND) && counter < 10) {
        Serial.print(".");
        counter++;
        delay(500);
    }
    if (counter == 10) {
        // Increment readingID on every new reading
        Serial.println("Starting LoRa failed!"); 
    }
    
    Serial.println("LoRa Master Initialization OK!");
}

void connectWiFi(){
    // Connect to Wi-Fi network with SSID and password
    Serial.print("Connecting to ");
    Serial.println(ssid);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    // Print local IP address and start web server
    Serial.println("");
    Serial.println("WiFi connected.");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
}

String processor(const String& var){
    //Serial.println(var);
    if(var == "TEMPERATURE"){
        return temperature;
    }
    else if(var == "HUMIDITY"){
        return humidity;
    }
    else if(var == "PRESSURE"){
        return pressure;
    }
    else if(var == "TIMESTAMP"){
        return timestamp;
    }
    else if (var == "RRSI"){
        return String(rssi);
    }
    return String();
}

void getTimeStamp() {
    while(!timeClient.update()) {
        timeClient.forceUpdate();
    }
    // The formattedDate comes with the following format:
    // 2018-05-28T16:00:13Z
    // We need to extract date and time
    // formattedDate = timeClient.getFormattedDate();
    formattedDate = timeClient.getFormattedTime();
    //  Serial.println(formattedDate);
    // Extract date
    int splitT = formattedDate.indexOf("T");
    day = formattedDate.substring(0, splitT);
    //  Serial.println(day);
    // Extract time
    hour = formattedDate.substring(splitT+1, formattedDate.length()-1);
    //  Serial.println(hour);
    timestamp = day + " " + hour;
}

void getLoRaData() {
    Serial.print("Lora packet received: ");
    // Read packet
    
    while (LoRa.available()) {
        String LoRaData = LoRa.readString();
        // LoRaData format: readingID/temperature&soilMoisture#batterylevel
        // String example: 1/27.43&654#95.34
        Serial.print(LoRaData); 
        
        // Get readingID, temperature and soil moisture
        int pos1 = LoRaData.indexOf('/');
        int pos2 = LoRaData.indexOf('&');
        int pos3 = LoRaData.indexOf('#');
        readingID = LoRaData.substring(0, pos1);
        temperature = LoRaData.substring(pos1 +1, pos2);
        humidity = LoRaData.substring(pos2+1, pos3);
        pressure = LoRaData.substring(pos3+1, LoRaData.length());    
    }
    // Get RSSI
    rssi = LoRa.packetRssi();
    Serial.print(" with RSSI ");    
    Serial.println(rssi);
}

String getValue(String data, char separator, int index)
{
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
    LoRa.beginPacket();                   // start packet
    LoRa.write(otherNode);              // add destination address
    LoRa.write(MasterNode);             // add sender address
    LoRa.write(msgCount);                 // add message ID
    LoRa.write(outgoing.length());        // add payload length
    LoRa.print(outgoing);                 // add payload
    LoRa.endPacket();                     // finish packet and send it
    msgCount++;                           // increment message ID
}

void onReceive(int packetSize) {
    if (packetSize == 0) return;          // if there's no packet, return

    // read packet header bytes:
    int recipient = LoRa.read();          // recipient address
    byte sender = LoRa.read();            // sender address
    if ( sender == 0X01 )
        SenderNode = "Node1";
    if ( sender == 0X02 )
        SenderNode = "Node2";
    byte incomingMsgId = LoRa.read();     // incoming msg ID
    byte incomingLength = LoRa.read();    // incoming msg length


    while (LoRa.available()) {
        incoming += (char)LoRa.read();
    }

    if (incomingLength != incoming.length()) {   // check length for error
        //Serial.println("error: message length does not match length");
        ;
        return;                             // skip rest of function
    }

    // if the recipient isn't this device or broadcast,
    if (recipient != Node1 && recipient != MasterNode) {
        // Serial.println("This message is not for me.");
        ;
        return;                             // skip rest of function
    }

Serial.println(SenderNode + ": " +incoming);

    // if message is for this device, or broadcast, print details:
    //Serial.println("Received from: 0x" + String(sender, HEX));
    //Serial.println("Sent to: 0x" + String(recipient, HEX));
    //Serial.println("Message ID: " + String(incomingMsgId));
    // Serial.println("Message length: " + String(incomingLength));
    // Serial.println("Message: " + incoming);
    //Serial.println("RSSI: " + String(LoRa.packetRssi()));
    // Serial.println("Snr: " + String(LoRa.packetSnr()));
    // Serial.println();

    if ( sender == 0X02 )
    {
        String t = getValue(incoming, ',', 0); // Temperature
        String h = getValue(incoming, ',', 1); // Humidity

        temperature = t.toInt();
        humidity = h.toInt();
        incoming = "";
    }

    if ( sender == 0X01 )
    {
        soilMoistureValue = getValue(incoming, ',', 0); // Soil Moisture Value
        soilmoisturepercent = getValue(incoming, ',', 1); // Soil Moisture percentage
        wateringSW = getValue(incoming, ',', 2); // Soil Moisture Value
        soilmoisturepercent = getValue(incoming, ',', 1); // Soil Moisture percentage
        incoming = "";
    }

}
