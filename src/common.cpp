#include <Arduino.h>
#include <Preferences.h>
#include <FastLED.h>
#include <LEDMatrix.h>
#include "common.h"

cLEDMatrix<-MATRIX_WIDTH, -MATRIX_HEIGHT, VERTICAL_ZIGZAG_MATRIX> leds;

int32_t page_index=0,subpage_index=0;

int load_config(String key){
  Preferences prefs;
  prefs.begin("bottle", true);
  int v=prefs.getInt(key.c_str());
  prefs.end();
  return v;
}

void save_config(String key,int value){
  Preferences prefs;
  prefs.begin("bottle", false);
  prefs.putInt(key.c_str(),value);
  prefs.end();
}

String load_config_string(String key){
  Preferences prefs;
  prefs.begin("bottle", true);
  String v = prefs.getString(key.c_str(), "");
  prefs.end();
  return v;
}

void save_config_string(String key, String value){
  Preferences prefs;
  prefs.begin("bottle", false);
  prefs.putString(key.c_str(), value.c_str());
  prefs.end();
}

float load_config_float(String key){
  Preferences prefs;
  prefs.begin("bottle", true);
  float v = prefs.getFloat(key.c_str(), 0.0f);
  prefs.end();
  return v;
}

void save_config_float(String key, float value){
  Preferences prefs;
  prefs.begin("bottle", false);
  prefs.putFloat(key.c_str(), value);
  prefs.end();
}

// 带命名空间的配置函数
void save_config_ns(String ns, String key, int value) {
  Preferences prefs;
  prefs.begin(ns.c_str(), false);
  prefs.putInt(key.c_str(), value);
  prefs.end();
}

int load_config_ns(String ns, String key) {
  Preferences prefs;
  prefs.begin(ns.c_str(), true);
  int v = prefs.getInt(key.c_str(), 0);
  prefs.end();
  return v;
}

void save_config_string_ns(String ns, String key, String value) {
  Preferences prefs;
  prefs.begin(ns.c_str(), false);
  prefs.putString(key.c_str(), value.c_str());
  prefs.end();
}

String load_config_string_ns(String ns, String key) {
  Preferences prefs;
  prefs.begin(ns.c_str(), true);
  String v = prefs.getString(key.c_str(), "");
  prefs.end();
  return v;
}

void save_config_float_ns(String ns, String key, float value) {
  Preferences prefs;
  prefs.begin(ns.c_str(), false);
  prefs.putFloat(key.c_str(), value);
  prefs.end();
}

float load_config_float_ns(String ns, String key) {
  Preferences prefs;
  prefs.begin(ns.c_str(), true);
  float v = prefs.getFloat(key.c_str(), 0.0f);
  prefs.end();
  return v;
}