#include "trickle.h"
#include <string.h>
#include "nrf.h"

// Structures, helper functions and interface for trickle

static uint8_t values[N_TRICKLE_NODES];
// Memory for trickle instances
static uint8_t instances[N_TRICKLE_NODES][TRICKLE_T_SIZE];

// Access structure to map (address <-> index)
typedef struct {
    uint8_t present;
    uint8_t address[6];
} address_index_t;

static address_index_t addresses[N_TRICKLE_NODES];
static uint16_t addresses_top = 0;


/* Translation from address to index. If address isn't yet indexed, give it an index.
   Note: In this application, a key consists of two 6-byte addresses.
*/
uint32_t
toggle_get_index(slice_t address) {

    for (int i = 0; i < N_TRICKLE_NODES; i ++) {
        if (addresses[i].present) {
            if (memcmp(addresses[i].address, address.ptr, 6) == 0) {
                return i;
            }
        } else {
            break;
        }
    }

    // The address is not found. Give it an index.
    uint32_t new_index = addresses_top++;
    addresses[new_index].present = 1;
    memcpy(addresses[new_index].address, address.ptr, 6);
    return new_index;
}

uint16_t
get_instance_index(uint8_t *instance) {

    return (instance - (uint8_t*)instances) / TRICKLE_T_SIZE;
}

///////////////
// Interface //
///////////////
void
toggle_app_init() {
    trickle_init((struct trickle_t*)instances, N_TRICKLE_NODES);
}

// Generate key based on the index of the trickle instance in the array
// Return number of bytes written do `dest`
uint8_t
toggle_get_key(uint8_t *instance, uint8_t *dest) {
    uint16_t i = get_instance_index(instance);
    // Copy the addresses to dest
    memcpy(dest,   addresses[i].address, 6);
    return 6;
}

// Write data of a trickle instance to `dest`. Returns bytes written
slice_t
toggle_get_val(uint8_t *instance) {
    uint16_t i = get_instance_index(instance);
    return new_slice(&values[i], 1);
}

struct trickle_t*
toggle_get_instance(slice_t key) {
    uint32_t i = toggle_get_index(new_slice(key.ptr, 6));
    NRF_GPIO->OUTSET = (i & 0b11) << 23;
    NRF_GPIO->OUTCLR = ((~i) & 0b11) << 23;
    return (struct trickle_t *) &instances[i];
}
