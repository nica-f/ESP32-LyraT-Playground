//
// ESP32 LyratT playground
// (c) Nicole Faerber
// based on example code from
// https://github.com/earlephilhower/ESP8266Audio
// (c) Earle F. Philhower, III
// License: GPLv3
//

#include <Arduino.h>

#include "ES8388.h"

// IO18 SDA
// IO23 SCL
// 400kHz
ES8388 es8388(18, 23, 400000);


#if defined(ARDUINO_ARCH_RP2040)
void setup() {}
void loop() {}

#else
#if defined(ESP32)
    #include <WiFi.h>
#else
    #include <ESP8266WiFi.h>
#endif
#include "AudioFileSourceICYStream.h"
#include "AudioFileSourceBuffer.h"
#include "AudioGeneratorMP3.h"
#include "AudioOutputI2S.h"

// To run, set your ESP8266 build to 160MHz, update the SSID info, and upload.

// Enter your WiFi setup here:
#ifndef STASSID
#define STASSID "YOUR SSID HERE"
#define STAPSK  "YOUR WPA PASSPHRASE HERE"
#endif

const char* ssid = STASSID;
const char* password = STAPSK;

static uint8_t vol=20;
static bool tp_vol_chg=false;
static bool hp_det_chg=false;

// Randomly picked URL
// const char *URL="http://kvbstreams.dyndns.org:8000/wkvi-am";
const char *URL="http://liveradio.swr.de/sw282p3/swr3/play.mp3";

AudioGeneratorMP3 *mp3;
AudioFileSourceICYStream *file;
AudioFileSourceBuffer *buff;
AudioOutputI2S *out;

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
  Serial.printf("METADATA(%s) '%s' = '%s'\n", ptr, s1, s2);
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
  Serial.printf("STATUS(%s) '%d' = '%s'\n", ptr, code, s1);
  Serial.flush();
}

static void IRAM_ATTR tp_play_cb(void)
{

}

static void IRAM_ATTR tp_set_cb(void)
{

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

void setup()
{
  Serial.begin(115200);
  pinMode(12, INPUT); // AUX detect
  pinMode(19, INPUT); // HP detect
  pinMode(21, OUTPUT); // AMP CTRL
  pinMode(22, OUTPUT); // LED green
  digitalWrite(21, 0); // mute AMP

  Serial.printf("xtal: %d MHz\n", getXtalFrequencyMhz());
  setCpuFrequencyMhz(160);

  Serial.println("Read Reg ES8388 : ");
  if (!es8388.init())
    Serial.println("Init Fail");
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
  uint8_t *reg;
  for (uint8_t i = 0; i < 53; i++) {
    reg = es8388.readAllReg();
    Serial.printf("Reg-%02d = 0x%02x\r\n", i, reg[i]);
  }

  delay(1000);
  Serial.println("Connecting to WiFi");

  WiFi.disconnect();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  
  WiFi.begin(ssid, password);

  // Try forever
  while (WiFi.status() != WL_CONNECTED) {
    Serial.println("...Connecting to WiFi");
    delay(1000);
  }
  Serial.println("Connected");

  audioLogger = &Serial;
  file = new AudioFileSourceICYStream(URL);
  file->RegisterMetadataCB(MDCallback, (void*)"ICY");
  buff = new AudioFileSourceBuffer(file, 2048);
  buff->RegisterStatusCB(StatusCallback, (void*)"buffer");
  out = new AudioOutputI2S();
  mp3 = new AudioGeneratorMP3();
  mp3->RegisterStatusCB(StatusCallback, (void*)"mp3");
  mp3->begin(buff, out);

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
}


void loop()
{
  static int lastms = 0;

  if (mp3->isRunning()) {
    if (millis()-lastms > 500) {
      lastms = millis();
      digitalWrite(22, !digitalRead(22));
      Serial.printf("Running for %d ms... %d\n", lastms, digitalRead(19));
      if (tp_vol_chg) {
        Serial.printf("vol chg %d\n", vol);
        es8388.setOutputVolume(vol);
        tp_vol_chg=false;
      }
      Serial.flush();
    }
    if (!mp3->loop())
      mp3->stop();
  } else {
    Serial.println("MP3 done\n");
    delay(1000);
  }
  if (hp_det_chg) {
    Serial.println("hp det chg\n");
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
  // let the CPU sleep for some cycles
  delay(2);
  // esp_light_sleep_start();
}
#endif
