#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <helpers/radiolib/CustomLR1121Wrapper.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/AutoDiscoverRTCClock.h>
#include <helpers/SensorManager.h>
#include "CYDBoard.h"
#ifdef DISPLAY_CLASS
  #include "CYDDisplay.h"
  #include <helpers/ui/MomentaryButton.h>
#endif

extern CYDBoard board;
extern WRAPPER_CLASS radio_driver;
extern AutoDiscoverRTCClock rtc_clock;   // uses an I2C RTC (DS3231, PCF8563, RV3028...) if fitted
extern SensorManager sensors;
extern bool rtc_hw_trusted;   // DS3231 found with its oscillator running (OSF clear): boot time is good

#ifdef DISPLAY_CLASS
  extern DISPLAY_CLASS display;
  extern MomentaryButton user_btn;
#endif

bool radio_init();
mesh::LocalIdentity radio_new_identity();
