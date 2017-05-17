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

#include "hal/radio.h"

// Limitation: key and value passed with a particular instance_id must always have the same width.

#define min(a,b) ((a) < (b) ? (a) : (b))


// TODO .. not a good solution

extern uint8_t *dev_addr;
extern address_type_t addr_type;


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

uint8_t tx_packet[MAX_PACKET_LEN];

/////////////
// Trickle //
/////////////

void
request_transmission(trickle_t *trickle);
void
trickle_timeout(uint32_t ticks_at_expire, uint32_t remainder, uint16_t lazy, void *context);
void
prepare_transmit_timeout(uint32_t ticks_at_expire, uint32_t remainder, uint16_t lazy, void *context);
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

#define TRANSMISSION_PREPARE_TIME_US 500
#define TRANSMISSION_TIME_US 500 // approximated time it takes to transmit
#define TRANSMIT_TRY_INTERVAL_US 10000 // interval between each time we try to get a spot for transmission



void
trickle_init(struct trickle_t *instances, uint32_t n) {
    for (int i = 0; i < n; i ++) {
        instances[i] = (trickle_t) {
            .interval = trickle_config.interval_max_us,
            .c_count = trickle_config.c_threshold, // because at the very start, 
            .version = 0,

            .ticker_id = trickle_config.first_ticker_id + i*TICKER_PER_TRICKLE,
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
    uint32_t err = ticker_update(RADIO_TICKER_INSTANCE_ID_RADIO, // instance
            MAYFLY_CALL_ID_0, // user
            trickle->ticker_id, // ticker_id
            TICKER_US_TO_TICKS(trickle->interval - trickle_config.interval_min_us), 0,
            0, 0, // slot
            0, 1, // lazy, force
            0, 0);
    ASSERT(err != TICKER_STATUS_FAILURE);

    request_transmission(trickle);

    //radio_disable();
}



void
reset_timers(trickle_t *trickle, uint8_t user_id) {
    // Stop periodic timer
    ticker_stop(RADIO_TICKER_INSTANCE_ID_RADIO // instance
            , user_id // user
            , trickle->ticker_id // id
            , 0, 0); // operation fp & context

    // Start periodic timer
    uint32_t retval = ticker_start(RADIO_TICKER_INSTANCE_ID_RADIO // instance
        , user_id // user
        , trickle->ticker_id // ticker id
        , ticker_ticks_now_get() // anchor point
        , TICKER_US_TO_TICKS(trickle_config.interval_min_us) // first interval
        , TICKER_US_TO_TICKS(trickle_config.interval_min_us) // periodic interval
        , TICKER_REMAINDER(trickle_config.interval_min_us) // remainder
        , 0 // lazy
        , 0 // slot
        , trickle_timeout // timeout callback function
        , trickle // context
        , 0 // op func
        , 0 // op context
        );
    
    request_transmission(trickle);
}

void
request_transmission(trickle_t *trickle) {
    // Stop an eventual previous timer (TODO not sure if this will work)
    ticker_stop(RADIO_TICKER_INSTANCE_ID_RADIO // instance
            , MAYFLY_CALL_ID_0 // user
            , trickle->ticker_id + 1 // id
            , 0, 0); // operation fp & context
    uint32_t random_transmit_time = rand_range(trickle->interval/2, trickle->interval - TRANSMISSION_TIME_US);
    uint32_t retval = ticker_start(RADIO_TICKER_INSTANCE_ID_RADIO // instance
        , MAYFLY_CALL_ID_0 // user
        , trickle->ticker_id + 1 // ticker id
        , ticker_ticks_now_get() // anchor point
        , TICKER_US_TO_TICKS(random_transmit_time) // first interval
        , TICKER_US_TO_TICKS(TRANSMIT_TRY_INTERVAL_US) // periodic interval
        , TICKER_REMAINDER(TRANSMIT_TRY_INTERVAL_US) // remainder
        , 0 // lazy
        , TICKER_US_TO_TICKS(TRANSMISSION_PREPARE_TIME_US + TRANSMISSION_TIME_US) // slot
        , prepare_transmit_timeout // timeout callback function
        , trickle // context
        , 0 // op func
        , 0 // op context
        );
    // TODO there should be a decision point whether or not to send - hence perhaps no slot at this point
}

void
prepare_transmit_timeout(uint32_t ticks_at_expire, uint32_t remainder, uint16_t lazy, void *context) {
    trickle_t *trickle = (trickle_t *) context;

    // Need to give the PhoenixLL scanner a chance to 'cleanup'
    radio_role_disable();

    // The timer has done its job... schedule transmission
    ticker_stop(RADIO_TICKER_INSTANCE_ID_RADIO // instance
            , MAYFLY_CALL_ID_0 // user
            , trickle->ticker_id + 1 // id
            , 0, 0); // operation fp & context

    uint32_t retval = ticker_start(RADIO_TICKER_INSTANCE_ID_RADIO // instance
        , MAYFLY_CALL_ID_0 // user
        , trickle->ticker_id + 2 // ticker id
        , ticks_at_expire // anchor point
        , TICKER_US_TO_TICKS(TRANSMISSION_PREPARE_TIME_US) // first interval
        , 0 // periodic interval not needed - one-shot with no slot
        , 0 // remainder
        , 0 // lazy
        , 0 // slot
        , transmit_timeout // timeout callback function
        , trickle // context
        , 0 // op func
        , 0 // op context
        );
}

void
transmit_timeout(uint32_t ticks_at_expire, uint32_t remainder, uint16_t lazy, void *context) {
    toggle_line(22);
    ASSERT(radio_role() == 0);
    radio_reset();

    //10-11-12-16
    {
        NRF_GPIO->OUTCLR = (1 << 10) | (1 << 11) | (1 << 12);
        if(lazy == 0) {
            
        } else if (lazy == 1) {
            NRF_GPIO->OUTSET = (1 << 10);
        } else if (lazy == 2) {
            NRF_GPIO->OUTSET = (1 << 11);
        } else if (lazy == 3) {
            NRF_GPIO->OUTSET = (1 << 10) | (1 << 11);
        } else if (lazy == 4) {
            NRF_GPIO->OUTSET = (1 << 12);
        } else if (lazy == 5) {
            NRF_GPIO->OUTSET = (1 << 10) | (1 << 12);
        } else if (lazy == 6) {
            NRF_GPIO->OUTSET = (1 << 11) | (1 << 12);
        } else {
            NRF_GPIO->OUTSET = (1 << 10) | (1 << 11) | (1 << 12);
        }
        NRF_GPIO->OUT ^= (1 << 16);
    }

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

    uint8_t *packet_start_ptr = packet_ptr;

    // Protocol discriminator
    write_quad(packet_ptr, PROTOCOL_ID);
    packet_ptr += sizeof(uint32_t);

    // Version
    write_quad(packet_ptr, trickle->version);
    packet_ptr += sizeof(uint32_t);

    // Key
    uint8_t key_len = trickle_config.get_key_fp((uint8_t*)trickle, &packet_ptr[1]);
    packet_ptr[0] =  key_len;
    packet_ptr += 1 + key_len;

    // Value
    slice_t val = trickle_config.get_val_fp((uint8_t*)trickle);
    packet_ptr[0] =    val.len;
    memcpy(&packet_ptr[1], val.ptr, val.len);
    packet_ptr += 1 + val.len;
    
    write_pdu_header(PDU_TYPE_ADV_IND, packet_ptr - packet_start_ptr, addr_type, dev_addr, tx_packet);

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
// - `trickle_value_write` handles internal message
// - both will call `value_register` to decide what is done with the data

static uint32_t x = 0;

void
start_instance(trickle_t *instance, uint8_t user_id) {
    uint32_t ticker_id = instance->ticker_id;
    uint32_t err = ticker_start(RADIO_TICKER_INSTANCE_ID_RADIO // instance
        , user_id // user
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
    ASSERT(err != TICKER_STATUS_FAILURE);
}
void
value_register(trickle_t *instance, slice_t key, slice_t new_val, trickle_version_t version, uint32_t user_id) {
    // If local version is 0, it means the instance is unused and should be initialised
    if (instance->version == 0) {
        start_instance(instance, user_id);
    }

    
    if (version < instance->version) {
        instance->interval = trickle_config.interval_min_us;
        reset_timers(instance, user_id);
    } else if (version > instance->version) {
        instance->version = version;
        slice_t val = trickle_config.get_val_fp((uint8_t*)instance);
        if (val.len != new_val.len) { // Erroneous packet... abort (TODO think about this)
            return;
        }
        memcpy(val.ptr, new_val.ptr, new_val.len);

        instance->interval = trickle_config.interval_min_us;
        reset_timers(instance, user_id);
    } else {
        instance->c_count ++;
    }
}


void
trickle_pdu_handle(uint8_t *packet_ptr, uint8_t packet_len) {
    uint8_t *packet_end_ptr = packet_ptr + packet_len; // in order to check later that the packet length is correct

    uint32_t protocol_id = read_quad(packet_ptr);
    if (protocol_id != PROTOCOL_ID) {
        return;
    }
    packet_ptr += sizeof(protocol_id);

    uint32_t version = read_quad(packet_ptr);
    packet_ptr += sizeof(version);

    uint8_t key_len = packet_ptr[0];
    packet_ptr += sizeof(key_len);
    slice_t key = new_slice(packet_ptr, key_len);
    packet_ptr += key_len;


    uint8_t val_len = packet_ptr[0];
    packet_ptr += sizeof(val_len);
    slice_t val = new_slice(packet_ptr, val_len);
    packet_ptr += val_len;

    // Check that we have read exactly until packet_len
    if (packet_ptr != packet_end_ptr) {
        return;
    }

    trickle_t *instance = trickle_config.get_instance_fp(key);

    value_register(instance, key, val, version, MAYFLY_CALL_ID_PROGRAM);
}
void
trickle_value_write(trickle_t *instance, slice_t key, slice_t val, uint8_t user_id) {
    uint32_t v = instance->version;
    value_register(instance, key, val, instance->version + 1, user_id);
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
