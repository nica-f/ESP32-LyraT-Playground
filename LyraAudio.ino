//
// ESP32 LyratT playground
// (c) Nicole Faerber
// based on example code from
// https://github.com/earlephilhower/ESP8266Audio
// (c) Earle F. Philhower, III
// License: GPLv3
//

#include <Arduino.h>

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <FS.h>
#include <SPIFFS.h>

#include <ESP32Console.h>

#include <AudioFileSourceICYStream.h>
#include <AudioFileSourceBuffer.h>
#include <AudioGeneratorMP3.h>
#include <AudioGeneratorAAC.h>
#include <AudioOutputI2S.h>

#include "ES8388.h"
// IO18 SDA
// IO23 SCL
// 400kHz
ES8388 es8388(18, 23, 400000);

// Enter your WiFi setup here:
#ifndef STASSID
#define STASSID "YOUR SSID HERE"
#define STAPSK  "YOUR WPA PASSPHRASE HERE"
#endif

const char* ssid = STASSID;
const char* password = STAPSK;

#define ANYPLAY_RECEIVER_PORT 4424

WebServer server(ANYPLAY_RECEIVER_PORT);

using namespace ESP32Console;
Console console;

static uint8_t vol=20;
static bool tp_vol_chg=false;
static bool hp_det_chg=false;
static bool play_btn_press=false;
static bool set_btn_press=false;

// Randomly picked URL
// const char *URL="http://kvbstreams.dyndns.org:8000/wkvi-am";
//const char *URL="http://liveradio.swr.de/sw282p3/swr3/play.mp3";
char URL[128];
#define DEFAULT_URL "http://liveradio.swr.de/sw282p3/swr3/play.mp3"
//#define DEFAULT_URL "https://edge66.streams.klassikradio.de/beats-radio/stream/mp3"

AudioGeneratorMP3 *mp3 = NULL;
AudioFileSourceICYStream *file = NULL;
AudioFileSourceBuffer *buff = NULL;
AudioOutputI2S *out = NULL;

const int preallocateBufferSize = 16*1024;
const int preallocateCodecSize = 85332; // AAC+SBR codec max mem needed
void *preallocateBuffer = NULL;
void *preallocateCodec = NULL;


// Called when a metadata event occurs (i.e. an ID3 tag, an ICY block, etc.
void MDCallback(void *cbData, const char *type, bool isUnicode, const char *string)
{
  const char *ptr = reinterpret_cast<const char *>(cbData);
  (void) isUnicode; // Punt this ball for now
  // Note that the type and string may be in PROGMEM, so copy them to RAM for printf
  char s1[32], s2[64];
  strncpy_P(s1, type, sizeof(s1));
  s1[sizeof(s1)-1]=0;
  strncpy_P(s2, string, sizeof(s2));
  s2[sizeof(s2)-1]=0;
  printf("METADATA(%s) '%s' = '%s'\n", ptr, s1, s2);
  Serial.flush();
}

// Called when there's a warning or error (like a buffer underflow or decode hiccup)
void StatusCallback(void *cbData, int code, const char *string)
{
  const char *ptr = reinterpret_cast<const char *>(cbData);
  // Note that the string may be in PROGMEM, so copy it to RAM for printf
  char s1[64];
  strncpy_P(s1, string, sizeof(s1));
  s1[sizeof(s1)-1]=0;
  printf("STATUS(%s) '%d' = '%s'\n", ptr, code, s1);
}

static void IRAM_ATTR tp_play_cb(void)
{
  play_btn_press=true;
}

static void IRAM_ATTR tp_set_cb(void)
{
  set_btn_press=true;
}

static void IRAM_ATTR tp_vol_dec_cb(void)
{
  if (tp_vol_chg)
    return;
  tp_vol_chg=true;
  if (vol>0)
    vol--;
}

static void IRAM_ATTR tp_vol_inc_cb(void)
{
  if (tp_vol_chg)
    return;
  tp_vol_chg=true;
  vol++;
  if (vol>33)
    vol=33;
}

static void IRAM_ATTR hp_detect_cb(void)
{
  hp_det_chg=true;
}

void handleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.sendHeader("Connection", "close");
  server.send(404, "text/plain", message);
}

void handleRoot() {
  server.sendHeader("Connection", "close");
  server.send(200, "text/plain", "hello from esp32!");
}

void handleCmd() {
  String message = "CMD\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.sendHeader("Connection", "close");
  server.send(200, "text/plain", message);
}

