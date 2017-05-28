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


/** \brief  Struct that stores information about radio configuration. 
* 
* \param    bt_channel              Bluetooth channel used.
* \param    rf_channel              Radio frequency used.
* \param    access_addr             Access address used.
* \param    update_interval_us      Interval for timer that checks outbox. 
*/
struct rio_config_t {
    uint8_t bt_channel;
    uint8_t rf_channel;
    uint32_t access_addr;
    uint32_t update_interval_us;
};
typedef struct rio_config_t rio_config_t;


/** \brief Struct used for storing incoming and outgoing packets. 
*
* \param    data                    Array that contains packet data.
* \param    rssi                    Rssi value of incoming packets.
* \param    state                   Used only internally, should not be touched by
*                                   application.
*/
struct packet_t {
    uint8_t data[MAX_PACKET_LEN];
    uint8_t rssi;
    uint8_t state;
};
typedef struct packet_t packet_t;


/** \brief Needs to be defined by application
*
*/
extern rio_config_t rio_config;


/** \brief Function called by radio ISR handler. Handles incoming and outgoing
*          packets.
*
*/
void
rio_isr_radio(void);


/** \brief Rio module initialization. 
*
*/
void 
rio_init(void);


/** \brief  Pushes a new packet to outbox. Packet should be written to `packet_t->data`.
*   \return Packet to which to write. Remember to call `rio_tx_finalize_packet` when done.
*/
packet_t *
rio_tx_start_packet(void);



/** \brief Changes the state of the packet to signal that it is complete.
*          Must be called before transmission.
*
* param[in] packet                  Pointer to packet should to be 
*                                   transmitted.
*/
void
rio_tx_finalize_packet(packet_t *packet);


/** \brief  Gets the first packet in the inbox queue. The scanner will overwrite
*           the start of the queue when the queue gets full. 
*   \return NULL if no packet, else the first received packet in the queue.
*/
packet_t *
rio_rx_get_packet(void);

#endif
