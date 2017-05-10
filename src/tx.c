#include <nrf51_bitfields.h>
#include <string.h>
#include "tx.h"

#define FREQ_BASE 2400

void set_address0(uint32_t address);
uint32_t freq_mhz(uint8_t channel);
void override_mode();

void start_hfclk() {
    // Enable HFCLK
    NRF_CLOCK->EVENTS_HFCLKSTARTED = 0;
    NRF_CLOCK->TASKS_HFCLKSTART = 1;
    while (NRF_CLOCK->EVENTS_HFCLKSTARTED == 0) {}
}
void configure_radio(uint8_t* packet_ptr, uint8_t bt_channel, uint8_t rf_channel) {
  NRF_RADIO->PCNF0 = (6 << RADIO_PCNF0_LFLEN_Pos) | 
                     (1 << RADIO_PCNF0_S0LEN_Pos) |
                     (2 << RADIO_PCNF0_S1LEN_Pos);
  // Setting LITTLE_ENDIAN make S0, LENGTH, S1 and PAYLOAD LSBit first
  NRF_RADIO->PCNF1 = (MAX_PACKET_LEN << RADIO_PCNF1_MAXLEN_Pos) |
                     (0 << RADIO_PCNF1_STATLEN_Pos) |
                     (3 << RADIO_PCNF1_BALEN_Pos) |
                     (RADIO_PCNF1_ENDIAN_Little << RADIO_PCNF1_ENDIAN_Pos) |
                     (1 << RADIO_PCNF1_WHITEEN_Pos);
  NRF_RADIO->PACKETPTR = (uint32_t)packet_ptr;
  NRF_RADIO->FREQUENCY = freq_mhz(rf_channel) - FREQ_BASE;
  NRF_RADIO->TXPOWER = RADIO_TXPOWER_TXPOWER_0dBm;
  NRF_RADIO->MODE = RADIO_MODE_MODE_Ble_1Mbit;

  set_address0(0x8E89BED6);

  NRF_RADIO->TXADDRESS = 0; // BASE0 + PREFIX0
  NRF_RADIO->RXADDRESSES = 0b1;
  
  /** CRC: **/
  // - 24 bits
  // - "All bits in the PDU shall be processed in transmitted order starting from the LSBit."
  // - Polynomial (exponents): 24, 10, 9, 6, 4, 3, 1, 0
  NRF_RADIO->CRCCNF = (3 << RADIO_CRCCNF_LEN_Pos) |
                      (1 << RADIO_CRCCNF_SKIPADDR_Pos);
  NRF_RADIO->CRCPOLY = (1<<24) | (1<<10) | (1<<9) | (1<<6) | (1<<4) | (1<<3) | (1<<1) | 1;
  NRF_RADIO->CRCINIT = 0x555555;

  //NRF_RADIO->TIFS = 150; // Microseconds.

  /** Whitening **/
  // b0 = 1
  // b1..b6 = channel number, LSBit first
  NRF_RADIO->DATAWHITEIV = bt_channel;

  override_mode();
}


#define ADVA_SIZE 6 // number of uint8_ts of advertising address

void make_pdu_packet(uint8_t pdu_type, uint8_t *data, uint32_t data_len, uint8_t *dest, address_type_t address_type, uint8_t *dev_addr) {
    dest[0] = (address_type << 6) | (pdu_type & low_mask(4));
    dest[1] = ADVA_SIZE + data_len;
    dest[2] = 0; // S1 is represented by one uint8_t but is not transmitted

    // Write device address
    for (int i = 0; i < ADVA_SIZE; i ++) {
        dest[i + 3] = dev_addr[i];
    }

    // Data
    memcpy(dest + 3 + ADVA_SIZE, data, data_len);
}

void disable_radio() {
    // If not already disabled, disable radio
    switch (NRF_RADIO->STATE) {
        case 0:
            break;
        case 1: // rx rampup
        case 2: // rx idle
            break;
        case 3: // rx
            NRF_RADIO->EVENTS_DISABLED = 0;
            NRF_RADIO->TASKS_DISABLE = 1;

            while (NRF_RADIO->EVENTS_DISABLED == 0) {}
            break;
    }
}

void transmit(uint8_t *adv_packet, uint8_t rf_channel) {
    NRF_RADIO->FREQUENCY = freq_mhz(rf_channel) - FREQ_BASE;
    NRF_RADIO->PACKETPTR = (uint32_t) adv_packet;
    /* NRF_RADIO->SHORTS |= 1 << RADIO_SHORTS_READY_START_Pos | */
                         /* 1 << RADIO_SHORTS_END_DISABLE_Pos; */
    NRF_RADIO->SHORTS = 0;

    int a = NRF_RADIO->STATE;

    disable_radio();

    NRF_RADIO->EVENTS_READY = 0;
    NRF_RADIO->TASKS_TXEN   = 1;

    while (NRF_RADIO->EVENTS_READY == 0) { }

    NRF_RADIO->EVENTS_END = 0;
    NRF_RADIO->TASKS_START = 1;

    while (NRF_RADIO->EVENTS_END == 0) { }

    NRF_RADIO->TASKS_DISABLE = 1;
}


/* Convenience functions */
uint32_t low_mask(uint8_t n) {
  return (1<<(n+1)) - 1;
}

void set_address0(uint32_t address) {
  uint32_t base = address << 8;
  NRF_RADIO->BASE0 = base;
  uint32_t prefix = (address >> 24) & low_mask(8);
  NRF_RADIO->PREFIX0 = prefix;\
}

uint32_t freq_mhz(uint8_t channel) {
  return 2402 + 2 * channel;
}


void override_mode() {
    uint32_t override_val = NRF_FICR->OVERRIDEEN & FICR_OVERRIDEEN_BLE_1MBIT_Msk;

    if (override_val == FICR_OVERRIDEEN_BLE_1MBIT_Override) {
        NRF_RADIO->OVERRIDE0 = NRF_FICR->BLE_1MBIT[0];
        NRF_RADIO->OVERRIDE1 = NRF_FICR->BLE_1MBIT[1];
        NRF_RADIO->OVERRIDE2 = NRF_FICR->BLE_1MBIT[2];
        NRF_RADIO->OVERRIDE3 = NRF_FICR->BLE_1MBIT[3];
        NRF_RADIO->OVERRIDE4 = NRF_FICR->BLE_1MBIT[4];
    }
}
