#include "trickle.h"
#include "nrf.h"

#define min(a,b) ((a) < (b) ? (a) : (b))


uint32_t
next_interval(trickle_t *trickle) {
    trickle->interval = min(trickle->interval * 2, trickle_config.interval_max);
    return trickle->interval;
}

uint32_t
get_t_value(trickle_t *trickle){
    uint32_t rand_num = rand(trickle->interval/2, trickle->interval-1);
    return rand_num;
}


uint32_t
get_next_radio_drift(trickle_t *trickle){
    if(trickle->t_timer == 0){
        uint32_t rand_num = get_t_value(trickle);
        trickle->t_timer = rand_num;
        return rand_num;
    }
    uint32_t rand_num = get_t_value(trickle);
    uint32_t retval = (rand_num - (2 * trickle->t_timer - trickle->interval/2));
    trickle->t_timer = rand_num;
    return  retval;
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
    trickle->t_timer = 0;
}
