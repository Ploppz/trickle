#include <stdint.h>

#include "rio.h"
#include "tx.h"

// PhoenixLL
#include "ticker.h"
#include "ctrl.h"
#include "debug.h"

#define TX_ALLOCATED 0      // Allocated for writing
#define TX_COMPLETE 1       // The packet has been written to and is ready
#define TX_TRANSMITTING 2

#define RX_ALLOCATED 0      // The packet will be written to
#define RX_COMPLETE 1       // The packet has been written to and is ready


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
        outbox[prev_outbox_tail].state = TX_ALLOCATED;
        return &outbox[prev_outbox_tail];
    }
}

// If front is only allocated, not complete or transmitting, return 0.
packet_t *
outbox_front() {
    if (outbox_head == outbox_tail || outbox[outbox_head].state == TX_ALLOCATED) {
        return 0;
    } else {
        return &outbox[outbox_head];
    }
}

// If front is only allocated, not complete or transmitting, nothing happens.
void
outbox_pop_front() {
    if (outbox_head == outbox_tail || outbox[outbox_head].state == TX_ALLOCATED) {
        // EMPTY
    } else {
        outbox_head = (outbox_head+1) % RIO_N_PACKETS;
    }
}

uint8_t
outbox_pending() {
    return outbox_head != outbox_tail
        && outbox[outbox_head].state == TX_COMPLETE;
    /* Previous code: outbox_len
     * problem: needs to count only packets with TX_COMPLETE
    if (outbox_tail < outbox_head) {
        return RIO_N_PACKETS + outbox_tail - outbox_head;
    } else { // outbox_head <= outbox_tail
        return outbox_tail - outbox_head;
    }
    */
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
    inbox[prev_inbox_tail].state = RX_ALLOCATED;
    return &inbox[prev_inbox_tail];
}

// Inbox..

packet_t *
inbox_front() {
    if (inbox_head == inbox_tail || inbox[inbox_head].state != RX_COMPLETE) {
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

packet_t *
inbox_pop_front() {
    if (inbox_head == inbox_tail || inbox[inbox_head].state != RX_COMPLETE) {
        // EMPTY
    } else {
        uint32_t old_head = inbox_head;
        inbox_head = (inbox_head+1) % RIO_N_PACKETS;
        return &inbox[old_head];
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
            ASSERT(old_packet);
            ASSERT(old_packet->state == TX_TRANSMITTING);

            outbox_pop_front(); /* TODO REENTRANT */
        } else if (NRF_RADIO->EVENTS_READY) {

            NRF_RADIO->SHORTS = 0;

        }

        if (NRF_RADIO->EVENTS_END || NRF_RADIO->EVENTS_READY) {
            // Initiate new transmission
            if (outbox_pending()) {
                // Transmit again
                packet_t *packet = outbox_front();
                ASSERT(packet->state == TX_COMPLETE);
                packet->state = TX_TRANSMITTING;
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
            // Get RSSI
            NRF_RADIO->TASKS_RSSISTOP = 1;
            uint8_t rssi = NRF_RADIO->RSSISAMPLE;
            // Mark the previous packet as 'completed'
            packet_t *packet = inbox_back();
            packet->state = RX_COMPLETE;
            packet->rssi = rssi;

            // Start new
            rx_new_packet();
            NRF_RADIO->TASKS_START = 1;

        } else if (NRF_RADIO->EVENTS_READY) {

            NRF_RADIO->SHORTS = RADIO_SHORTS_ADDRESS_RSSISTART_Msk;
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
    if (outbox_pending()) {
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
    if (packet == 0 || packet->state == TX_TRANSMITTING) {
        return;
    }

    packet->state = TX_TRANSMITTING;

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
    packet->state = TX_COMPLETE;
}


packet_t *
rio_rx_get_packet() {
    return inbox_pop_front();
}
