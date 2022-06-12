# ESP32-LyraT-Playground
Arduino playground and test program for the ESP32 LyraT board

You will need my fork of the ESP8266Audio project to build it:
https://github.com/nica-f/ESP8266Audio-LyraT
This fork contains the necessary updated pin definitions for the ESP32-LyraT board.

Right now this application will init the LyraT, init audio using the ESP8266Audio library, init the DAC (aka codec) though the ES8388 library (included code), connect to WiFi (you need to setup your WiFi SSID and WPA Passphrase) and play my favorite radio station SWR3. I have wired up the touch buttons of the LyraT to interrupts so that Vol+ and Vol- will increase or decrease the volume.

To save some energy I have reduced the core frequency to 160MHz and added a small delay in the main loop() - and indeed the ESP32 gets significantly less warm. The audio output will switch automatically when a headphone is inserted and vc.vs. Also this is triggere thorugh an interrupt.

This is only my test application to play aroud with things, expect stuff to break. But I wanted to publish it anyway since I have not found a lot of projects for the LyraT board outside the ESP32 SDK.

Enjoy!
And keep hacking...
