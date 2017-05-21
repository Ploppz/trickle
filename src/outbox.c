#include <stdint.h>

#include "rio.h"
#include "tx.h"

#include "ticker.h"

/////////////////////
// Circular buffer //
/////////////////////

// Elements are in [outbox_head, outbox_tail)

packet_t outbox[OUTBOX_N_PACKETS];
uint32_t outbox_head       = 0;
uint32_t outbox_tail       = 0;

packet_t inbox[OUTBOX_N_PACKETS];
uint32_t inbox_head       = 0;
uint32_t inbox_tail       = 0;

packet_t *
outbox_push() {
    uint32_t next_outbox_tail = (outbox_tail+1) % OUTBOX_N_PACKETS;
    if (next_outbox_tail == outbox_head) {
        // OVERFLOW
        return 0;
    } else {
        uint32_t prev_outbox_tail = outbox_tail;
        outbox_tail = next_outbox_tail;
        outbox[prev_outbox_tail].final = 0;
        outbox[prev_outbox_tail].in_progress = 0;
        return &outbox[prev_outbox_tail];
    }
}

packet_t *
outbox_front() {
    if (outbox_head == outbox_tail || !outbox[outbox_head].final) {
        return 0;
    } else {
        return &outbox[outbox_head];
    }
}

void
outbox_pop_front() {
    if (outbox_head == outbox_tail || !outbox[outbox_head].final) {
        // EMPTY
    } else {
        outbox_head = (outbox_head+1) % OUTBOX_N_PACKETS;
    }
}


packet_t *
inbox_push() {
    uint32_t next_inbox_tail = (inbox_tail+1) % OUTBOX_N_PACKETS;
    if (next_inbox_tail == inbox_head) {
        // OVERFLOW
        return 0;
    } else {
        uint32_t prev_inbox_tail = inbox_tail;
        inbox_tail = next_inbox_tail;
        inbox[prev_inbox_tail].final = 0;
        inbox[prev_inbox_tail].in_progress = 0;
        return &inbox[prev_inbox_tail];
    }
}

packet_t *
inbox_front() {
    if (inbox_head == inbox_tail || !inbox[inbox_head].final) {
        return 0;
    } else {
        return &inbox[inbox_head];
    }
}

void
inbox_pop_front() {
    if (inbox_head == inbox_tail || !inbox[inbox_head].final) {
        // EMPTY
    } else {
        inbox_head = (inbox_head+1) % OUTBOX_N_PACKETS;
    }
}


////////////////////////
// Radio input/output //
////////////////////////

enum state_t {
    TX,
    TXRU,
    RX,
    RXRU,
};

static state_t state = RX;

void rio_isr_radio() {
    if (state == TX) {
        NRF_RADIO->EVENTS_END = 0;

        { // Test assumption/logic
            packet_t *old_packet = outbox_front();
            ASSERT(old_packet->in_progress == 1);
        }

        // Cleanup - free the packet
        outbox_pop_front();

        if (outbox_len() > 0) {
            // Transmit again
            packet_t *packet = outbox_front();
            packet->in_progress = 1;
            NRF_RADIO->PACKETPTR = (uint32_t) packet->data;
            NRF_RADIO->TASKS_START = 1;
        } else {
            // Ramp-down & start scanning
        }

        // TODO if no more packets, start RX

    } else if (state == RX) {
        if (NRF_RADIO->EVENTS_DEVMATCH) {
            NRF_RADIO->EVENTS_DEVMATCH = 0;
            // TODO Get & set new packet ptr. Else stop scanning (buffer full)
        } else if (NRF_RADIO->EVENTS_END) {
            NRF_RADIO->EVENTS_END = 0;
            NRF_RADIO->TASKS_START = 1;
        }


        // TODO perhaps here also, if pending TX, start TX
    }
}

void
rio_timeout(uint32_t ticks_at_expire, uint32_t remainder, uint16_t lazy, void *context) {
    if (state == RX && outbox_len() > 0) {
        if (state == RX) {
            // TODO ramp-down, start TX
        }
    }
}

void
rx_rampup() {
    NRF_RADIO->TASKS_RXEN = 1;
}
void
rio_schedule_tx() {
    packet_t *packet = front();
    if (packet == 0 || packet->in_progress == 1) {
        return;
    }

    packet->in_progress = 1;

    start_hfclk();
    configure_radio(packet->data, rio_config.bt_channel, rio_config.rf_channel);


    NRF_RADIO->TASKS_TXEN   = 1;
}

///////////////
// Interface //
///////////////

void 
rio_init(uint32_t interval_us) {
    NRF_RADIO->SHORTS   = RADIO_SHORTS_READY_START_Msk;
    NRF_RADIO->INTENCLR = ~0;
    NRF_RADIO->INTENSET = RADIO_INTENSET_END_Msk | RADIO_INTENSET_DEVMATCH_Msk;

    rx_rampup();
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
rio_start_packet() {
    packet_t *ptr = push();
    return ptr;
}

// Signal that the packet is done - no more writing
void
rio_finalize_packet(packet_t *handle) {
    handle->final = 1;
}


uint8_t
outbox_len() {
    if (outbox_tail < outbox_head) {
        return OUTBOX_N_PACKETS + outbox_tail - outbox_head;
    } else { // outbox_head <= outbox_tail
        return outbox_tail - outbox_head;
    }
}
