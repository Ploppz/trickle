#include "trickle.h"

#define min(a,b) ((a) < (b) ? (a) : (b))


uint32_t
next_interval(trickle_t *trickle) {
    trickle->interval = min(trickle->interval * 2, trickle_config.interval_max);
    return trickle->interval;
}


uint32_t
trickle_init(trickle_t *trickle) {
    trickle->interval = trickle_config.interval_min /* TODO random */;
    trickle->c_count = 0;
}
