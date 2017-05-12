#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include <Time.h>
#include <Wire.h>
#include <NtpClientLib.h>
#include <PubSubClient.h>
#include <ADXL345.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <WebSocketsServer.h>
#include <Hash.h>
#include <WiFiSETUP.h>

#define ADXL 0x53 // i2c address for accel

#define HASDOW 0 // device has 1-wire device attached
#define DOWPIN 4 // 1-wire data pin
#define DOWPWR 5 // 1-wire power pin
#define pinSDA 12 // i2c data pin
#define pinSCL 13 // i2c clock oin

ADXL345 accel(ADXL);
OneWire oneWire(DOWPIN);
DallasTemperature ds18b20(&oneWire);
WebSocketsServer webSocket = WebSocketsServer(81);
WiFiClient espClient;
PubSubClient mqtt(espClient);

#define serialDebug 0 // flag for serial debugging commands

#define min(X, Y) (((X)<(Y))?(X):(Y))

// #define WIFIPASSWORD "defined via build flag"
// const char* ssid = WIFISSID;
// const char* password = WIFIPASSWORD;
const char* mqtt_server = "mypi3";
const char* mqttPub = "trailer/msg"; // general messages
const char* mqttAccel = "trailer/esplevel/accelerometer"; // accell data
const char* mqttSub = "trailer/esplevel/cmd"; // general commands
const char* mqttTemp = "trailer/esplevel/temperature"; // general commands
const char* mqttRSSI = "trailer/esplevel/rssi"; // general commands
const char* mqttEpoch = "trailer/esplevel/epoch"; // general commands
const char* nodeName = "esplevel"; // hostname

uint16_t lastReconnectAttempt = 0;
uint16_t lastMsg = 0;
char msg[200];
char msgTemp[10];
char msgRSSI[10];
char msgAccel[24];
char str[60];
uint8_t i2cbuff[30];
char devId[6];
double celsius;
bool hasTout = false; // no temp out right now
bool hasAccel = true;
bool hasRSSI = true; // report our signal strength
bool setPolo = false; bool doReset = false; bool getTime = false;
bool useMQTT = false; // flag for mqtt available
bool skipSleep = false; // flag for mqtt available
int16_t adxlX = 0; int16_t adxlY = 0; int16_t adxlZ = 0; // raw accel storages
uint8_t LOOPDELAY = 5; // time delay between updates (in 100ms chunks)
uint8_t wsConcount = 0; // counter for websocket connections
uint8_t newWScon = 0;

char* cleanStr(const char* _str) {
  int x=0, i=0;
  char c;
  memset(str,0,sizeof(str)); // zero out array

  while (((c = _str[i++]) != '\0') && (x<59)) { // read array until we hit a null
    if (isPrintable(c)) str[x++] = c; // exclude character that are not alphaNumeric
  }
  str[x] = '\0'; // null terminate

  return str; // return printable results
}

void wsSend(const char* _str) {
  if (sizeof(_str)<=1) return; // don't send blank messages
  if (wsConcount>0) {
    for (int x=0; x<wsConcount; x++) {
      webSocket.sendTXT(x, _str);
    }
  }
}

void i2c_wordwrite(int address, int cmd, int theWord) {
  //  Send output register address
  Wire.beginTransmission(address);
  Wire.write(cmd); // control register
  Wire.write(highByte(theWord));  //  high byte
  Wire.write(lowByte(theWord));  //  send low byte of word data
  Wire.endTransmission();
}

void i2c_write(int address, int cmd, int data) {
  //  Send output register address
  Wire.beginTransmission(address);
  Wire.write(cmd); // control register
  Wire.write(data);  //  send byte data
  Wire.endTransmission();
}

uint16_t i2c_wordread(int address, int cmd) {
  int result;
  int xlo, xhi;

  Wire.beginTransmission(address);
  Wire.write(cmd); // control register
  Wire.endTransmission();

  int readbytes = Wire.requestFrom(address, 2); // request two bytes
  xhi = Wire.read();
  xlo = Wire.read();

  result = xhi << 8; // hi byte
  result = result | xlo; // add in the low byte

  return result;
}

uint8_t i2c_read(byte devaddr, byte regaddr) {

  uint8_t result = 0;
  size_t readcnt = 1;
  Wire.beginTransmission(devaddr);
  Wire.write(regaddr); // control register
  Wire.endTransmission();

  uint8_t readbytes = Wire.requestFrom(devaddr, readcnt, (bool)true); // request cnt bytes
  result  = Wire.read();

  return result;
}

void i2c_readbytes(byte address, byte cmd, byte bytecnt) {

  Wire.beginTransmission(address);
  Wire.write(cmd); // control register
  Wire.endTransmission();

  Wire.requestFrom(address, bytecnt); // request cnt bytes
  for (byte x = 0; x < bytecnt; x++) {
    i2cbuff[x] = Wire.read();
  }
}


