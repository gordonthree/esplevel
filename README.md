# esplevel

WiFi enabled 3-axis accelerometer. Intended to be installed in my RV trailer so I can check how level my parking spot is from my cell phone.

![proto board image](http://i.imgur.com/Q2TS3Iel.jpg)

Progress so far:

code still compiles to fit (via OTA) on an 1mbit ESP01/07 module with 64kbit set aside for SPIFFS
intended for a more spacious ESP12 or ESP7s

added more MQTT topics, now reporting RSSI and also epoch timestamp

same data is available via websocket server on port 81 

basic html websock client is now operational, with a pretty graph even

![web app image](http://i.imgur.com/Xl5L78ol.png)

To do yet:

Need to improve html client to better emulate a spirt level.

Trouble:

