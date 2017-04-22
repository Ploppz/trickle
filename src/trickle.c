#include "trickle.h"

#define min(a,b) ((a) < (b) ? (a) : (b))


uint32_t
next_interval(trickle_t *trickle) {
    trickle->interval = min(trickle->interval * 2, trickle_config.interval_max);
    return trickle->interval;
}

// returns 8 bit random number from min to max
uint8_t 
rng(int min, int max){
  float divisor = 0xFF / (max - min);
  * (int *) 0x4000D000 = 1; // Start task
  while (* (int *) 0x4000D100 == 0) {} // while Valrdy event == 0
  * (int *) 0x4000D004 = 1; // Stop task
  float random_number = * (int *) 0x4000D508; // Value
  return min + random_number / divisor;
}


uint32_t
trickle_init(trickle_t *trickle) {
    trickle->interval = trickle_config.interval_min /* TODO random */;
    trickle->c_count = 0;
}
