#include "trickle.h"
#include <string.h>

// Structures, helper functions and interface for trickle

static uint8_t data[N_TRICKLE_NODES][N_TRICKLE_NODES];
// Memory for trickle instances
static uint8_t instances[N_TRICKLE_NODES][N_TRICKLE_NODES][TRICKLE_T_SIZE];

// Access structure to map (address <-> index)
typedef struct {
    uint8_t present;
    uint8_t address[6];
} address_index_t;

static address_index_t addresses[N_TRICKLE_NODES];
static uint16_t addresses_top = 0;


void
positioning_init() {
    trickle_init(instances, N_TRICKLE_NODES * N_TRICKLE_NODES);
}
/* Translation from address to index. If address isn't yet indexed, give it an index.
   Note: In this application, a key consists of two 6-byte addresses.
*/
uint32_t
get_index(slice_t address) {
    for (int i = 0; i < N_TRICKLE_NODES; i ++) {
        if (addresses[i].present) {
            if (memcmp(addresses[i].address, address.data, 6) == 0) {
                return i;
            }
        } else {
            break;
        }
    }

    // The address is not found. Give it an index.
    uint32_t new_index = addresses_top++;
    memcpy(addresses[new_index].address, address.data, 6);
    return new_index;
}

void
get_double_index(uint8_t *instance, uint16_t *i, uint16_t *j) {

    uint32_t index = (instance - instances) / TRICKLE_T_SIZE;
    // Convert from index to (i, j) index into 2D array
    *i = index / N_TRICKLE_NODES;
    *j = index % N_TRICKLE_NODES;
}

///////////////
// Interface //
///////////////

// Generate key based on the index of the trickle instance in the array
// Return number of bytes written do `dest`
uint8_t
get_key(uint8_t *instance, uint8_t *dest) {
    uint16_t i, j;
    get_double_index(instance, &i, &j);
    // Copy the addresses to dest
    memcpy(dest,   addresses[i].address, 6);
    memcpy(dest+6, addresses[j].address, 6);
    return 12;
}

// Write data of a trickle instance to `dest`. Returns bytes written
uint8_t
get_data     (uint8_t *instance, uint8_t *dest) {
    uint16_t i, j;
    get_double_index(instance, &i, &j);
    *dest = data[i][j];
    return 1;
}
struct trickle_t*
get_instance (slice_t key) {
    uint32_t i = get_index(key);
    return (trickle_t *) instances;
}
