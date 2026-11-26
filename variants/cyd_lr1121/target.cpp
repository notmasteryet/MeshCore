#include <Arduino.h>
#include "target.h"

CYDBoard board;

// LR1121 on VSPI (the display + touch own HSPI).
static SPIClass spi(VSPI);
RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, spi);
WRAPPER_CLASS radio_driver(radio, board);

ESP32RTCClock rtc_clock;
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

bool radio_init() {
  rtc_clock.begin();

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
