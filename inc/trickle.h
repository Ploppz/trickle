#include <stdint.h>

/* Trickle instance */
typedef struct {
    uint32_t interval; // interval in microseconds / I
    uint32_t c_count; // consistency counter / c
} trickle_t;

uint8_t 
rng(int min, int max);

/* Initialize trickle struct */
uint32_t
trickle_init(trickle_t *trickle);


/* Ends the current interval, returns the time at which the next interval should end.
 */
uint32_t
next_interval(trickle_t *trickle);


/* Configuration, which should be global to the node */
typedef struct {
    uint32_t interval_min; // in microseconds
    uint32_t interval_max; // in microseconds
    uint32_t c_constant; // consistency constant / k
} trickle_config_t;

extern trickle_config_t trickle_config;
