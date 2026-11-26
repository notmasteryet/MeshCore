#pragma once

#include <Arduino.h>
#include <helpers/ESP32Board.h>
#include <esp_bt.h>

#ifndef CYD_VBAT_MULT
  #define CYD_VBAT_MULT 2.48f   // tweak per unit if a multimeter disagrees
#endif

// Modified ESP32-2432S028 "Cheap Yellow Display" with an LR1121 on VSPI.
// Pin map taken from the Meshtastic port:
// https://github.com/notmasteryet/firmware/tree/cyd-24-v2/variants/esp32/cyd
class CYDBoard : public ESP32Board {
public:
  void begin() {
    // Only BLE is used: hand the Bluetooth Classic controller's reserved RAM back to the heap.
    // Must run before the BT controller is initialised (BLE starts later in setup()).
    esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    ESP32Board::begin();
    // RGB LED is common-anode (active LOW): keep it dark.
    const uint8_t leds[] = { PIN_LED_R, PIN_LED_G, PIN_LED_B };
    for (uint8_t p : leds) { pinMode(p, OUTPUT); digitalWrite(p, HIGH); }
#ifdef MESH_DEBUG
    Serial.printf("CYD: battery %u mV\n", getBattMilliVolts());
#endif
  }

  // Battery divider is 1.48M / 1M (x2.48, as in the Meshtastic port); ESP32Board assumes x2.
  // The divider is high-impedance, so take more samples and let the ADC settle.
  uint16_t getBattMilliVolts() override {
    analogReadResolution(12);
    analogSetPinAttenuation(PIN_VBAT_READ, ADC_11db);
    analogReadMilliVolts(PIN_VBAT_READ);   // discard first sample
    uint32_t raw = 0;
    for (int i = 0; i < 16; i++) raw += analogReadMilliVolts(PIN_VBAT_READ);
    return (uint16_t)((raw / 16) * CYD_VBAT_MULT);
  }

  const char* getManufacturerName() const override {
    return "CYD LR1121";
  }
};
