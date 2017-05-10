#include "trickle.h"
#include "nrf.h"

#define min(a,b) ((a) < (b) ? (a) : (b))

void
pdu_handle(uint8_t *packet_ptr, trickle_t *trickle){
    uint32_t protocol_ID = *(uint32_t*) packet_ptr;
    packet_ptr += sizeof(protocol_ID);

    uint8_t  instance_ID = *packet_ptr;
    packet_ptr += sizeof(instance_ID);

    uint32_t version_ID = *(uint32_t*) packet_ptr;
    packet_ptr += sizeof(version_ID);

    uint8_t *data_ptr = packet_ptr;
    
    if (protocol_ID != PROTOCOL_ID)
        return;
    
    // TODO cmp instance id

    if (version_ID < trickle->version_ID) {
        // TODO broadcast
        // TODO reset i
    } else if (version_ID > trickle->version_ID) {
        // TODO update own data
        // TODO reset interval to i_min
    } else {
        trickle->c_count ++;
    }

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
    trickle->protocol_ID = 0xffeeffee;
    trickle->instance_ID = 123;
    trickle->version_ID = 0x33333333;
}
