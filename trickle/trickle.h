#ifndef TRICKLE_TRICKLE_H
#define TRICKLE_TRICKLE_H
/* Before using this module, ticker and radio should be initialized.
 */

#include <stdint.h>
#include "tx.h"
#include "slice.h"

#define PROTOCOL_ID 0x50607080

struct trickle_t;
#define TRICKLE_T_SIZE 16


#define TICKER_PER_TRICKLE 2 // Instances of ticker per instance of trickle


typedef uint32_t trickle_version_t;
typedef uint16_t trickle_app_id_t;


// get_key:
typedef uint8_t             (*trickle_get_key_fp_t)     (uint8_t *instance, uint8_t *dest);
// get_data: If data is not present, register it...
typedef slice_t             (*trickle_get_val_fp_t)     (uint8_t *instance);
// get_instance: If key is not found, this function should initialize a trickle struct
typedef struct trickle_t *  (*trickle_get_instance_fp_t)(slice_t key);


// unused atm.
typedef struct __attribute__((packed)) {
    uint32_t version;
    uint8_t  key_len;
    uint8_t  val_len;
    uint8_t  data[1];
} trickle_pdu_t;

typedef struct {
    // Trickle config
    uint32_t interval_min_us;
    uint32_t interval_max_us;
    uint32_t c_threshold;

    // Ticker
    uint32_t first_ticker_id;

    // Functionality provided by the application
    trickle_get_key_fp_t        get_key_fp;
    trickle_get_val_fp_t        get_val_fp;
    trickle_get_instance_fp_t   get_instance_fp;

    // Other
    uint32_t max_tx_time_us;
} trickle_config_t;

extern trickle_config_t trickle_config;



// Initialize an array of trickle instances
void
trickle_init(struct trickle_t *instances, uint32_t n);

// Handle incoming trickle packet
void
trickle_pdu_handle(uint8_t *packet_ptr, uint8_t packet_len);

// Write new data to an instance, incrementing the version number.
void
trickle_value_write(struct trickle_t *instance, slice_t key, slice_t val, uint8_t user_id);

// TODO no idea why there is not a linker error.... investigate!
uint32_t 
rand(int min, int max);

uint32_t
get_t_value(struct trickle_t *trickle);


// Serializing

uint32_t
read_uint32(uint8_t *src);
void
write_uint32(uint8_t *dest, uint32_t src);
uint16_t
read_uint16(uint8_t *src);
void
write_uint16(uint8_t *dest, uint16_t src);


void
toggle_line(uint32_t line);

#endif
