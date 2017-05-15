#ifndef TRICKLE_SLICE_H
#define TRICKLE_SLICE_H
typedef struct {uint8_t *data; uint32_t len} slice_t;

inline slice_t new_slice(uint8_t *data, uint32_t len) {
    return (slice_t) {
        .data = data,
        .len = len
    };
}
#endif
