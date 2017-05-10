#include <stdint.h>

#define PROTOCOL_ID 0x10203040

typedef struct __attribute__((packed)) {
    uint32_t protocol_ID;
    uint8_t  instance_ID;
    uint32_t version_ID;
} trickle_pdu_t;

/* Trickle instance */
typedef struct __attribute__((packed)) {
    uint32_t interval; // interval in microseconds / I
    uint32_t c_count; // consistency counter / c
    trickle_pdu_t pdu;
} trickle_t;


void
pdu_handle(trickle_t *trickle, uint8_t *packet_ptr, uint8_t packet_len);


uint8_t
get_packet_len(trickle_t *trickle);

uint8_t *
get_packet_data(trickle_t *trickle);

/* returns random number from min to max */
uint32_t 
rand(int min, int max);

/* get t value */
uint32_t
get_t_value(trickle_t *trickle);

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
