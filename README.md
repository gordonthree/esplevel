# esplevel

WiFi enabled 3-axis accelerometer. Intended to be installed in my RV trailer so I can check how level my parking spot is from my cell phone.

![proto board image](http://i.imgur.com/Q2TS3Iel.jpg)

Progress so far:

added more MQTT topics, now reporting RSSI and also epoch timestamp

same data is available via websocket server on port 81 

To do yet:

Need to an HTML + Javascript client page and figure out how to do the graphical representation of the accelerometer data

Trouble:

This PlatformIO project doesn't seem to compile on other machines, complaining about a missing file WiFiSETUP.h ... I'm not sure where that dependency is coming from, I've gone through all the libraries I'm using and it's not turning up.