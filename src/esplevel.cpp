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

#define ADXL 0x53 // i2c address for accel

#define HASDOW 0 // device has 1-wire device attached
#define DOWPIN 4 // 1-wire data pin
#define DOWPWR 5 // 1-wire power pin
#define pinSDA 12 // i2c data pin
#define pinSCL 13 // i2c clock oin

ADXL345 accel(ADXL); 
OneWire oneWire(DOWPIN); 
DallasTemperature ds18b20(&oneWire);

#define serialDebug 0 // flag for serial debugging commands

#define min(X, Y) (((X)<(Y))?(X):(Y))

// #define WIFIPASSWORD "defined via build flag"
const char* ssid = "Tell my WiFi I love her";
const char* password = WIFIPASSWORD;
const char* mqtt_server = "mypi3";
const char* myPub = "trailer/esplevel/msg"; // general messages
const char* myAccel = "trailer/esplevel/accel"; // accell data
const char* mySub = "trailer/esplevel/cmd"; // general commands 
const char* clientid = "esplevel"; // hostname

uint16_t lastReconnectAttempt = 0;
uint16_t lastMsg = 0;
char msg[200];
byte i2cbuff[30];
char devId[6];
double celsius;
int16_t adxlX = 0;
int16_t adxlY = 0;
int16_t adxlZ = 0;
uint8_t LOOPDELAY = 5; // time delay between updates (in 100ms chunks)


WiFiClient espClient;
PubSubClient client(espClient);

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

int i2c_wordread(int address, int cmd) {
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

byte i2c_read(byte devaddr, byte regaddr) {

  byte result = 0;

  Wire.beginTransmission(devaddr);
  Wire.write(regaddr); // control register
  Wire.endTransmission();

  int readbytes = Wire.requestFrom(devaddr, 1, true); // request cnt bytes

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


void setup_wifi() {

  delay(10);
  // We start by connecting to a WiFi network
  if (serialDebug) Serial.println();
  if (serialDebug) Serial.print("Connecting to ");
  if (serialDebug) Serial.println(ssid);

  WiFi.begin(ssid, password);

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
  ArduinoOTA.setHostname(clientid);

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

void callback(char* topic, byte* payload, unsigned int length) { // handle incoming MQTT messages
  if (serialDebug) Serial.print("Message arrived [");
  if (serialDebug) Serial.print(topic);
  if (serialDebug) Serial.print("] ");
  for (int i = 0; i < length; i++) {
    if (serialDebug) Serial.print((char)payload[i]);
  }
  if (serialDebug) Serial.println();
}

boolean reconnect() { // connect or reconnect to MQTT server
  if (client.connect("arduinoClient")) {
    // Attempt to connect
    if (client.connect(clientid)) {
      if (serialDebug) Serial.println("Establish MQTT connection.");
      // Once connected, publish an announcement...
      client.publish(myPub, "hello world", true);
      // ... and resubscribe
      client.subscribe(mySub);
    }
  }
  return client.connected();
}

void setup() {
  if (serialDebug) Serial.begin(115200);

  if (HASDOW) {
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
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

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

  // setup accel
  accel.writeRange(ADXL345_RANGE_16G); // 16g provides the highest resolution (13 bits)
  accel.writeRate(ADXL345_RATE_200HZ); // sampling rate
  accel.start();

  /*
  byte deviceID = accel.readDeviceID();
  if (deviceID != 0) {
    if (serialDebug) Serial.print("ADXL345 Found at 0x");
    if (serialDebug) Serial.print(deviceID, HEX);
    if (serialDebug) Serial.println("");
  } else {
    if (serialDebug) Serial.println("ADXL345 read device failed");
  }
  */
 }

void loop() {
  ArduinoOTA.handle(); // check for OTA updates

  if (!client.connected()) { // check on MQTT connection
    if (serialDebug) Serial.println("MQTT connect failed!");
    long now = millis();
    if (now - lastReconnectAttempt > 5000) {
      lastReconnectAttempt = now;
      // Attempt to reconnect
      if (reconnect()) {
        lastReconnectAttempt = 0;
      }
    }
  } else {
    // Client connected
    client.loop(); // check for MQTT messages
  }

  if (accel.update()) { // read new data from accel, send it over MQTT
    adxlX = accel.getRawX();
    adxlY = accel.getRawY();
    adxlZ = accel.getRawZ();
    sprintf(msg, "x=%d,y=%d,z=%d", adxlX, adxlY, adxlZ);
    client.publish(myAccel, msg, true);
  }

  if (HASDOW) { // if DOW available, get temperature
    ds18b20.requestTemperatures();
    byte retry = 5;
    float temp=0.0;
    do {
      temp = ds18b20.getTempCByIndex(0);
      retry--;
      delay(2);
    } while (retry > 0 && (temp == 85.0 || temp == (-127.0)));
    // send over mqtt eventually
  }

  for (uint8_t x=0; x<LOOPDELAY; x++) { // pause for a few, checking for OTA updates
    delay(100);
    ArduinoOTA.handle();
  }
} // end of main loop