void i2c_scan() {
  byte error, address;
  int nDevices;

  if (serialDebug) Serial.println("Scanning I2C Bus...");

  nDevices = 0;
  for(address = 1; address < 127; address++ ) {
    // The i2c_scanner uses the return value of
    // the Write.endTransmisstion to see if
    // a device did acknowledge to the address.
    Wire.beginTransmission(address);
    error = Wire.endTransmission();

    if (error == 0) {
      if (serialDebug) Serial.print("Device found at 0x");
      if (address<16)
      if (serialDebug) Serial.print("0");
      if (serialDebug) Serial.print(address,HEX);
      if (serialDebug) Serial.println();

      nDevices++;
    }
  }

}

void handleMsg(char* cmdStr) { // handle commands from mqtt broker
  // using c string routines instead of Arduino String routines ... a lot faster
  char* cmdTxt = strtok(cmdStr, "=");
  char* cmdVal = strtok(NULL, "=");

  if (strcmp(cmdTxt, "marco")==0) setPolo = true;
  else if (strcmp(cmdTxt, "reboot")==0) doReset = true;
  else if (strcmp(cmdTxt, "gettime")==0) getTime = true;
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
      case WStype_DISCONNECTED:
          //USE_SERIAL.printf("[%u] Disconnected!\n", num);
          wsConcount--;
          sprintf(str,"ws Disconnect count=%d",wsConcount);
          if (useMQTT) mqtt.publish(mqttPub,str);
          break;
      case WStype_CONNECTED:
          {
              IPAddress ip = webSocket.remoteIP(num);
              sprintf(str,"[%u] connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);
              //USE_SERIAL.println();
              // send message to client
              sprintf(str,"Connection #%d.", num);
              webSocket.sendTXT(num, str);
              sprintf(str, "name=%s", nodeName);
              webSocket.sendTXT(num, str);
              //webSocket.sendTXT(num, mqttPub);
              //webSocket.sendTXT(num, mqttsub);
              if (timeStatus() == timeSet) webSocket.sendTXT(num, "Time is set.");
              else webSocket.sendTXT(num, "Time not set.");
              //mqtt.publish(mqttPub, str);
              //wsSendlabels();
              newWScon = num + 1;
              wsConcount++;
              sprintf(str,"ws Connect count=%d",wsConcount);
              if (useMQTT) mqtt.publish(mqttPub,str);
          }
          break;
      case WStype_TEXT:
          payload[length] = '\0'; // null terminate
          handleMsg((char *)payload);

          break;
      case WStype_BIN:
         // USE_SERIAL.printf("[%u] get binary lenght: %u\n", num, length);
          hexdump(payload, length);

          // send message to client
          // webSocket.sendBIN(num, payload, lenght);
          break;
  }
}

void mqttCallback(char* topic, uint8_t* payload, uint16_t len) {
  skipSleep=true; // don't go to sleep if we receive mqtt message
  char tmp[200];
  strncpy(tmp, (char*)payload, len);
  tmp[len] = 0x00;
  handleMsg(tmp);
}

void setup_wifi() {
  delay(10);
  // We start by connecting to a WiFi network
  if (serialDebug) Serial.println();
  if (serialDebug) Serial.print("Connecting to ");
  if (serialDebug) Serial.println(WIFISSID);

  WiFi.begin(WIFISSID, WIFIPASSWORD);

  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    if (serialDebug) Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }

  if (serialDebug) Serial.println("");
  if (serialDebug) Serial.print("WiFi connected, my IP address ");
  if (serialDebug) Serial.println(WiFi.localIP());

  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname(nodeName);

  // No authentication by default
  // ArduinoOTA.setPassword((const char *)"123");

  ArduinoOTA.onStart([]() {
    if (serialDebug) Serial.print("Start");
  });
  ArduinoOTA.onEnd([]() {
    if (serialDebug) Serial.println("End");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    if (serialDebug) Serial.print(".");
  });
  ArduinoOTA.onError([](ota_error_t error) {
    if (serialDebug) Serial.printf("Error[%u]: ", error);

    if (error == OTA_AUTH_ERROR) if (serialDebug) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) if (serialDebug) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) if (serialDebug) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) if (serialDebug) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) if (serialDebug) Serial.println("End Failed");

  });
  ArduinoOTA.begin();
  if (serialDebug) Serial.println("OTA is ready");
}

boolean mqttReconnect() { // connect or reconnect to MQTT server
  if (mqtt.connect(nodeName)) {
    if (serialDebug) Serial.println("Established MQTT connection.");
    // Once connected, publish an announcement...
    sprintf(msg,"Hello from %s", nodeName);
    mqtt.publish(mqttPub, msg);
    // ... and resubscribe
    mqtt.subscribe(mqttSub);
    useMQTT = true;
  }
  return mqtt.connected();
}

