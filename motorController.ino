#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>


#ifndef STASSID
#define STASSID "SK_WiFiE925"
#define STAPSK  "1402018716"
#endif
#define interruptPin  D1
#define pwmPin        D6
#define utc_timeout 2000
#define tacho_minInterval 500


const char * ssid = STASSID; // your network SSID (name)
const char * pass = STAPSK;  // your network password

const char* ntpServerName = "ntp.ubuntu.com";
String base_addr = "http://192.168.25.3:8000/motor/";
String MAC;
//String MAC_dummy = String("a");

int utc_timestamp;
int millis_timestamp;
int tacho_millis;
unsigned int cnt_hall;

int duty;

// ntp related variables
const int NTP_PACKET_SIZE = 48; // NTP time stamp is in the first 48 bytes of the message
byte packetBuffer[ NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets
WiFiUDP udp;
unsigned int localPort = 2390;      // local port to listen for UDP packets
IPAddress timeServerIP; // time.nist.gov NTP server address


ICACHE_RAM_ATTR void hall_sense(){
  cnt_hall++;
}


void setup() {
  Serial.begin(115200);
  pinMode(interruptPin, INPUT);
  pinMode(pwmPin, OUTPUT);
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
}


void loop(){   
  //int target = update_status(100, 23);
  int speed_data = tachometer(); 
  Serial.print(speed_data);
  Serial.println("rpm");
  Serial.print(adjust_speed(speed_data, 1000));
  Serial.println("duty");

  Serial.print(update_status(speed_data));
  Serial.println("http code");

  delay(2000);
}


// returns duty
int adjust_speed(int speed_data, int speed_target){
  int speed_diff = speed_target - speed_data;
  if(speed_diff>200){
    duty += 100;
  }else if(speed_diff>50){
    duty += 10;
  }else if(speed_diff>10){
    duty += 2;
  }else if(speed_diff>0){
    duty += 1;
  }else if(speed_diff==0){
    duty += 0;
  }else if(speed_diff>-10){
    duty -= 2;
  }else if(speed_diff>-50){
    duty -= 10;
  }else if(speed_diff>-200){
    duty -= 100;
  }

  if(duty>1023){
    Serial.println("target is larger than maximum");
    duty = 1023;
  }else if(duty<0){
    Serial.println("target is smaller than minimum");
    duty = 0;
  }

  analogWrite(pwmPin, duty);
  return duty;
}


// returns in [rpm]
// when access interval is shorter than tacho_minInterval, returns -1 
int tachometer(){
  int tacho_temp = millis();
  if(tacho_temp - tacho_millis < tacho_minInterval){
    return -1;
  }

  int cnt_hall_interval;
  int tacho_interval;
  noInterrupts();
  tacho_interval = millis() - tacho_millis;
  tacho_millis = millis();
  cnt_hall_interval = cnt_hall;
  cnt_hall = 0;
  interrupts();

  float rpm = (float)cnt_hall_interval / (float)tacho_interval;
  Serial.print(cnt_hall_interval);
  Serial.print("/");
  Serial.println(tacho_interval);
  rpm = rpm * 7500.0; // rev per ms, 2 fallings per tick, 4 ticks per rev, rpm = rpm / 8.0 && rpm = rpm * 60000.0; // rpm

  return (int)rpm; 
}


// update status, returns speed_target, -1:error
int update_status(int speed_data){
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
