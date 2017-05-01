#include "trickle.h"
#include "NRF.h"

#define min(a,b) ((a) < (b) ? (a) : (b))


uint32_t
next_interval(trickle_t *trickle) {
    trickle->interval = min(trickle->interval * 2, trickle_config.interval_max);
    return trickle->interval;
}

uint16_t
next_t(trickle_t *trickle){
  return rng(trickle->interval/2, trickle->interval-1);
}

uint16_t 
rng(int min, int max){
  NRF_RNG->EVENTS_VALRDY = 0;
  NRF_RNG->TASKS_START = 1;
  while(NRF_RNG->EVENTS_VALRDY == 0){
    // Wait for value to be ready
  }
  uint32_t random_number = NRF_RNG->VALUE;
  NRF_RNG->TASKS_STOP = 1;
  while(NRF_RNG->TASKS_START == 1){
    //wait for rng to stop
  }
  random_number *= max - min;
  random_number /= 0xFF;
  return min + random_number;
}

uint32_t
trickle_init(trickle_t *trickle) {
    trickle->interval = trickle_config.interval_min;
    trickle->c_count = 0;
}
