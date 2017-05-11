#include "trickle.h"
#include "nrf.h"

#define min(a,b) ((a) < (b) ? (a) : (b))

void
pdu_handle(trickle_t *trickle, uint8_t *packet_ptr, uint8_t packet_len) {
    if (packet_len < sizeof(trickle_pdu_t)) {
        return;
    }

    trickle_pdu_t *pdu = (trickle_pdu_t*) packet_ptr;

    int a = pdu->protocol_ID;
    
    if (pdu->protocol_ID != PROTOCOL_ID) {
        return;
    }

    if (pdu->instance_ID != 0) {
        return;
    }
    
    if (pdu->version_ID < trickle->pdu.version_ID) {
        // TODO broadcast own data
        // TODO reset i
    } else if (pdu->version_ID > trickle->pdu.version_ID) {
        // Update own data
        trickle->pdu.version_ID = pdu->version_ID;
        // FOR DEMO: Set leds:
        NRF_GPIO->OUTSET = (0b1111 << 21);
        switch ((pdu->version_ID) & 0x03) {
            case 1:
                NRF_GPIO->OUTCLR = (1 << 21);
                break;

            case 2:
                NRF_GPIO->OUTCLR = (1 << 22);
                break;

            case 3:
                NRF_GPIO->OUTCLR = (1 << 23);
                break;

            case 0:
                NRF_GPIO->OUTCLR = (1 << 24);
                break;
        }
        // TODO reset interval to i_min
    } else {
        trickle->c_count ++;
    }

}

uint8_t
get_packet_len(trickle_t *trickle) {
    return sizeof(trickle_pdu_t);
}

uint8_t *
get_packet_data(trickle_t *trickle) {
    return (uint8_t*) (&trickle->pdu);
}


uint32_t
next_interval(trickle_t *trickle) {
    trickle->interval = min(trickle->interval * 2, trickle_config.interval_max);
    return trickle->interval;
}

uint32_t
get_t_value(trickle_t *trickle){
    return rand(trickle->interval/2, trickle->interval-1);
}

uint32_t 
rand(int min, int max){
    NRF_RNG->EVENTS_VALRDY = 0;
    NRF_RNG->TASKS_START = 1;
    while(NRF_RNG->EVENTS_VALRDY == 0){
      // Wait for value to be ready
    }
    uint32_t random_number = NRF_RNG->VALUE;
    NRF_RNG->TASKS_STOP = 1;
    while(NRF_RNG->TASKS_START == 1){
      //wait for rand to stop
    }
    random_number *= max - min;
    random_number /= 0xFF;
    return min + random_number;
}

uint32_t
trickle_init(trickle_t *trickle) {
    trickle->interval = trickle_config.interval_min;
    trickle->c_count = 0;
    trickle->pdu.protocol_ID = PROTOCOL_ID;
    trickle->pdu.instance_ID = 0;
    trickle->pdu.version_ID = 0;
}
