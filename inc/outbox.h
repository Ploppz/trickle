#ifndef OUTBOX_H
#define OUTBOX_H

struct outbox_config_t {
    uint8_t bt_channel;
    uint8_t rf_channel;
};
typedef struct outbox_config_t outbox_config_t;


struct packet_t {
    uint8_t data[MAX_PACKET_LEN];
    uint8_t final;
};
typedef struct packet_t packet_t;

extern outbox_config_t outbox_config;

void
schedule();

packet_t *
start_packet();

// Signal that the packet is done - no more writing
void
finalize_packet(packet_t *handle);



void
outbox_isr_radio();
#endif
