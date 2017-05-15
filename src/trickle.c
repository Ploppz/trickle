#include "nrf.h"

#include <string.h>
// Trickle
#include "trickle.h"
#include "tx.h"
#include "slice.h"


// PhoenixxLL
#include "ticker.h"
#include "ctrl.h"
#include "ll.h"
#include "debug.h"
#include "rand.h"


// Limitation: key and value passed with a particular instance_id must always have the same width.

#define min(a,b) ((a) < (b) ? (a) : (b))


// TODO .. this is duplicate from main.c
int8_t dev_addr[] = {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
address_type_t addr_type = ADDR_RANDOM;


/* Trickle instance */
struct trickle_t {
    uint32_t interval; // current interval in microseconds / I
    uint32_t c_count; // consistency counter / c
    uint32_t version;

    // (ticker_id) is used for the main periodic timer, and (ticker_id+1) is used
    //   for the transmission timer
    uint32_t ticker_id;
} __attribute__((packed));

typedef struct trickle_t trickle_t;

static uint8_t trickle_initialized = 0;
static trickle_t instances[N_TRICKLE_INSTANCES];
uint8_t tx_packet[MAX_PACKET_LEN];

/////////////
// Trickle //
/////////////

void
trickle_timeout(uint32_t ticks_at_expire, uint32_t remainder, uint16_t lazy, void *context);
void
request_transmission(trickle_t *trickle);
void
transmit_timeout(uint32_t ticks_at_expire, uint32_t remainder, uint16_t lazy, void *context);
void
toggle_line(uint32_t line);
uint32_t
rand_range(uint32_t min, uint32_t max);

// TODO .. in lack of a better more endianness-agnostic way to read/write uint32_t in byte streams
uint32_t
read_quad(uint8_t *bytes) {
    return (bytes[3]<<24) | (bytes[2]<<16) | (bytes[1]<<8) | (bytes[0]);
}
void
write_quad(uint8_t *dest, uint32_t quad) {
    uint32_t low_mask = (1<<9) - 1;
    dest[0] = (quad    ) & low_mask;
    dest[1] = (quad>> 8) & low_mask;
    dest[2] = (quad>>16) & low_mask;
    dest[3] = (quad>>24) & low_mask;
}

#define TRANSMISSION_TIME_US 500 // approximated time it takes to transmit
#define TRANSMIT_TRY_INTERVAL_US 10000 // interval between each time we try to get a spot for transmission


void
trickle_init(struct trickle_t *instances, uint32_t n) {
    for (int i = 0; i < n; i ++) {
        instances[i] = (trickle_t) {
            .interval = trickle_config.interval_max_us,
            .c_count = trickle_config.c_threshold, // because at the very start, 
            .version = 0,

            .ticker_id = trickle_config.first_ticker_id + i,
        };
    }
}

void
trickle_next_interval(trickle_t *trickle) {
    trickle->interval = min(trickle->interval * 2, trickle_config.interval_max_us);
}

void
trickle_timeout(uint32_t ticks_at_expire, uint32_t remainder, uint16_t lazy, void *context) {

    trickle_t* trickle = (trickle_t*) context;

    toggle_line(21);
    // Set the next interval
    trickle_next_interval(trickle);
    ticker_update(RADIO_TICKER_INSTANCE_ID_RADIO, // instance
            MAYFLY_CALL_ID_0, // user
            trickle->ticker_id, // ticker_id
            TICKER_US_TO_TICKS(trickle->interval - trickle_config.interval_min_us), 0,
            0, 0, // slot
            0, 1, // lazy, force
            0, 0);

    request_transmission(trickle);
}

void
request_transmission(trickle_t *trickle) {
    // Stop an eventual previous timer (TODO not sure if this will work)
    ticker_stop(RADIO_TICKER_INSTANCE_ID_RADIO // instance
            , MAYFLY_CALL_ID_0 // user
            , trickle->ticker_id + 1 // id
            , 0, 0); // operation fp & context

    uint32_t retval = ticker_start(RADIO_TICKER_INSTANCE_ID_RADIO // instance
        , MAYFLY_CALL_ID_0 // user
        , trickle->ticker_id + 1 // ticker id
        , ticker_ticks_now_get() // anchor point
        , TICKER_US_TO_TICKS(100000) // first interval (TODO: the random interval)
        , TICKER_US_TO_TICKS(TRANSMIT_TRY_INTERVAL_US) // periodic interval
        , TICKER_REMAINDER(TRANSMIT_TRY_INTERVAL_US) // remainder
        , 0 // lazy
        , TICKER_US_TO_TICKS(TRANSMISSION_TIME_US) // slot
        , transmit_timeout // timeout callback function
        , trickle // context
        , 0 // op func
        , 0 // op context
        );
}


void
transmit_timeout(uint32_t ticks_at_expire, uint32_t remainder, uint16_t lazy, void *context) {
    toggle_line(22);
    trickle_t *trickle = (trickle_t *) context;

    // Packet structure:
    // |----------+---------|
    // | field    | bytes   |
    // |----------+---------|
    // | version  | 4       |
    // | key_len  | 1       |
    // | key      | key_len |
    // | val_len  | 1       |
    // | val      | val_len |
    // |----------+---------|

    uint8_t *packet_ptr = tx_packet;
    packet_ptr += PDU_HDR_LEN;

    // Version
    write_quad(packet_ptr, trickle->version);
    packet_ptr += sizeof(uint32_t);

    // Key
    uint8_t key_len = trickle_config.get_key_fp((uint8_t*)trickle, &packet_ptr[1]);
    packet_ptr[0] =  key_len;
    packet_ptr += 1 + key_len;

    // Value
    uint8_t val_len = trickle_config.get_val_fp((uint8_t*)trickle, &packet_ptr[1]);
    packet_ptr[0] =    val_len;
    packet_ptr += 1 + val_len;
    
    write_pdu_header(PDU_TYPE_ADV_IND, packet_ptr - tx_packet, addr_type, dev_addr, tx_packet);

    // Transmission
    start_hfclk();
    configure_radio(tx_packet, 37, ADV_CH37);
    transmit(tx_packet, ADV_CH37);

    // The timer has done its job...
    ticker_stop(RADIO_TICKER_INSTANCE_ID_RADIO // instance
            , MAYFLY_CALL_ID_0 // user
            , trickle->ticker_id + 1 // id
            , 0, 0); // operation fp & context
}


// - `trickle_pdu_handle` handles external message
// - `trickle_write` handles internal message
// - both will call `value_register` to decide what is done with the data

void
start_instance(trickle_t *instance) {
    uint32_t err = ticker_start(RADIO_TICKER_INSTANCE_ID_RADIO // instance
        , MAYFLY_CALL_ID_PROGRAM // user
        , instance->ticker_id // ticker id
        , ticker_ticks_now_get() // anchor point
        , TICKER_US_TO_TICKS(trickle_config.interval_min_us) // first interval
        , TICKER_US_TO_TICKS(trickle_config.interval_min_us) // periodic interval
        , TICKER_REMAINDER(trickle_config.interval_min_us) // remainder
        , 0 // lazy
        , 0 // slot
        , trickle_timeout // timeout callback function
        , instance // context
        , 0 // op func
        , 0 // op context
        );
    ASSERT(!err);
}
void
value_register(trickle_t *instance, slice_t key, slice_t val, trickle_version_t version) {
    // If local version is 0, it means the instance is unused and should be initialised
    if (instance->version == 0) {
        start_instance(instance);
    }

    if (version < instance->version) {
        // TODO broadcast own value
        // TODO reset i
    } else if (version > instance->version) {
        // Update own data
        instance->version = version;
        // TODO reset interval to i_min
    } else {
        instance->c_count ++;
    }
}


void
trickle_pdu_handle(uint8_t *packet_ptr, uint8_t packet_len) {
    uint8_t *packet_end_ptr = packet_ptr + packet_len; // in order to check later that the packet length is correct

    uint32_t version = read_quad(packet_ptr);
    packet_ptr += sizeof(version);

    uint8_t key_len = packet_ptr[0];
    packet_ptr += sizeof(key_len);
    if (key_len != 12) return; // TODO: TMP SOLUTION TO DISCRIMINATE NONTRICKLE PACKETS
    slice_t key = new_slice(packet_ptr, key_len);
    packet_ptr += key_len;


    uint8_t val_len = packet_ptr[0];
    packet_ptr += sizeof(val_len);
    if (val_len != 1) return; // TODO: TMP SOLUTION TO DISCRIMINATE NONTRICKLE PACKETS
    slice_t val = new_slice(packet_ptr, val_len);
    packet_ptr += val_len;

    // Check that we have read exactly until packet_len
    if (packet_ptr != packet_end_ptr) {
        return;
    }

    trickle_t *instance = trickle_config.get_instance_fp(key);

    value_register(instance, key, val, version);
}
void
trickle_value_write(trickle_t *instance, slice_t key, slice_t val) {

    value_register(instance, key, val, instance->version + 1);
}



uint32_t
get_t_value(trickle_t *trickle){
    return rand(trickle->interval/2, trickle->interval-1);
}

uint32_t
rand_range(uint32_t min, uint32_t max) {
    uint32_t random_number = 0;
    rand_get(1, (uint8_t*)&random_number);
    random_number *= max - min;
    random_number /= 0xFF;
    return min + random_number;
}


void toggle_line(uint32_t line)
{
    NRF_GPIO->OUT ^= 1 << line;
}
