#include "nrf.h"

#include <string.h>
// Trickle
#include "trickle.h"
#include "tx.h"

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

#define TRANSMISSION_TIME_US 500 // approximated time it takes to transmit
#define TRANSMIT_TRY_INTERVAL_US 10000 // interval between each time we try to get a spot for transmission


void
trickle_init(struct trickle_t *instances, trickle_id_t n) {
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
    trickle_t *trickle = (trickle_t *) context;

    // Packet structure ... name (bytes)
    // - version (4)
    // - key_len (1)
    // - key bytes (key_len)
    // - val_len (1)
    // - val bytes (val_len)

    uint8_t *packet_ptr = tx_packet;
    packet_ptr += PDU_HDR_LEN;

    // Version
    ((uint32_t*)packet_ptr)[0] = trickle->version;

    // Key
    uint8_t key_len = trickle_config.get_key_fp((uint8_t*)trickle, &packet_ptr[1]);
    packet_ptr[0] =  key_len;
    packet_ptr += 1 + key_len;

    // Value
    uint8_t val_len = trickle_config.get_data_fp((uint8_t*)trickle, &packet_ptr[1]);
    packet_ptr[0] =    val_len;
    packet_ptr += 1 + val_len;
    
    write_pdu_header(PDU_TYPE_ADV_IND, packet_ptr - tx_packet, addr_type, dev_addr, tx_packet);

    // Transmission
    start_hfclk();
    configure_radio(tx_packet, 37, ADV_CH37);
    transmit(tx_packet, ADV_CH37);
    // Debugging
    toggle_line(22);

    // The timer has done its job...
    ticker_stop(RADIO_TICKER_INSTANCE_ID_RADIO // instance
            , MAYFLY_CALL_ID_0 // user
            , trickle->ticker_id + 1 // id
            , 0, 0); // operation fp & context
}


void
pdu_handle(uint8_t *packet_ptr, uint8_t packet_len) {
    uint32_t version = ((uint32_t*)packet_ptr)[0];
    packet_ptr += sizeof(version);

    uint8_t key_len = packet_ptr[0];
    packet_ptr += sizeof(key_len);
    slice_t key = new_slice(packet_ptr, key_len);
    packet_ptr += key_len;


    uint8_t val_len = packet_ptr[0];
    packet_ptr += sizeof(val_len);
    slice_t val = new_slice(packet_ptr, val_len);
    packet_ptr += val_len;

    trickle_t *instance = trickle_config.get_instance_fp(key);
    
    if (version < instance->version) {
        // TODO broadcast own data
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
trickle_next_interval(trickle_t *trickle) {
    trickle->interval = min(trickle->interval * 2, trickle_config.interval_max_us);
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
