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

packet_t *
rio_start_packet();

// Signal that the packet is done - no more writing
void
rio_finalize_packet(packet_t *handle);

void
rio_isr_radio();
#endif
