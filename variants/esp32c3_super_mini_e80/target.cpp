#include <Arduino.h>
#include "target.h"

SuperMiniE80Board board;

// E80-900M2213S does not conform to Semtech reference RF switch layout.
// Its DIO7 is not connected, so GNSS/WiFi scanning are unavailable.
// See: https://www.cdebyte.com/products/E80-900M2213S/2#Pin (footnote #3)
static const uint32_t rfswitch_dios[Module::RFSWITCH_MAX_PINS] = {
  RADIOLIB_LR11X0_DIO5, RADIOLIB_LR11X0_DIO6, RADIOLIB_NC, RADIOLIB_NC, RADIOLIB_NC
};

static const Module::RfSwitchMode_t rfswitch_table[] = {
  // mode                 DIO5   DIO6
  { LR11x0::MODE_STBY,   {LOW,  LOW  }},
  { LR11x0::MODE_RX,     {LOW,  LOW  }},
  { LR11x0::MODE_TX,     {LOW,  HIGH }},   // RFO_LP_LF path
  { LR11x0::MODE_TX_HP,  {HIGH, LOW  }},   // RFO_HP_LF path
  { LR11x0::MODE_TX_HF,  {LOW,  LOW  }},
  { LR11x0::MODE_GNSS,   {LOW,  LOW  }},
  { LR11x0::MODE_WIFI,   {LOW,  LOW  }},
  END_OF_MODE_TABLE,
};

RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, SPI);

WRAPPER_CLASS radio_driver(radio, board);

ESP32RTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);
SensorManager sensors;

#ifndef LORA_CR
  #define LORA_CR 5
#endif

bool radio_init() {
  fallback_clock.begin();
  rtc_clock.begin(Wire);

  SPI.begin(P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI, P_LORA_NSS);

#ifdef LR11X0_DIO3_TCXO_VOLTAGE
  float tcxo = LR11X0_DIO3_TCXO_VOLTAGE;
#else
  float tcxo = 1.8f;
#endif

  int status = radio.begin(LORA_FREQ, LORA_BW, LORA_SF, LORA_CR, RADIOLIB_LR11X0_LORA_SYNC_WORD_PRIVATE, LORA_TX_POWER, 16, tcxo);
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

uint32_t radio_get_rng_seed() {
  return radio.random(0x7FFFFFFF);
}

void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr) {
  radio.setFrequency(freq);
  radio.setSpreadingFactor(sf);
  radio.setBandwidth(bw);
  radio.setCodingRate(cr);
}

void radio_set_tx_power(int8_t dbm) {
  radio.setOutputPower(dbm);
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);
}
