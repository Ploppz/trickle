#include <stdint.h>

#include "rio.h"
#include "tx.h"

// PhoenixLL
#include "ticker.h"
#include "ctrl.h"
#include "debug.h"



/////////////////////
// Circular buffer //
/////////////////////

// Elements are in [head, tail)

static packet_t outbox[RIO_N_PACKETS];
static uint32_t outbox_head       = 0;
static uint32_t outbox_tail       = 0;

static packet_t inbox[RIO_N_PACKETS];
static uint32_t inbox_head       = 0;
static uint32_t inbox_tail       = 0;

packet_t *
outbox_push() {
    uint32_t next_outbox_tail = (outbox_tail+1) % RIO_N_PACKETS;
    if (next_outbox_tail == outbox_head) {
        // Full buffer
        return 0;
    } else {
        uint32_t prev_outbox_tail = outbox_tail;
        outbox_tail = next_outbox_tail;
        outbox[prev_outbox_tail].complete = 0;
        outbox[prev_outbox_tail].transmitting = 0;
        return &outbox[prev_outbox_tail];
    }
}

packet_t *
outbox_front() {
    if (outbox_head == outbox_tail || !outbox[outbox_head].complete) {
        return 0;
    } else {
        return &outbox[outbox_head];
    }
}

void
outbox_pop_front() {
    if (outbox_head == outbox_tail || !outbox[outbox_head].complete) {
        // EMPTY
    } else {
        outbox_head = (outbox_head+1) % RIO_N_PACKETS;
    }
}

uint8_t
outbox_len() {
    if (outbox_tail < outbox_head) {
        return RIO_N_PACKETS + outbox_tail - outbox_head;
    } else { // outbox_head <= outbox_tail
        return outbox_tail - outbox_head;
    }
}

packet_t *
inbox_push() {
    uint32_t next_inbox_tail = (inbox_tail+1) % RIO_N_PACKETS;
    if (next_inbox_tail == inbox_head) {
        // Full buffer - the oldest packet is forgotten
        inbox_head = (inbox_head+1) % RIO_N_PACKETS;
    }
    uint32_t prev_inbox_tail = inbox_tail;
    inbox_tail = next_inbox_tail;
    inbox[prev_inbox_tail].garbage = 0;
    inbox[prev_inbox_tail].complete = 0;
    return &inbox[prev_inbox_tail];
}

// Inbox..

void
inbox_free_garbage();

packet_t *
inbox_front() {
    ASSERT(!inbox[inbox_head].garbage); // (just to test)
    // TODO skip garbage

    if (inbox_head == inbox_tail || inbox[inbox_head].complete) {
        return 0;
    } else {
        return &inbox[inbox_head];
    }
}

packet_t *
inbox_back() {
    uint32_t tail = (inbox_tail + RIO_N_PACKETS - 1) % RIO_N_PACKETS;
    if (inbox_head == inbox_tail) {
        return 0;
    } else {
        return &inbox[tail];
    }
}

void
inbox_pop_front() {
    if (inbox_head == inbox_tail || inbox[inbox_head].complete) {
        // EMPTY
    } else {
        inbox_head = (inbox_head+1) % RIO_N_PACKETS;
    }
}

void
inbox_free_garbage() {
    // Remove packets from beginning whose garbage=1, stop when reach packet with garbage=0.
    uint32_t i = inbox_head;
    while (i != inbox_tail) {
        if (inbox[i].garbage) {
            inbox_pop_front();
        } else {
            break;
        }
        i = (i+1) % RIO_N_PACKETS;
    }
}


////////////////////////
// Radio input/output //
////////////////////////

enum state_t {
    STATE_NONE,
    STATE_TX,
    STATE_RX,
};
typedef enum state_t state_t;

static state_t state = STATE_NONE;

void
rx_new_packet() {
    packet_t *packet = inbox_push();
    NRF_RADIO->PACKETPTR = (uint32_t) packet->data;
}

uint32_t
check_event(volatile uint32_t *event) {
    uint32_t ret = *event;
    *event = 0;
    return ret;
}

void
clear_radio_events() {
    NRF_RADIO->EVENTS_READY = 0;   
    NRF_RADIO->EVENTS_ADDRESS = 0; 
    NRF_RADIO->EVENTS_PAYLOAD = 0; 
    NRF_RADIO->EVENTS_END = 0;     
    NRF_RADIO->EVENTS_DISABLED = 0;
    NRF_RADIO->EVENTS_DEVMATCH = 0;
    NRF_RADIO->EVENTS_DEVMISS = 0; 
    NRF_RADIO->EVENTS_RSSIEND = 0; 
    NRF_RADIO->EVENTS_BCMATCH = 0; 
}



