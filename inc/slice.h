#ifndef TRICKLE_SLICE_H
#define TRICKLE_SLICE_H

#include <string.h>

typedef struct {uint8_t *ptr; uint32_t len;} slice_t;

inline slice_t new_slice(uint8_t *ptr, uint32_t len);

inline
slice_t
new_slice(uint8_t *ptr, uint32_t len) {
    return (slice_t) {
        .ptr = ptr,
        .len = len
    };
}

#endif
