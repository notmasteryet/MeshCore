#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <helpers/radiolib/CustomLR1121Wrapper.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/SensorManager.h>
#include "CYDBoard.h"
#ifdef DISPLAY_CLASS
  #include "CYDDisplay.h"
  #include <helpers/ui/MomentaryButton.h>
#endif

extern CYDBoard board;
extern WRAPPER_CLASS radio_driver;
extern ESP32RTCClock rtc_clock;
extern SensorManager sensors;

#ifdef DISPLAY_CLASS
  extern DISPLAY_CLASS display;
  extern MomentaryButton user_btn;
#endif

bool radio_init();
mesh::LocalIdentity radio_new_identity();
