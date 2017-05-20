#include <stdint.h>

#include "outbox.h"
#include "tx.h"

/////////////////////
// Circular buffer //
/////////////////////

// Elements are in [head, tail)
packet_t packets[OUTBOX_N_PACKETS];
uint32_t head = 0;
uint32_t tail = 0;

packet_t *
push() {
    uint32_t next_tail = (tail+1) % OUTBOX_N_PACKETS;
    if (next_tail == head) {
        // OVERFLOW
        return 0;
    } else {
        uint32_t prev_tail = tail;
        tail = next_tail;
        packets[prev_tail].final = 0;
        return &packets[prev_tail];
    }
}

packet_t *
front() {
    if (head == tail || !packets[head].final) {
        return 0;
    } else {
        return &packets[head];
    }
}

void
pop_front() {
    if (head == tail || !packets[head].final) {
        // EMPTY
    } else {
        head = (head+1) % OUTBOX_N_PACKETS;
    }
}

/////////////
// Helpers //
/////////////


void outbox_isr_radio() {
    pop_front();
    // TODO Transmit more than one packet in the given slot
    
}

///////////////
// Interface //
///////////////

void
schedule() {
    packet_t *packet = front();
    if (packet == 0) {
        return;
    }

    start_hfclk();
    configure_radio(packet->data, outbox_config.bt_channel, outbox_config.rf_channel);

    // TODO: Should do this with interrupts
    NRF_RADIO->SHORTS   = RADIO_SHORTS_READY_START_Msk;
                        // | RADIO_SHORTS_END_START_Msk;

    NRF_RADIO->INTENCLR = ~0;
    NRF_RADIO->INTENSET = RADIO_INTENSET_END_Msk;

    NRF_RADIO->TASKS_TXEN   = 1;
}


packet_t *
start_packet() {
    packet_t *ptr = push();
    return ptr;
}

// Signal that the packet is done - no more writing
void
finalize_packet(packet_t *handle) {
    handle->final = 1;
}
