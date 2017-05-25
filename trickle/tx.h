#ifndef TRICKLE_RADIO_H
#define TRICKLE_RADIO_H

#include <nrf.h>

typedef enum {
  ADDR_PUBLIC = 0,
  ADDR_RANDOM = 1,
} address_type_t;

typedef enum {
    PDU_TYPE_ADV_IND = 0b0000,
    PDU_TYPE_SCAN_RSP = 0b0100,
} pdu_type_t;


#define CH_INDEX0 1
#define CH_INDEX1 2
#define CH_INDEX2 3
#define CH_INDEX3 4
#define CH_INDEX4 5
#define CH_INDEX5 6
#define CH_INDEX6 7
#define CH_INDEX7 8
#define CH_INDEX8 9
#define CH_INDEX9 10
#define CH_INDEX10 11
#define CH_INDEX11 13
#define CH_INDEX12 14
#define CH_INDEX13 15
#define CH_INDEX14 16
#define CH_INDEX15 17
#define CH_INDEX16 18
#define CH_INDEX17 19
#define CH_INDEX18 20
#define CH_INDEX19 21
#define CH_INDEX20 22
#define CH_INDEX21 23
#define CH_INDEX22 24
#define CH_INDEX23 25
#define CH_INDEX24 26
#define CH_INDEX25 27
#define CH_INDEX26 28
#define CH_INDEX27 29
#define CH_INDEX28 30
#define CH_INDEX29 31
#define CH_INDEX30 32
#define CH_INDEX31 33
#define CH_INDEX32 34
#define CH_INDEX33 35
#define CH_INDEX34 36
#define CH_INDEX35 37
#define CH_INDEX36 38
#define CH_INDEX37 0
#define CH_INDEX38 12
#define CH_INDEX39 39

// The length before payload
#define PDU_HDR_LEN 3
#define DEV_ADDR_LEN 6

// (probably not needed in this project, as it's done by PhoenixLL
void configure_radio(uint8_t bt_channel, uint8_t rf_channel, uint32_t access_address);
void start_hfclk();

void
write_pdu_header(uint8_t pdu_type, uint32_t data_len, address_type_t address_type, uint8_t *dev_addr, uint8_t *dest);
void transmit(uint8_t *adv_packet, uint8_t rf_channel);


uint32_t low_mask(uint8_t n);
void set_address0(uint32_t address);
uint32_t freq_mhz(uint8_t channel);

#endif
