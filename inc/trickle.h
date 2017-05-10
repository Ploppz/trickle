#include <stdint.h>

/* Trickle instance */
typedef struct {
    uint32_t interval; // interval in microseconds / I
    uint32_t c_count; // consistency counter / c
    uint32_t t_timer; // current random time for transmit / t
    uint32_t draft;
    uint8_t draft_update_flag;
} trickle_t;

/* returns the value of update_draft_flag */
uint8_t 
get_draft_update_flag(trickle_t *trickle);

uint8_t
set_draft_update_flag(trickle_t *trickle, uint8_t value);

/* returns random number from min to max */
uint32_t 
rand(int min, int max);

/* get t value */
uint32_t
get_t_value(trickle_t *trickle);

/* get next drift value for the radio timer. */
uint32_t
get_next_radio_drift(trickle_t *trickle);

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


typedef struct {
    uint32_t interval_min; // in microseconds
    uint32_t interval_max; // in microseconds
    uint8_t k_constant; // consistency constant / k 
    uint32_t interval; // interval in microseconds / I
    uint8_t c_count; // consistency counter / c
    uint32_t t_timer; // current random time for transmit / t
} trickle2_t;