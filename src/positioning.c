#include "trickle.h"
#include <string.h>

#include "mayfly.h"
#include "debug.h"

#define APP_ID 0x8070
#define KEY_LEN 14

/* Application "positioning"
 * Key structure: (uint16_t app_id, uint8_t addr1[6], uint8_t addr2[6])
 * Value: 1 byte RSSI
 */

// Structures, helper functions and interface for trickle

static uint8_t values[N_TRICKLE_NODES][N_TRICKLE_NODES];
// Memory for trickle instances
static uint8_t instances[N_TRICKLE_NODES][N_TRICKLE_NODES][TRICKLE_T_SIZE];

// Access structure to map (address <-> index)
typedef struct {
    uint8_t present;
    uint8_t address[6];
} address_index_t;

static address_index_t addresses[N_TRICKLE_NODES];
static uint16_t addresses_top = 0;

extern uint8_t dev_addr[6];

void
positioning_init() {
    trickle_init((struct trickle_t*)instances, N_TRICKLE_NODES * N_TRICKLE_NODES);
    // Start first instance - a meaningless self<->self RSSI just to give other nodes some packets
    uint8_t key_data[KEY_LEN];
    write_uint16(key_data, APP_ID);
    memcpy(key_data+2, dev_addr, 6);
    memcpy(key_data+8, dev_addr, 6);
    slice_t key = new_slice(key_data, KEY_LEN);

    uint8_t val_data = 0;
    slice_t val = new_slice(&val_data, 1);

    struct trickle_t *instance = trickle_config.get_instance_fp(key);
    ASSERT(instance);
    trickle_value_write(instance, key, val, MAYFLY_CALL_ID_PROGRAM);
}
/* Translation from address to index. If address isn't yet indexed, give it an index.
   Note: In this application, a key consists of two 6-byte addresses.
*/
uint32_t
get_index(uint8_t *address) {
    for (int i = 0; i < N_TRICKLE_NODES; i ++) {
        if (addresses[i].present) {
            if (memcmp(addresses[i].address, address, 6) == 0) {
                return i;
            }
        } else {
            break;
        }
    }

    // The address is not found. Give it an index.
    uint32_t new_index = addresses_top++;
    if (new_index >= N_TRICKLE_NODES) {
        // Max number of addresses reached
        return ~0;
    }
    memcpy(addresses[new_index].address, address, 6);
    addresses[new_index].present = 1;
    return new_index;
}

void
get_double_index(uint8_t *instance, uint16_t *i, uint16_t *j) {

    uint32_t index = (instance - (uint8_t*)instances) / TRICKLE_T_SIZE;
    // Convert from index to (i, j) index into 2D array
    *i = index / N_TRICKLE_NODES;
    *j = index % N_TRICKLE_NODES;
}

uint8_t
make_key(uint8_t *dest, uint8_t *addr1, uint8_t *addr2) {
    write_uint16(dest, APP_ID);
    memcpy(dest+2, addr1, 6);
    memcpy(dest+8, addr2, 6);
    return KEY_LEN;
}

///////////////
// Interface //
///////////////

// Generate key based on the index of the trickle instance in the array
// Return number of bytes written do `dest`
uint8_t
positioning_get_key(uint8_t *instance, uint8_t *dest) {
    uint16_t i, j;
    get_double_index(instance, &i, &j);
    return make_key(dest, addresses[i].address, addresses[j].address);
}

// Write data of a trickle instance to `dest`. Returns bytes written
slice_t
positioning_get_val(uint8_t *instance) {
    uint16_t i, j;
    get_double_index(instance, &i, &j);
    ASSERT(i < N_TRICKLE_NODES);
    ASSERT(j < N_TRICKLE_NODES);
    return new_slice(&values[i][j], 1);
}

struct trickle_t*
positioning_get_instance(slice_t key) {
    // Check that the key is for this application
    if (read_uint16(key.ptr) != APP_ID || key.len != KEY_LEN) {
        return 0;
    }
    // Prohibit access to instances that reflect RSSI between a single device
    if (memcmp(key.ptr+2, key.ptr+8, 6) == 0
    // .... except self <-> self. It's used just for other nodes to get RSSI
            && memcmp(key.ptr+2, dev_addr, 6) != 0) {
        return 0;
    }
    uint32_t i = get_index(key.ptr + 2);
    uint32_t j = get_index(key.ptr + 8);
    if (i == ~0 || j == ~0) {
        // Max number of addresses reached
        return 0;
    }
    return (struct trickle_t *) &instances[i][j];
}

void
positioning_register_rssi(uint8_t rssi, uint8_t *other_dev_addr) {
    uint8_t key_data[KEY_LEN];
    make_key(key_data, dev_addr, other_dev_addr);
    slice_t key = new_slice(key_data, sizeof(key_data));

    struct trickle_t *instance = trickle_config.get_instance_fp(key);
    // RSSI will only get registered if it comes from a device running the positioning app
    if (instance) {
        slice_t val = new_slice(&rssi, 1);
        trickle_value_write(instance, key, val, MAYFLY_CALL_ID_PROGRAM);
    }
}

uint8_t
is_positioning_node(uint8_t *address) {
    for (int i = 0; i < N_TRICKLE_NODES; i ++) {
        if (addresses[i].present) {
            if (memcmp(addresses[i].address, address, 6) == 0) {
                return 1;
            }
        } else {
            return 0;
        }
    }
}
