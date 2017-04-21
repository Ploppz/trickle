#include "trickle.h"

#define min(a,b) ((a) < (b) ? (a) : (b))


uint32_t
next_interval(trickle_t *trickle) {
    trickle->interval = min(trickle->interval * 2, trickle_config.interval_max);
    return trickle->interval;
}

// returns 8 bit random number from 0 to 0xFF
uint8_t 
rng(){
  * (int *) 0x4000D000 = 1; // Start task
  while (* (int *) 0x4000D100 == 0) {} // while Valrdy event == 0
  * (int *) 0x4000D004 = 1; // Stop task
  uint8_t random_number = * (int *) 0x4000D508; // Value
  return random_number;
}


uint32_t
trickle_init(trickle_t *trickle) {
    trickle->interval = trickle_config.interval_min /* TODO random */;
    trickle->c_count = 0;
}
