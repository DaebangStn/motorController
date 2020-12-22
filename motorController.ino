#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>


#ifndef STASSID
#define STASSID "SK_WiFiE925"
#define STAPSK  "1402018716"
#endif
#define interruptPin D1


const char * ssid = STASSID; // your network SSID (name)
const char * pass = STAPSK;  // your network password

const char* ntpServerName = "ntp.ubuntu.com";
String base_addr = "http://192.168.25.3:8000/motor/";
String MAC;
//String MAC_dummy = String("a");

int utc_timestamp;
int millis_timestamp;

unsigned int cnt_hall;

// ntp related variables
const int NTP_PACKET_SIZE = 48; // NTP time stamp is in the first 48 bytes of the message
byte packetBuffer[ NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets
WiFiUDP udp;
unsigned int localPort = 2390;      // local port to listen for UDP packets
IPAddress timeServerIP; // time.nist.gov NTP server address
int utc_timeout = 2000;


ICACHE_RAM_ATTR void hall_sense(){
  cnt_hall++;
}


void setup() {
  Serial.begin(115200);
  pinMode(interruptPin, INPUT);
  attachInterrupt(interruptPin, hall_sense, FALLING);

  // Connecting to WiFi network
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");

  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  // get MAC
  uint8_t MAC_arr[6];
  char temp[2];
  WiFi.macAddress(MAC_arr);
  for(int i=0; i<sizeof(MAC_arr); i++){
    sprintf(temp, "%x", MAC_arr[i]);
    MAC += temp;
  }
/*
  // register MAC
  HTTPClient http;
  String url = base_addr + String("register/") + MAC;
  http.begin(url);
  Serial.println(url);
  Serial.println("Registering controller...");
  int httpCode = http.GET();
  while(httpCode <= 0){
    httpCode = http.GET();
    Serial.println("Registering failed...");
    delay(1000);
  }
  http.end();
  */
}


void loop() {   
  //int target = update_status(100, 23);
  Serial.println(cnt_hall);
  Serial.println(digitalRead(interruptPin));
  // wait ten seconds before asking for the time again
  delay(1000);
}


// update status, returns speed_target, -1:error
int update_status(int speed_data, int duty){
  StaticJsonDocument<200> doc;
  String body_up;
  
  doc["speed_data"] = speed_data;
  doc["duty"] = duty;
  doc["timestamp"] = utc_time();
  serializeJson(doc, body_up);  
  Serial.println("POST body");
  Serial.println(body_up);
  
  HTTPClient http;
  String url = base_addr + String("update/") + MAC;
  http.begin(url);
  Serial.println(url);
  Serial.println("Updating controller...");
  int httpCode = http.POST(body_up);
  while(httpCode <= 0){
    httpCode = http.POST(body_up);
    Serial.println("Updating failed...");
    delay(1000);
  }
  http.end();  
}


int utc_time(void){  
  if(!utc_timestamp){
    Serial.println("Starting UDP");
    udp.begin(localPort);
    Serial.print("Local port: ");
    Serial.println(udp.localPort());

    WiFi.hostByName(ntpServerName, timeServerIP);
    // send an NTP packet to a time server
    sendNTPpacket(timeServerIP); 
    // wait to see if a reply is available
    int stamp = millis();
    int cb = udp.parsePacket();

    while(!cb && (millis()-stamp) < utc_timeout){
      cb = udp.parsePacket();
    }

    if((millis()-stamp) > utc_timeout){
      Serial.println("ntp connection failed");
      return 0;    
    }

    Serial.print("packet received, length=");
    Serial.println(cb);
    // We've received a packet, read the data from it
    udp.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer
    millis_timestamp = millis();

    //the timestamp starts at byte 40 of the received packet and is four bytes,
    // or two words, long. First, esxtract the two words:

    unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
    unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
    unsigned long secsSince1900 = highWord << 16 | lowWord;

    // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
    const unsigned long seventyYears = 2208988800UL;
    // subtract seventy years:
    utc_timestamp = (int)(secsSince1900 - seventyYears);
    // print Unix time:
    Serial.print("Unix time = ");
    Serial.println(utc_timestamp);    
  }

  int utc_now = utc_timestamp + millis() - millis_timestamp;
  return utc_now;
}


// send an NTP request to the time server at the given address
void sendNTPpacket(IPAddress& address) {
  Serial.println("sending NTP packet...");
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;

  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  udp.beginPacket(address, 123); //NTP requests are to port 123
  udp.write(packetBuffer, NTP_PACKET_SIZE);
  udp.endPacket();
}