int stream_stop(int argc, char **argv)
{
    printf("stopping stream playback\n");
#if 0
    if (mp3 != NULL) {
      mp3->stop();
      delete mp3;
      mp3 = NULL;
    }
#endif
#if 0
    printf("2\n");
    if (buff != NULL) {
      buff->close();
      delete buff;
      buff=NULL;
    }
#endif
#if 1
    printf("1\n");
    if (file != NULL) {
      file->close();
      delete file;
      file = NULL;
      // drain file?
      delay(200);
    }
    printf("done\n");
#endif

  return EXIT_SUCCESS; // EXIT_FAILURE
}

#define FILE_BUFFER_SIZE 2048

void create_decoder_pipeline(void)
{
    // are we leaking objects here?
    file = new AudioFileSourceICYStream(URL);
    file->RegisterMetadataCB(MDCallback, (void*)"ICY");

    buff = new AudioFileSourceBuffer(file, preallocateBuffer, preallocateBufferSize);
    buff->RegisterStatusCB(StatusCallback, (void*)"buffer");

    // mp3 = isACC ? new AudioGeneratorAAC() : new AudioGeneratorMP3();
    mp3 = new AudioGeneratorMP3(preallocateCodec, preallocateCodecSize);
    //mp3 = new AudioGeneratorMP3();
    mp3->RegisterStatusCB(StatusCallback, (void*)"mp3");
    mp3->begin(buff, out);
}

int stream_play(int argc, char **argv)
{
  if (mp3 == NULL) {
    printf("starting stream playback\n");
    create_decoder_pipeline();
  } else
    printf("stream playback already running?\n");
  return EXIT_SUCCESS; // EXIT_FAILURE
}

int stream_source(int argc, char **argv)
{
  if (argc != 2) {
    printf("current source: '%s'\n", URL);
  } else {
    printf("setting new source to: '%s'\n", argv[1]);
    strcpy(URL, argv[1]);
  }
  return EXIT_SUCCESS; // EXIT_FAILURE
}

void setup()
{
  //Serial.begin(115200);
  console.setPrompt("LyraT> ");
  console.begin(115200);
  console.registerSystemCommands();
  console.registerCommand(ConsoleCommand("stop", &stream_stop, "stop playback of current stream"));
  console.registerCommand(ConsoleCommand("play", &stream_play, "start playback of current stream"));
  console.registerCommand(ConsoleCommand("source", &stream_source, "set new stream source to URL"));

  pinMode(12, INPUT); // AUX detect
  pinMode(19, INPUT); // HP detect
  pinMode(21, OUTPUT); // AMP CTRL
  pinMode(22, OUTPUT); // LED green
  digitalWrite(21, 0); // mute speaker AMP

  printf("xtal: %d MHz\n", getXtalFrequencyMhz());
  setCpuFrequencyMhz(160);

  printf("Read Reg ES8388 : ");
  if (!es8388.init())
    printf("Init Fail");
  //es8388.inputSelect(IN2);
  //es8388.setInputGain(8);
  if (digitalRead(19)) {
    es8388.outputSelect(OUT2); // speaker
    digitalWrite(21, 1);
  } else {
    es8388.outputSelect(OUT1); // headphone
  }
  es8388.setOutputVolume(vol);
  es8388.mixerSourceSelect(MIXADC, MIXADC);
  es8388.mixerSourceControl(DACOUT);
  es8388.DACmute(false);
#if 0
  uint8_t *reg;
  for (uint8_t i = 0; i < 53; i++) {
    reg = es8388.readAllReg();
    printf("Reg-%02d = 0x%02x\r\n", i, reg[i]);
  }
#endif
  // delay(100);
  printf("Connecting to WiFi");

  // First, preallocate all the memory needed for the buffering and codecs, never to be freed
  preallocateBuffer = malloc(preallocateBufferSize);
  preallocateCodec = malloc(preallocateCodecSize);
  if (!preallocateBuffer || !preallocateCodec) {
    printf("FATAL ERROR:  Unable to preallocate %d bytes for app\n", preallocateBufferSize+preallocateCodecSize);
    while (1) delay(1000); // Infinite halt
  }
  
  WiFi.disconnect();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  
  WiFi.begin(ssid, password);

  // Try forever
  while (WiFi.status() != WL_CONNECTED) {
    printf("...Connecting to WiFi");
    delay(500);
  }
  printf("Connected");

  /*use mdns for host name resolution*/
  if (!MDNS.begin("AnyplaySpeaker")) {
    printf("Error setting up MDNS responder!");
  } else {
    printf("mDNS responder started");
  }

  MDNS.setInstanceName("Living room");
  MDNS.addService("http", "tcp", ANYPLAY_RECEIVER_PORT);

  server.on("/", handleRoot);
  server.on("/cmd", handleCmd);
  server.onNotFound(handleNotFound);
  server.begin();
  printf("HTTP server started");
  
  strcpy(URL, DEFAULT_URL);
  // audioLogger = &Serial;
  out = new AudioOutputI2S();

#if 0
  file = new AudioFileSourceICYStream(URL);
  file->RegisterMetadataCB(MDCallback, (void*)"ICY");

  buff = new AudioFileSourceBuffer(file, preallocateBuffer, preallocateBufferSize);
  buff->RegisterStatusCB(StatusCallback, (void*)"buffer");

  mp3 = new AudioGeneratorMP3();
  mp3->RegisterStatusCB(StatusCallback, (void*)"mp3");
  mp3->begin(buff, out);
else
  create_decoder_pipeline();
#endif

#define TP_THRESHOLD 30
  touchAttachInterrupt(T8, tp_play_cb, TP_THRESHOLD);
  touchAttachInterrupt(T9, tp_set_cb, TP_THRESHOLD);
  touchAttachInterrupt(T4, tp_vol_dec_cb, TP_THRESHOLD);
  touchAttachInterrupt(T7, tp_vol_inc_cb, TP_THRESHOLD);

  attachInterrupt(19, hp_detect_cb, CHANGE);

//  esp_bluedroid_disable(); esp_bt_controller_disable();
#if 0
  esp_pm_config_esp32_t pm_cfg;
  pm_cfg.max_freq_mhz=160;
  pm_cfg.min_freq_mhz=10;
  pm_cfg.light_sleep_enable=true;
  esp_pm_configure(&pm_cfg);
#endif

  printf("Total heap: %d\n", ESP.getHeapSize());
  printf("Free heap: %d\n", ESP.getFreeHeap());
  printf("Total PSRAM: %d\n", ESP.getPsramSize());
  printf("Free PSRAM: %d\n", ESP.getFreePsram());
}