void
rio_isr_radio() {
    if (state == STATE_TX) {
        ASSERT(NRF_RADIO->STATE != RADIO_STATE_STATE_Rx
            && NRF_RADIO->STATE != RADIO_STATE_STATE_RxIdle);
        if (NRF_RADIO->EVENTS_DISABLED) {

            NRF_RADIO->TASKS_TXEN = 1;

        } else if (NRF_RADIO->EVENTS_END) {
            // Free memory of previous transmission
            
            packet_t *old_packet = outbox_front();
            // The following IF should always be true but that changes when setting breakpoints
            if (old_packet && old_packet->transmitting == 1) {
                outbox_pop_front(); /* TODO REENTRANT */
            }
        }

        if (NRF_RADIO->EVENTS_END || NRF_RADIO->EVENTS_READY) {
            // Initiate new transmission
            if (outbox_len() > 0) {
                // Transmit again
                packet_t *packet = outbox_front();
                packet->transmitting = 1;
                NRF_RADIO->PACKETPTR = (uint32_t) packet->data;
                NRF_RADIO->TASKS_START = 1;
            } else {
                // Ramp-down & start scanning
                state = STATE_RX;
                NRF_RADIO->TASKS_DISABLE = 1;
            }
        }
    }
    else
    if (state == STATE_RX) {
        ASSERT(NRF_RADIO->STATE != RADIO_STATE_STATE_Tx
            && NRF_RADIO->STATE != RADIO_STATE_STATE_TxIdle);

        if (NRF_RADIO->EVENTS_END) {

            rx_new_packet();
            packet_t *packet = inbox_front(); /* TODO REENTRANT */
            packet->complete = 1;
            // TODO stop scanning if buffer full
            NRF_RADIO->TASKS_START = 1;

        } else if (NRF_RADIO->EVENTS_READY) {

            NRF_RADIO->TASKS_START = 1;

        }
        
        else if (NRF_RADIO->EVENTS_DISABLED) {
            NRF_RADIO->TASKS_RXEN = 1;
        }
    }

    clear_radio_events();
}

void
rio_timeout(uint32_t ticks_at_expire, uint32_t remainder, uint16_t lazy, void *context) {
    uint32_t len = outbox_len();
    if (state == STATE_RX && len > 0) {
        if (state == STATE_RX) {
            state = STATE_TX;
            NRF_RADIO->TASKS_DISABLE = 1;
        } else if (state == STATE_NONE) {
            state = STATE_TX;
            NRF_RADIO->TASKS_TXEN = 1;
        }
    }
}

void
rio_schedule_tx() {
    packet_t *packet = outbox_front();
    if (packet == 0 || packet->transmitting == 1) {
        return;
    }

    packet->transmitting = 1;


    NRF_RADIO->TASKS_TXEN   = 1;
}

///////////////
// Interface //
///////////////

void 
rio_init(uint32_t interval_us) {
    clear_radio_events();
    NRF_RADIO->SHORTS   = 0;
    NRF_RADIO->INTENCLR = ~0;
    NRF_RADIO->INTENSET = RADIO_INTENSET_READY_Msk
                        | RADIO_INTENSET_END_Msk
                        | RADIO_INTENSET_DISABLED_Msk;

    start_hfclk();
    configure_radio(rio_config.bt_channel, rio_config.rf_channel);

    rx_new_packet();

    state = STATE_RX;
    NRF_RADIO->TASKS_RXEN = 1;

    uint32_t retval = ticker_start(RADIO_TICKER_INSTANCE_ID_RADIO // instance
        , MAYFLY_CALL_ID_PROGRAM // user
        , 0 // ticker id
        , ticker_ticks_now_get() // anchor point
        , TICKER_US_TO_TICKS(interval_us) // first interval
        , TICKER_US_TO_TICKS(interval_us) // periodic interval
        , TICKER_REMAINDER(interval_us) // remainder
        , 0 // lazy
        , 0 // slot
        , rio_timeout // timeout callback function
        , 0 // context
        , 0 // op func
        , 0 // op context
        );
    ASSERT(retval == TICKER_STATUS_SUCCESS);
}



packet_t *
rio_tx_start_packet() {
    return outbox_push();
}

// Signal that the packet is done - no more writing
void
rio_tx_finalize_packet(packet_t *packet) {
    packet->complete = 1;
}


packet_t *
rio_rx_get_packet() {
    return inbox_front();
}

void
rio_rx_free_packet(packet_t *packet) {
    packet->garbage = 1;
    inbox_free_garbage();
}
