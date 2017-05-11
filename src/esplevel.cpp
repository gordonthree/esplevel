#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include <Adafruit_GFX.h>
#include <Time.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>
#include <NtpClientLib.h>
#include <PubSubClient.h>
#include <ADXL345.h>
#include <Adafruit_ADS1015.h>
#include <Adafruit_MCP4725.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#define OLED 0x3C
#define ADS 0x49
#define ADXL 0x53
#define BEEP 0x16
#define MCP 0x63
#define DOW 14

Adafruit_ADS1115 ads(0x49);  // 0x49
ADXL345 accel(ADXL345_ALT); // 0x53
Adafruit_MCP4725 dac; // 0x63
OneWire oneWire(DOW); // 0x14 gpio14
DallasTemperature ds18b20(&oneWire);

#define OLED_RESET 13
Adafruit_SSD1306 display(OLED_RESET);


#define serialDebug 0x01
#define DAC_RESOLUTION (8)

#define min(X, Y) (((X)<(Y))?(X):(Y))

//  I2C device address is 0 1 0 0   A2 A1 A0


const char* ssid = "Tell my WiFi I love her";
const char* password = "2317239216";
const char* mqtt_server = "mypi3";
const char* myPub = "trailer/oled/msg";
const char* mySub = "trailer/oled/cmd";
const char* clientid = "oled1";

long lastReconnectAttempt = 0;
long lastMsg = 0;
char msg[200];
int value = 0;
double celsius;
int vpres = 0;
int vref = 0;
int freespace = 0;
int reporting = 0;
int vmin = 0;
int vmax = 0;
int vpwr = 0;
byte rtcVals[8];
byte i2cbuff[30];
char devId[6];
uint16_t dacOut = 0;

WiFiClient espClient;
PubSubClient client(espClient);

void doBeep(byte cnt) {
  while (cnt--) {
    digitalWrite(BEEP, 1); delay(100);
    digitalWrite(BEEP, 0); delay(100);
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
  ArduinoOTA.setHostname("oled1");

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

void callback(char* topic, byte* payload, unsigned int length) {
  if (serialDebug) Serial.print("Message arrived [");
  if (serialDebug) Serial.print(topic);
  if (serialDebug) Serial.print("] ");
  for (int i = 0; i < length; i++) {
    if (serialDebug) Serial.print((char)payload[i]);
  }
  if (serialDebug) Serial.println();
}

boolean reconnect() {
  if (client.connect("arduinoClient")) {
    // Attempt to connect
    if (client.connect(clientid)) {
      if (serialDebug) Serial.println("Establish MQTT connection.");
      // Once connected, publish an announcement...
      client.publish("home/oled1/msg", "hello world", true);
      // ... and resubscribe
      client.subscribe("home/oled1/cmd");
    }
  }
  return client.connected();
}

void testdrawchar(void) {
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0,0);

  for (uint8_t i=0; i < 168; i++) {
    if (i == '\n') continue;
    display.write(i);
    if ((i > 0) && (i % 21 == 0))
      display.println();
  }
  display.display();
  delay(1);
}


void testscrolltext(void) {
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor(10,0);
  display.clearDisplay();
  display.println("scroll");
  display.display();
  delay(1);

  display.startscrollright(0x00, 0x0F);
  delay(2000);
  display.stopscroll();
  delay(1000);
  display.startscrollleft(0x00, 0x0F);
  delay(2000);
  display.stopscroll();
  delay(1000);
  display.startscrolldiagright(0x00, 0x07);
  delay(2000);
  display.startscrolldiagleft(0x00, 0x07);
  delay(2000);
  display.stopscroll();
}

void setup() {
  if (serialDebug) Serial.begin(115200);
  pinMode(BEEP, OUTPUT);

  // iic.pins(0, 2); //on ESP-01 sda gpio0 scl gpio2
  // iic.begin(0, 2);
  Wire.begin(4,5);

  setup_wifi();
  i2c_scan();

  ds18b20.begin(); // start one wire temp probe

  lastReconnectAttempt = 0;
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

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
  NTP.setInterval(600);
  NTP.setTimeZone(-5);
  NTP.setDayLight(true);

  byte deviceID = accel.readDeviceID();
  if (deviceID != 0) {
    if (serialDebug) Serial.print("ADXL345 Found at 0x");
    if (serialDebug) Serial.print(deviceID, HEX);
    if (serialDebug) Serial.println("");
  } else {
    if (serialDebug) Serial.println("ADXL345 read device failed");
  }

  accel.writeRange(ADXL345_RANGE_16G);
  accel.writeRate(ADXL345_RATE_200HZ);
  accel.start();

  ads.begin();
  ads.setGain(GAIN_ONE);
  ads.setSPS(ADS1115_DR_250SPS);

  dac.begin(0x63);

  // by default, we'll generate the high voltage from the 3.3v line internally! (neat!)
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);  // initialize with the I2C addr 0x3D (for the 128x64)
  display.dim(false);
  // Clear the buffer.
  display.clearDisplay();
  display.display();
  delay(200);

 }

void loop() {
  ArduinoOTA.handle();

  if (!client.connected()) {
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
    client.loop();
  }

  // text display tests
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0,0);
  display.print("EDT ");
  display.println(NTP.getTimeStr());

  if (accel.update()) {
    display.print(accel.getX());
    display.print(",");
    display.print(accel.getY());
    display.print(",");
    display.print(accel.getZ());
    display.println("");
  }

  int16_t adc0, adc1, adc2, adc3;

  adc0 = ads.readADC_SingleEnded(0);
  adc1 = ads.readADC_SingleEnded(1);
  adc2 = ads.readADC_SingleEnded(2);
  adc3 = ads.readADC_SingleEnded(3);
  display.print(adc0); display.print(",");
  display.print(adc1); display.print(",");
  display.print(adc2); display.print(",");
  display.println(adc3);

  ds18b20.requestTemperatures();
  byte retry = 5;
  float temp=0.0;
  do {
    temp = ds18b20.getTempCByIndex(0);
    retry--;
    delay(2);
  } while (retry > 0 && (temp == 85.0 || temp == (-127.0)));

  display.print(temp); display.println(" c");

  display.display();

  dac.setVoltage(dacOut, false);

  delay(50);

  display.clearDisplay();

  if (dacOut++>4095) dacOut=0;

} // end of main loop
