#include <Arduino.h>
#include "target.h"

CYDBoard board;

// LR1121 on VSPI (the display + touch own HSPI).
static SPIClass spi(VSPI);
RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, spi);
WRAPPER_CLASS radio_driver(radio, board);

ESP32RTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);
SensorManager sensors;

#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display;
  MomentaryButton user_btn(PIN_USER_BTN, 1000, true, true);
#endif

#ifndef LORA_CR
  #define LORA_CR 5
#endif

// DIO5/DIO6 drive the sub-GHz T/R switch (same table as the Meshtastic CYD port).
static const uint32_t rfswitch_dios[Module::RFSWITCH_MAX_PINS] = {
  RADIOLIB_LR11X0_DIO5,
  RADIOLIB_LR11X0_DIO6,
  RADIOLIB_NC
};

static const Module::RfSwitchMode_t rfswitch_table[] = {
  // mode                 DIO5  DIO6
  { LR11x0::MODE_STBY,   {LOW,  LOW}},
  { LR11x0::MODE_RX,     {HIGH, LOW}},
  { LR11x0::MODE_TX,     {LOW,  HIGH}},
  { LR11x0::MODE_TX_HP,  {LOW,  HIGH}},
  { LR11x0::MODE_TX_HF,  {LOW,  LOW}},
  { LR11x0::MODE_GNSS,   {LOW,  LOW}},
  { LR11x0::MODE_WIFI,   {LOW,  LOW}},
  END_OF_MODE_TABLE,
};

bool rtc_hw_trusted = false;

bool radio_init() {
  fallback_clock.begin();
  rtc_clock.begin(Wire);   // I2C on GPIO21/22 (started in ESP32Board::begin)
  {
    Serial.print("I2C scan (SDA 21, SCL 22):");
    int found = 0;
    for (uint8_t a = 0x08; a < 0x78; a++) {
      Wire.beginTransmission(a);
      if (Wire.endTransmission() == 0) { Serial.printf(" 0x%02X", a); found++; }
    }
    Serial.println(found ? "" : " none");
    // DS3231 sanity: status reg 0x0F (bit7 OSF = oscillator stopped / lost power), temp regs 0x11-0x12
    Wire.beginTransmission(0x68); Wire.write(0x0F);
    if (Wire.endTransmission() == 0 && Wire.requestFrom(0x68, 4) == 4) {
      uint8_t st = Wire.read(); Wire.read(); int8_t th = (int8_t)Wire.read(); uint8_t tl = Wire.read();
      rtc_hw_trusted = !(st & 0x80) && rtc_clock.getCurrentTime() > 1772323200UL + 7 * 86400UL;
      Serial.printf("RTC 0x68: status 0x%02X (OSF %d), temp %.2f C\n", st, (st >> 7) & 1, th + (tl >> 6) * 0.25f);
    }
    Serial.printf("Clock at boot: %lu (fallback %lu)%s\n", (unsigned long)rtc_clock.getCurrentTime(),
                  (unsigned long)fallback_clock.getCurrentTime(), rtc_hw_trusted ? " trusted (RTC)" : " unconfirmed");
  }

  spi.begin(P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI);
  int status = radio.begin(LORA_FREQ, LORA_BW, LORA_SF, LORA_CR, RADIOLIB_LR11X0_LORA_SYNC_WORD_PRIVATE,
                           LORA_TX_POWER, 16, LR11X0_DIO3_TCXO_VOLTAGE);
  if (status != RADIOLIB_ERR_NONE) {
    Serial.print("ERROR: radio init failed: ");
    Serial.println(status);
    return false;
  }

  radio.setCRC(2);
  radio.explicitHeader();
  radio.setRfSwitchTable(rfswitch_dios, rfswitch_table);
#ifdef RX_BOOSTED_GAIN
  radio.setRxBoostedGainMode(RX_BOOSTED_GAIN);
#endif
  return true;
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);
}