void loop()
{
  static int lastms = 0;

  if (mp3 != NULL) {
    if (mp3->isRunning()) {
      if (millis()-lastms > 500) {
        lastms = millis();
        digitalWrite(22, !digitalRead(22));
        // printf("Running for %d ms... %d\n", lastms, digitalRead(19));
        if (tp_vol_chg) {
          printf("vol chg %d\n", vol);
          es8388.setOutputVolume(vol);
          tp_vol_chg=false;
        }
      }
      if (!mp3->loop()) {
        printf("mp3 loop gone\n");
        //stream_stop(0,NULL);
#if 1
        //mp3->stop();
        //stream_stop(0,NULL);
        if (mp3 != NULL) {
          mp3->stop();
          delete mp3;
          mp3 = NULL;
          delay(200); // drain decoder buffer
        }
#endif
#if 1
        if (buff != NULL) {
          buff->close();
          delete buff;
          buff=NULL;
        }
#endif
#if 0
        if (file != NULL) {
          file->close();
          delete file;
          file = NULL;
          // drain file?
          delay(200);
        }
#endif
      }
    } else {
      printf("MP3 done\n");
//      stream_stop(0,NULL);
      delay(100);
    }
  }
  if (hp_det_chg) {
    printf("hp det chg\n");
    if (digitalRead(19)) {
        es8388.outputSelect(OUT2); // speaker
        es8388.setOutputVolume(vol);
        digitalWrite(21, 1);
    } else {
        digitalWrite(21, 0);
        es8388.outputSelect(OUT1); // headphone
        es8388.setOutputVolume(vol);
    }
  }
  if (play_btn_press) {
    printf("Play btn\n");
    if (mp3 != NULL) {
      stream_stop(0,NULL);
    } else {
      stream_play(0, NULL);
    }
    delay(200);
    play_btn_press=false;
  }
  if (set_btn_press) {
    set_btn_press=false;
    printf("Set btn\n");
  }

  // handle the web server
  server.handleClient();

  // let the CPU sleep for some cycles
  delay(2);
  // esp_light_sleep_start();
}
