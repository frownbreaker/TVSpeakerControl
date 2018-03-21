#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266mDNS.h>
#include <DNSServer.h>            //Local DNS Server used for redirecting all requests to the configuration portal   
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager 
#include <PubSubClient.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRutils.h>

// An IR detector/demodulator is connected to GPIO pin 14(D5 on a NodeMCU
// board).
uint16_t RECV_PIN = 14;
uint64_t target=0xFE50AF; //Sharp TV Power
//0x5EA1837C - DVD button on AV Amp Office
IRrecv irrecv(RECV_PIN);
//IRrecv irrecv(RECV_PIN,RAWBUF,TIMEOUT_MS, false);

decode_results results;

WiFiClient espClient;
PubSubClient MQTTclient(espClient);
long lastMsg = 0;
char msg[50];
const char* mqtt_server = "192.168.0.251";
String App_name = "TVHelper";


void setup() {
HTTPClient http; // This is the HTTP client that calls the web server to ask for the speakers to be turned on. 
//Serial.begin(9600);
Serial.begin(115200);
irrecv.enableIRIn();  // Start the receiver

pinMode(LED_BUILTIN, OUTPUT);     // Initialize the LED_BUILTIN pin as an output
// Connect to WiFi network
WiFiManager wifiManager;
//first parameter is name of access point, second is the password
wifiManager.autoConnect("TVSpeakerControl");
wifiManager.setConfigPortalTimeout(180); 
WiFi.mode(WIFI_STA); 
Serial.println("WiFi connected");
Serial.println("IP address: ");
Serial.println(WiFi.localIP());
ArduinoOTA.setPassword("5294");
Serial.println("Set OTA Password.");   
Serial.println("Connecting to MQTT..");
MQTTclient.setServer(mqtt_server, 1883);
MQTTclient.setCallback(callback); 
Serial.println("Connected");
MQTTclient.publish("Register", "TV Helper");
// Spin up mDNS
if (!MDNS.begin("rr")) {
  Serial.println("Error setting up mDNS responder!");
  while(1) { 
    delay(1000);
  }
}
  Serial.println("TV Helper Program");    
  ArduinoOTA.onStart([]() {
  String type;
  if (ArduinoOTA.getCommand() == U_FLASH)
    type = "sketch";
  else // U_SPIFFS
    type = "filesystem";  
    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
  Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
  Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
  Serial.printf("Error[%u]: ", error);
  if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
  else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
  else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
  else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
  else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
  // Sleep to allow the TV time to power up the Audio so we don't hear a click
  delay(4000); 
  // Call URL to power on speakers 
  Serial.print("[HTTP] Node Red call begin...\n");
  http.begin("http://192.168.0.251:1880/SHARPTVOPERATING/"); //HTTP
  Serial.print("[HTTP] GET...\n");
  // start connection and send HTTP header
  int httpCode = http.GET();
  // httpCode will be negative on error
  if(httpCode > 0) {
      // HTTP header has been send and Server response header has been handled
      Serial.printf("[HTTP] GET... code: %d\n", httpCode); 
      // file found at server
      if(httpCode == HTTP_CODE_OK) {
          String payload = http.getString();
          Serial.println(payload);   
        }
    } else {
        Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
   }  
   http.end();
}

void loop() {
HTTPClient http; // This is the HTTP client that calls the web server to ask for the speakers to be turned on. 
  //Maintain connection the MQTT Endpoint (Blocking)
  if (!MQTTclient.connected()) {
    reconnect();
  }
  Serial.println("Sending Keepalive to NodeRed over MQTT");
  MQTTclient.publish("TVKeepAlive", "1");
  //Busy wait for 10s look for OTA
  uint32_t period = 10000L; // 10s
  for( uint32_t tStart = millis();  (millis()-tStart) < period;  )
  {
        ArduinoOTA.handle();
        delay(100);
        if (irrecv.decode(&results)) 
        {
          // print() & println() can't handle printing long longs. (uint64_t)
          serialPrintUint64(results.value, HEX);
          Serial.println("");
          if (results.value==target)
            {
              Serial.println("Match");
              delay(15000); 
              // Wait for TV to power off and power off this unit for 15seconds, if there is still power arriving the TV needs to be rebooted
              // If the TV does power down then this unit will loose power but the timeout on  Node-Red will trigger and power down the speakers
              Serial.print("[HTTP] Node Red call begin...\n");
              http.begin("http://192.168.0.251:1880/SHARPTVSHUTDOWN/"); //HTTP
              Serial.print("[HTTP] GET...\n");
              // start connection and send HTTP header
              int httpCode = http.GET();
              // httpCode will be negative on error
              if(httpCode > 0) {
                  // HTTP header has been send and Server response header has been handled
                  Serial.printf("[HTTP] GET... code: %d\n", httpCode); 
                  // file found at server
                  if(httpCode == HTTP_CODE_OK) {
                      String payload = http.getString();
                      Serial.println(payload);  
                      Serial.println("Called Node Red asked for shutdown");  
                      delay(1000*60*60); 
                    }
                } else {
                    Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
               }  
               http.end();
            }
         irrecv.resume();  // Receive the next value
        }
   } 
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  // Switch on the LED if an 1 was received as first character
  if ((char)payload[0] == '1') {
    digitalWrite(BUILTIN_LED, LOW);   // Turn the LED on (Note that LOW is the voltage level
    // but actually the LED is on; this is because
    // it is acive low on the ESP-01)
  } else {
    digitalWrite(BUILTIN_LED, HIGH);  // Turn the LED off by making the voltage HIGH
  }

}


void publish()
{
   snprintf (msg, 75, "%ld", 1);
   Serial.print("Publish message: ");
   Serial.println(msg);
   MQTTclient.publish("RFWebRequest", msg);
}
void reconnect() {
  // Loop until we're reconnected
  while (!MQTTclient.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "ESP8266Client-";
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (MQTTclient.connect(clientId.c_str())) {
      Serial.println("connected");
      // Once connected, publish an announcement...
      MQTTclient.publish("Register", "WebRF Controller Connected");
      // ... and resubscribe
      MQTTclient.subscribe("inTopic");
    } else {
      Serial.print("failed, rc=");
      Serial.print(MQTTclient.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}