void setup() {
  if (serialDebug) Serial.begin(115200);

  if (HASDOW) {
    hasTout = false;
    pinMode(DOWPWR, OUTPUT);
    // turn on the one wire device
    digitalWrite(DOWPWR, 1);
    ds18b20.begin(); // start one wire temp probe
  }

  Wire.begin(pinSDA, pinSCL); // setup i2c bus

  setup_wifi();
  // i2c_scan();

  // setup MQTT
  lastReconnectAttempt = 0;
  mqtt.setServer(mqtt_server, 1883);
  mqtt.setCallback(mqttCallback);

  // setup NTP
  NTP.onNTPSyncEvent([](NTPSyncEvent_t error) {
      if (error) {
          if (serialDebug) Serial.print("Time Sync error: ");
          if (error == noResponse)
              if (serialDebug) Serial.println("NTP server not reachable");
          else if (error == invalidAddress)
              if (serialDebug) Serial.println("Invalid NTP server address");
      }
      else {
          if (serialDebug) Serial.print("Got NTP time: ");
          if (serialDebug) Serial.println(NTP.getTimeDateString(NTP.getLastNTPSync()));
      }

  });

  NTP.begin("us.pool.ntp.org", 1, true);
  NTP.setInterval(600); // reset time every 10 minutes
  NTP.setTimeZone(-5); // eastern US time
  NTP.setDayLight(true); // daylight savings


  // start websockets server
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

  // setup accel
  accel.writeRange(ADXL345_RANGE_16G); // 16g provides the highest resolution (13 bits)
  accel.writeRate(ADXL345_RATE_200HZ); // sampling rate
  accel.start();

  byte deviceID = accel.readDeviceID();
  if (deviceID != 0) {
    if (serialDebug) Serial.print("ADXL345 Found at 0x");
    if (serialDebug) Serial.print(deviceID, HEX);
    if (serialDebug) Serial.println("");
    hasAccel = true; // we have an accel!
  } else {
    if (serialDebug) Serial.println("ADXL345 read device failed");
    hasAccel = false;
  }
 }

 void wsSendTime(const char* msg, time_t mytime) {
   memset(str,0,sizeof(str));
   sprintf(str, msg, mytime);
   wsSend(str);
 }

 void mqttSendTime(time_t _time) {
   if (!mqtt.connected()) return; // bail out if there's no mqtt connection
   memset(str,0,sizeof(str));
   sprintf(str,"%d", _time);
   mqtt.publish(mqttEpoch, str);
 }

 void wsData() { // send some websockets data if client is connected
   if (wsConcount<=0) return;

   if (timeStatus() == timeSet) wsSendTime("epoch=%d",now()); // send time to ws client
   if (hasAccel) wsSend(msgAccel);
   if (hasTout) wsSend(msgTemp); // send temperature
   if (hasRSSI) wsSend(msgRSSI); // send rssi info
 }

 void mqttData() { // send mqtt messages as required
   if (!mqtt.connected()) return; // bail out if there's no mqtt connection

   if (timeStatus() == timeSet) mqttSendTime(now());
   if (hasAccel) mqtt.publish(mqttAccel, msgAccel);
   if (hasTout) mqtt.publish(mqttTemp, msgTemp);
   if (hasRSSI) mqtt.publish(mqttRSSI, msgRSSI);
 }

void doAccel() { // read accel store in global array
  memset(msgAccel,0,sizeof(msgAccel));
  if (accel.update()) { // read new data from accel, send it over MQTT
    adxlX = accel.getRawX();
    adxlY = accel.getRawY();
    adxlZ = accel.getRawZ();
    // mqtt.publish(myAccel, msgAccel, true);
  } else {
    adxlX = 0; adxlY = 0; adxlZ = 0;
  }
  sprintf(msgAccel, "x=%d,y=%d,z=%d", adxlX, adxlY, adxlZ);
}

void doRSSI() {
  int16_t rssi = WiFi.RSSI();
  memset(msgRSSI,0,sizeof(msgRSSI));
  sprintf(msgRSSI, "%d", rssi);
}

void doTout() {
  String vStr;
  if (!HASDOW) return;

  memset(msgTemp,0,sizeof(msgTemp));
  if (DOWPWR>0) {
    digitalWrite(DOWPWR, HIGH); // ow on
    delay(5); // wait for powerup
  }

  ds18b20.requestTemperatures();
  byte retry = 5;
  float temp=0.0;
  do {
    temp = ds18b20.getTempCByIndex(0);
    retry--;
    delay(2);
  } while (retry > 0 && (temp == 85.0 || temp == (-127.0)));

  if (DOWPWR>0) {
    digitalWrite(DOWPWR, LOW); // ow off
  }

  vStr = String(temp,4);
  vStr.toCharArray(msgTemp, vStr.length()+1);
}

void loop() {
  ArduinoOTA.handle(); // check for OTA updates

  if (!mqtt.connected()) { // check on MQTT connection
    useMQTT = false;
    if (serialDebug) Serial.println("MQTT connect failed!");
    long now = millis();
    if (now - lastReconnectAttempt > 5000) {
      lastReconnectAttempt = now;
      // Attempt to reconnect
      if (mqttReconnect()) {
        lastReconnectAttempt = 0;
      }
    }
  } else {
    // Client connected
    mqtt.loop(); // check for MQTT messages
  }

  if (hasAccel) doAccel();
  if (hasTout) doTout();
  if (hasRSSI) doRSSI();

  if (wsConcount>0) wsData();
  if (useMQTT) mqttData();

  for (uint8_t x=0; x<LOOPDELAY; x++) { // pause for a few, checking for OTA updates
    delay(100);
    ArduinoOTA.handle();
  }
} // end of main loop
