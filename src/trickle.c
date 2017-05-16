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

#include "hal/radio.h"

#define min(a,b) ((a) < (b) ? (a) : (b))

#define N_TRICKLE_INSTANCES 1


// TODO .. dupliate from main.c
int8_t dev_addr[] = {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
address_type_t addr_type = ADDR_RANDOM;

/////////////
// Structs //
/////////////

typedef struct __attribute__((packed)) {
    uint32_t protocol_id;
    uint8_t  instance_id;
    uint32_t version_id;
} trickle_pdu_t;


/* Trickle instance */
struct trickle_t {
    uint32_t interval; // current interval in microseconds / I
    uint32_t c_count; // consistency counter / c
    trickle_pdu_t pdu;
    uint8_t *data;
    // (ticker_id) is used for the main periodic timer, and (ticker_id+1) is used
    //   for the transmission timer
    uint32_t ticker_id;
} __attribute__((packed));

typedef struct trickle_t trickle_t;


typedef struct {
    uint32_t interval_min; // in microseconds
    uint32_t interval_max; // in microseconds
    uint32_t c_constant; // consistency constant / k

    uint32_t scan_interval;
    uint32_t scan_window;
} trickle_config_t;


static uint8_t trickle_initialized = 0;
static trickle_config_t trickle_config;
static trickle_t instances[N_TRICKLE_INSTANCES];

uint8_t tx_packet[MAX_PACKET_LEN];

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


//////////////////////
// Public interface //
//////////////////////


/*
 * Allocates N_TRICKLE_INSTANCES trickle instances, using ticker id `first_ticker_id` and up.
 * Trickle will use two ticker ids for each instance.
 */
trickle_t *
trickle_init(uint32_t first_ticker_id, uint32_t interval_min_ms, uint32_t interval_max_ms, uint32_t c_constant) {
    uint32_t retval;
    // Initialize & configure module
    ASSERT(!trickle_initialized);
    trickle_initialized = 1;
    trickle_config.interval_min = interval_min_ms * 1000;
    trickle_config.interval_max = interval_max_ms * 1000;
    trickle_config.c_constant = c_constant;

    // Initialize trickle instances
    for (int i = 0; i < N_TRICKLE_INSTANCES; i ++) {
        uint32_t ticker_id = first_ticker_id + 2*i;
        instances[i] = (trickle_t) {
            .interval = trickle_config.interval_min,
            .c_count = 0,
            .pdu = (trickle_pdu_t) {
                .protocol_id = PROTOCOL_ID,
                .instance_id = i,
                .version_id = 0,
            },
            .data = 0,
            .ticker_id = ticker_id,
        };

        uint32_t err = ticker_start(RADIO_TICKER_INSTANCE_ID_RADIO // instance
            , MAYFLY_CALL_ID_PROGRAM // user
            , ticker_id // ticker id
            , ticker_ticks_now_get() // anchor point
            , TICKER_US_TO_TICKS(trickle_config.interval_min) // first interval
            , TICKER_US_TO_TICKS(trickle_config.interval_min) // periodic interval
            , TICKER_REMAINDER(trickle_config.interval_min) // remainder
            , 0 // lazy
            , 0 // slot
            , trickle_timeout // timeout callback function
            , instances + i // context
            , 0 // op func
            , 0 // op context
            );
        ASSERT(!err);
    }
}

// First byte of the data is the leength of the rest of the data
// TODO: should probably also increase version number or something?
void
set_data(uint32_t trickle_id, uint8_t *data) {
    instances[trickle_id].data = data;
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
            TICKER_US_TO_TICKS(trickle->interval - trickle_config.interval_min), 0,
            0, 0, // slot
            0, 1, // lazy, force
            0, 0);
    ASSERT(err == TICKER_STATUS_SUCCESS || err == TICKER_STATUS_BUSY);

    request_transmission(trickle);

    //radio_disable();
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
        , TICKER_US_TO_TICKS(random_transmit_time) // first interval (TODO: the random interval)
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
reset_timers(trickle_t *trickle) {

    // Stop periodic timer
    ticker_stop(RADIO_TICKER_INSTANCE_ID_RADIO // instance
            , MAYFLY_CALL_ID_0 // user
            , trickle->ticker_id // id
            , 0, 0); // operation fp & context

    // Start periodic timer
    uint32_t retval = ticker_start(RADIO_TICKER_INSTANCE_ID_RADIO // instance
        , MAYFLY_CALL_ID_0 // user
        , trickle->ticker_id // ticker id
        , ticker_ticks_now_get() // anchor point
        , TICKER_US_TO_TICKS(trickle_config.interval_min) // first interval
        , TICKER_US_TO_TICKS(trickle_config.interval_min) // periodic interval
        , TICKER_REMAINDER(trickle_config.interval_min) // remainder
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
transmit_timeout(uint32_t ticks_at_expire, uint32_t remainder, uint16_t lazy, void *context) {
    //radio_reset();
    ASSERT(radio_role() == 0);

//10-11-12-16

    NRF_GPIO->OUTCLR = (1 << 10) | (1 << 11) | (1 << 12);
    if(lazy == 0){
        
    }else if (lazy == 1){
        NRF_GPIO->OUTSET = (1 << 10);
    }else if (lazy == 2){
        NRF_GPIO->OUTSET = (1 << 11);
    }else if (lazy == 3){
        NRF_GPIO->OUTSET = (1 << 10) | (1 << 11);
    }else if (lazy == 4){
        NRF_GPIO->OUTSET = (1 << 12);
    }else if (lazy == 5){
        NRF_GPIO->OUTSET = (1 << 10) | (1 << 12);
    }else if (lazy == 6){
        NRF_GPIO->OUTSET = (1 << 11) | (1 << 12);
    }else{
        NRF_GPIO->OUTSET = (1 << 10) | (1 << 11) | (1 << 12);
    }
    NRF_GPIO->OUT ^= (1 << 16);


    trickle_t *trickle = (trickle_t *) context;

    // Make packet: pdu followed by eventual data
    uint32_t data_len1 = sizeof(trickle_pdu_t);
    uint32_t data_len2 = trickle->data[0];

    memcpy(tx_packet + PDU_HDR_LEN, (uint8_t*)&trickle->pdu, data_len1);
    if (trickle->data) {
        memcpy(tx_packet + PDU_HDR_LEN + data_len1, ((uint8_t*)trickle->data) + 1, data_len2);
    }
    write_pdu_header(PDU_TYPE_ADV_IND, data_len1+data_len2, addr_type, dev_addr, tx_packet);

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
    if (packet_len < sizeof(trickle_pdu_t)) {
        return;
    }

    trickle_pdu_t *pdu = (trickle_pdu_t*) packet_ptr;

    int a = pdu->protocol_id;
    
    if (pdu->protocol_id != PROTOCOL_ID) {
        return;
    }

    if (pdu->instance_id != 0) {
        return;
    }

    trickle_t *trickle = instances + pdu->instance_id;
    
    if (pdu->version_id < trickle->pdu.version_id) {
        trickle->interval = trickle_config.interval_min;
        reset_timers(trickle);
    } else if (pdu->version_id > trickle->pdu.version_id) {
        trickle->pdu.version_id = pdu->version_id;
        trickle->interval = trickle_config.interval_min;
        reset_timers(trickle);
    } else {
        trickle->c_count ++;
    }

}


void
trickle_next_interval(trickle_t *trickle) {
    trickle->interval = min(trickle->interval * 2, trickle_config.interval_max);
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
