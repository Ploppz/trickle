#ifndef POSITIONING_H
#define POSITIONING_H

#include "trickle.h"

void
toggle_app_init();

uint8_t
toggle_get_key(uint8_t *instance, uint8_t *dest);

slice_t
toggle_get_val(uint8_t *instance);


struct trickle_t*
toggle_get_instance (slice_t key);

#endif
