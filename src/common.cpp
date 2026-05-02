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