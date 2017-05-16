#ifndef POSITIONING_H
#define POSITIONING_H
#include "trickle.h"

void
positioning_init();

uint8_t
get_key      (uint8_t *instance, uint8_t *dest);

slice_t
get_val     (uint8_t *instance);

struct trickle_t*
get_instance (slice_t key);

#endif
