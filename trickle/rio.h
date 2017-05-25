#ifndef OUTBOX_H
#define OUTBOX_H

/* Module description
 * Scanning and transmitting with only one slot-less ticker timer.
 * A current shortcoming is thus that it will work well with other
 * applications that require radio sharing.
 *
 * Inbox for scanning and Outbox for transmitting, are circular buffers.
 *
 * If inbox gets full, the oldest packet gets overwritten.
 *   - earlier attempt with a "grabage" flag was risky because of context-unsafety
 * If outbox gets full, pushing a packet will return 0
 *
 * At the moment, hence:
 *   - Main context pushes outbox, pops inbox
 *   - ISR context pushes inbox, pops outbox
 * And it's presumably not 100% context-safe yet.
 */

struct rio_config_t {
    uint8_t bt_channel;
    uint8_t rf_channel;
    uint32_t access_addr;
    uint32_t update_interval_us;
};
typedef struct rio_config_t rio_config_t;


struct packet_t {
    uint8_t data[MAX_PACKET_LEN];
    uint8_t state;
    uint8_t rssi; // used only for RX
};
typedef struct packet_t packet_t;

extern rio_config_t rio_config;

void
rio_isr_radio();

void 
rio_init();

// TX
/* Push new packet to outbox. Packet should be written to `packet_t->data`. */
packet_t *
rio_tx_start_packet();

/* Signal that the packet is done. Must be done before transmission.
 * If not done, the outbox will stall forever...
 */
void
rio_tx_finalize_packet(packet_t *packet);

// RX
packet_t *
rio_rx_get_packet();
void
rio_rx_free_packet(packet_t *packet);

#endif
