#ifndef TRICKLE_RADIO_H
#define TRICKLE_RADIO_H

#include <nrf.h>

#define MAX_PACKET_LEN 100

typedef enum {
  ADDR_PUBLIC = 0,
  ADDR_RANDOM = 1,
} address_type_t;

typedef enum {
    PDU_TYPE_ADV_IND = 0b0000,
    PDU_TYPE_SCAN_RSP = 0b0100,
} pdu_type_t;


#define ADV_CH37 0
#define ADV_CH38 12
#define ADV_CH39 39

// (probably not needed in this project, as it's done by PhoenixLL
void configure_radio(uint8_t* packet_ptr, uint8_t bt_channel, uint8_t rf_channel);
void start_hfclk();

void make_pdu_packet(pdu_type_t pdu_type, uint8_t *data, uint32_t data_len, uint8_t *dest,
        address_type_t address_type, uint8_t *dev_addr);
void transmit(uint8_t *adv_packet, uint8_t rf_channel);


uint32_t low_mask(uint8_t n);
void set_address0(uint32_t address);
uint32_t freq_mhz(uint8_t channel);

#endif
