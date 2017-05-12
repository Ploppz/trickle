#include <stdint.h>
#include "tx.h"

#define PROTOCOL_ID 0x10203040

// Before using this module, ticker and radio should be initialized.

struct trickle_t;

// Initialize module
uint32_t
trickle_init(uint32_t first_ticker_id, uint32_t interval_min_ms, uint32_t interval_max_ms, uint32_t c_constant);

// Ends the current interval, returns the time at which the next interval should end.
void
trickle_next_interval(struct trickle_t *trickle);

uint8_t
get_packet_len(struct trickle_t *trickle);

uint8_t *
get_packet_data(struct trickle_t *trickle);


/* returns random number from min to max */
uint32_t 
rand(int min, int max);

/* get t value */
uint32_t
get_t_value(struct trickle_t *trickle);





