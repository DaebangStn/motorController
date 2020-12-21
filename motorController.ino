#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <ESP8266HTTPClient.h>


#ifndef STASSID
#define STASSID "SK_WiFiE925"
#define STAPSK  "1402018716"
#endif

const char * ssid = STASSID; // your network SSID (name)
const char * pass = STAPSK;  // your network password

unsigned int localPort = 2390;      // local port to listen for UDP packets
const uint16_t port_server = 8000;

IPAddress timeServerIP; // time.nist.gov NTP server address
const char* ntpServerName = "ntp.ubuntu.com";
const char* serverName = "192.168.25.3";

String serverName_str = String(serverName);
String MAC;

const int NTP_PACKET_SIZE = 48; // NTP time stamp is in the first 48 bytes of the message

byte packetBuffer[ NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets

// A UDP instance to let us send and receive packets over UDP
WiFiUDP udp;

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println();

  // We start by connecting to a WiFi network
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

  Serial.println("Starting UDP");
  udp.begin(localPort);
  Serial.print("Local port: ");
  Serial.println(udp.localPort());
}

void loop() {
  //get a random server from the pool
  WiFi.hostByName(ntpServerName, timeServerIP);

  WiFiClient client;
  HTTPClient http;

  if (!client.connect(serverName, port_server)) {
    Serial.println("connection failed");
    Serial.println("wait 5 sec...");
    delay(5000);
    return;
  }

  uint8_t MAC_arr[6];
  char temp[2];
  WiFi.macAddress(MAC_arr);
  for(int i=0; i<sizeof(MAC_arr); i++){
    sprintf(temp, "%x", MAC_arr[i]);
    MAC += temp;
  }
  

  Serial.println(MAC);
  //register MAC
 

  utc();
  // wait ten seconds before asking for the time again
  delay(10000);
}

int utc(void){
  int timeout = 1000;
  
  WiFi.hostByName(ntpServerName, timeServerIP);
  sendNTPpacket(timeServerIP); // send an NTP packet to a time server
  // wait to see if a reply is available
  int stamp = millis();
  int cb = udp.parsePacket();
  while(!cb && (millis()-stamp) < timeout){
    cb = udp.parsePacket();
  }

  if((millis()-stamp) > timeout){
    Serial.println("ntp connection failed");
    return 0;    
  }else{
    Serial.print("packet received, length=");
    Serial.println(cb);
    // We've received a packet, read the data from it
    udp.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer

    //the timestamp starts at byte 40 of the received packet and is four bytes,
    // or two words, long. First, esxtract the two words:

    unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
    unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
    unsigned long secsSince1900 = highWord << 16 | lowWord;

    // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
    const unsigned long seventyYears = 2208988800UL;
    // subtract seventy years:
    unsigned long epoch = secsSince1900 - seventyYears;
    // print Unix time:
    Serial.print("Unix time = ");
    Serial.println(epoch);

    return epoch;
  }
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
