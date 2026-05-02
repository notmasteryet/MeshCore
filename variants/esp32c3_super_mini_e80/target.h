#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <helpers/ESP32Board.h>
#include <helpers/radiolib/CustomLR1110.h>
#include <helpers/radiolib/CustomLR1110Wrapper.h>
#include <helpers/AutoDiscoverRTCClock.h>
#include <helpers/SensorManager.h>

class SuperMiniE80Board : public ESP32Board {
public:
  uint16_t getBattMilliVolts() override {
#ifdef PIN_VBAT_READ
    analogReadResolution(12);
    uint32_t raw = 0;
    for (int i = 0; i < 4; i++) raw += analogReadMilliVolts(PIN_VBAT_READ);
    raw /= 4;
    // V_bat = V_adc * (VBAT_DIV_TOP + VBAT_DIV_BOT) / VBAT_DIV_BOT
    return (uint16_t)(raw * (VBAT_DIV_TOP + VBAT_DIV_BOT) / VBAT_DIV_BOT);
#else
    return 0;
#endif
  }
};

extern SuperMiniE80Board board;
extern WRAPPER_CLASS radio_driver;
extern AutoDiscoverRTCClock rtc_clock;
extern SensorManager sensors;

bool radio_init();
uint32_t radio_get_rng_seed();
void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr);
void radio_set_tx_power(int8_t dbm);
mesh::LocalIdentity radio_new_identity();
