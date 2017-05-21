#ifndef OUTBOX_H
#define OUTBOX_H

struct rio_config_t {
    uint8_t bt_channel;
    uint8_t rf_channel;
};
typedef struct rio_config_t rio_config_t;


struct packet_t {
    uint8_t data[MAX_PACKET_LEN];
    uint8_t final;
    uint8_t in_progress;
};
typedef struct packet_t packet_t;

extern rio_config_t rio_config;

void
rio_isr_radio();

void 
rio_init(uint32_t interval_us);
// TX
packet_t *
rio_tx_start_packet();
void
rio_tx_finalize_packet(packet_t *packet);

// RX
packet_t *
rio_rx_get_packet();
void
rio_rx_free_packet(packet_t *packet);

#endif
