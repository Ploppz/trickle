#include "trickle.h"
#include "tx.h"
#include "slice.h"
#include "rio.h"
#include "positioning.h"
#include "toggle.h"

#include "SEGGER_RTT.h"

#include "soc.h"
#include "cpu.h"
#include "irq.h"
#include "hal/uart.h"

#include "util/misc.h"
#include "util/util.h"
#include "util/mayfly.h"

#include "hal/clock.h"
#include "hal/cntr.h"

#include "ticker/ticker.h"

#include "hal/rand.h"
#include "hal/radio.h"

#include "pdu.h"
#include "ctrl.h"
#include "ll.h"

#include "hal/debug.h"

/* Positioning application */
uint8_t __noinit isr_stack[2048];
uint8_t __noinit main_stack[2048];
void * const isr_stack_top = isr_stack + sizeof(isr_stack);
void * const main_stack_top = main_stack + sizeof(main_stack);

#define RIO_TICKER_NODES (1)
#define TICKER_NODES (RIO_TICKER_NODES + 1 + TICKER_PER_TRICKLE * N_TRICKLE_INSTANCES)

#define TICKER_USER_WORKER_OPS (30)
#define TICKER_USER_JOB_OPS (30)
#define TICKER_USER_APP_OPS (30)
#define TICKER_USER_OPS (TICKER_USER_WORKER_OPS + TICKER_USER_JOB_OPS + TICKER_USER_APP_OPS)

static uint8_t ALIGNED(4) ticker_nodes[TICKER_NODES][TICKER_NODE_T_SIZE];
static uint8_t ALIGNED(4) ticker_users[MAYFLY_CALLER_COUNT][TICKER_USER_T_SIZE];
static uint8_t ALIGNED(4) ticker_user_ops[TICKER_USER_OPS][TICKER_USER_OP_T_SIZE];


static uint8_t ALIGNED(4) rng[3 + 4 + 1];


#define TICKER_ID_APP (RIO_TICKER_NODES)
#define TICKER_ID_TRICKLE (RIO_TICKER_NODES+1)



////////////
// Config //
////////////

// Macro to construct {APP_NAME}_{FN_NAME}
#define CAT(x, y) CAT_(x, y)
#define CAT_(x, y) x ## y
#define APP_FN(FN_NAME) CAT(CAT(APP_NAME, _), FN_NAME)

#define MAX_TX_TIME_US 5 * 1e3

trickle_config_t trickle_config = {
    .interval_min_us = 100  * 1e3,
    .interval_max_us = 4    * 1e6,
    .c_threshold = 2,
    
    .first_ticker_id = TICKER_ID_TRICKLE,
    
    .max_tx_time_us = MAX_TX_TIME_US,

    .get_key_fp = &APP_FN(get_key),
    .get_val_fp = &APP_FN(get_val),
    .get_instance_fp = &APP_FN(get_instance),
};


rio_config_t rio_config = {
    .bt_channel  = 5,
    .rf_channel  = CH_INDEX5,
    .access_addr = 0x84215142,
    .update_interval_us = MAX_TX_TIME_US - 1000,
};

//////////////////
// Declarations //
//////////////////

// Ticker timeouts
void op_callback1(uint32_t status, void *context);
void op_callback2(uint32_t status, void *context);
void op_callback3(uint32_t status, void *context);
void trickle_timeout(uint32_t ticks_at_expire, uint32_t remainder, uint16_t lazy, void *context);
void transmit_timeout(uint32_t ticks_at_expire, uint32_t remainder, uint16_t lazy, void *context);

// Other helpers
void request_transmission();
void toggle_line(uint32_t line);
void gpiote_out_init(uint32_t index, uint32_t pin, uint32_t polarity, uint32_t init_val);
void init_ppi();
uint32_t low_mask(uint8_t n);
uint32_t rand_range(uint32_t min, uint32_t max);
void read_address();

// Applications
void toggle_run();
void positioning_run();

static uint8_t trickle_val = 0;

uint8_t dev_addr[6];
address_type_t addr_type;

int main(void)
{
    uint32_t retval;
    DEBUG_INIT();

    // pins 1-4, 10-12, 16-24
    //                    31     24      16      8       0
    //                    |------||------||------||------|
    uint32_t out_pins = 0b00000001111111110001110000011110;
    NRF_GPIO->DIRSET = out_pins;
    NRF_GPIO->OUTSET = out_pins;

    NRF_GPIO->OUTCLR = (1 << 1);
    NRF_GPIO->DIRSET = (1 << 15);
    NRF_GPIO->OUTSET = (1 << 15);

    init_ppi();
    read_address();




    #if UART
    uart_init(UART, 1);
    irq_priority_set(UART0_IRQn, 0xFF);
    irq_enable(UART0_IRQn);

    uart_tx_str("\n\n\nBLE LL.\n");

    {
        extern void assert_print(void);

        assert_print();
    }
    #endif

    /* Mayfly shall be initialized before any ISR executes */
    mayfly_init();

    clock_k32src_start(1);
    irq_priority_set(POWER_CLOCK_IRQn, 0xFF);
    irq_enable(POWER_CLOCK_IRQn);

    cntr_init();
    irq_priority_set(RTC0_IRQn, 0xFD);
    irq_enable(RTC0_IRQn);

    irq_priority_set(SWI4_IRQn, 0xFE);
    irq_enable(SWI4_IRQn);

    ticker_users[MAYFLY_CALL_ID_0][0]       = TICKER_USER_WORKER_OPS;
    ticker_users[MAYFLY_CALL_ID_1][0]       = TICKER_USER_JOB_OPS;
    ticker_users[MAYFLY_CALL_ID_2][0]       = 0;
    ticker_users[MAYFLY_CALL_ID_PROGRAM][0] = TICKER_USER_APP_OPS;

    ticker_init(RADIO_TICKER_INSTANCE_ID_RADIO,
            TICKER_NODES, &ticker_nodes[0],
            MAYFLY_CALLER_COUNT, &ticker_users[0],
            TICKER_USER_OPS, &ticker_user_ops[0]);

    rand_init(rng, sizeof(rng));
    irq_priority_set(RNG_IRQn, 0xFF);
    irq_enable(RNG_IRQn);

    irq_priority_set(ECB_IRQn, 0xFF);

    irq_priority_set(RADIO_IRQn, 0xFD);
    irq_enable(RADIO_IRQn);
    rio_init();

    APP_FN(init)();
    APP_FN(run)();
}

////////////////
// App toggle //
////////////////

void
toggle_timeout(uint32_t ticks_at_expire, uint32_t remainder, uint16_t lazy, void *context) {
    trickle_val = !trickle_val;
    slice_t key = new_slice(dev_addr, 6);
    slice_t val = new_slice(&trickle_val, 1);
    trickle_value_write(trickle_config.get_instance_fp(key), key, val, MAYFLY_CALL_ID_0);
}

void
toggle_run() {
    const uint32_t PERIOD_MS = 1000;
    uint32_t err = ticker_start(RADIO_TICKER_INSTANCE_ID_RADIO // instance
        , MAYFLY_CALL_ID_PROGRAM // user
        , TICKER_ID_APP // ticker id
        , ticker_ticks_now_get() // anchor point
        , TICKER_US_TO_TICKS(PERIOD_MS * 1000) // first interval
        , TICKER_US_TO_TICKS(PERIOD_MS * 1000) // periodic interval
        , TICKER_REMAINDER(PERIOD_MS * 1000) // remainder
        , 0 // lazy
        , 0 // slot
        , toggle_timeout // timeout callback function
        , 0, 0, 0);
    ASSERT(!err);

    while (1) { 
        // Wait for incoming packet
        packet_t *in_packet = 0;
        while (!in_packet) {
            in_packet = rio_rx_get_packet();
        }

        trickle_pdu_handle(&in_packet->data[9], in_packet->data[1] - 6);

        if (NRF_RADIO->STATE == 3 || NRF_RADIO->STATE == 2 || NRF_RADIO->STATE == 1) {
            NRF_GPIO->OUTSET = (1 << 2);
        } else {
            NRF_GPIO->OUTCLR = (1 << 2);
        }
    }
}


/////////////////////
// App positioning //
/////////////////////

void
positioning_timeout(uint32_t ticks_at_expire, uint32_t remainder, uint16_t lazy, void *context) {
    positioning_print();
}

void
positioning_run() {
    // Start timer just to have some means of getting data out
    const uint32_t PERIOD_S = 2;
    uint32_t err = ticker_start(RADIO_TICKER_INSTANCE_ID_RADIO // instance
        , MAYFLY_CALL_ID_PROGRAM // user
        , TICKER_ID_APP // ticker id
        , ticker_ticks_now_get() // anchor point
        , TICKER_US_TO_TICKS(PERIOD_S * 1e6) // first interval
        , TICKER_US_TO_TICKS(PERIOD_S * 1e6) // periodic interval
        , TICKER_REMAINDER(PERIOD_S * 1e6) // remainder
        , 0 // lazy
        , 0 // slot
        , positioning_timeout // timeout callback function
        , 0, 0, 0);
    ASSERT(!err);


    // Listen for packets
    // Discard meaningless packets (self <-> self) (this is done inside positioning)
    while (1) { 
        // Wait for incoming packet
        packet_t *in_packet = 0;
        while (!in_packet) {
            in_packet = rio_rx_get_packet();
        }
        if (in_packet->data[0] != 0x40) {
            continue;
        }


        uint32_t pdu_len = in_packet->data[1];
        
        trickle_pdu_handle(&in_packet->data[PDU_HDR_LEN + DEV_ADDR_LEN], pdu_len - 6);

        if (is_positioning_node(&in_packet->data[PDU_HDR_LEN])) {
            uint8_t rssi = in_packet->rssi;
            // printf("RSSI %d\n", rssi);
            positioning_register_rssi(rssi, &in_packet->data[PDU_HDR_LEN]);
        }

        if (NRF_RADIO->STATE == 3 || NRF_RADIO->STATE == 2 || NRF_RADIO->STATE == 1) {
            NRF_GPIO->OUTSET = (1 << 2);
        } else {
            NRF_GPIO->OUTCLR = (1 << 2);
        }
    }
}

void
read_address() {
    addr_type = (address_type_t) (NRF_FICR->DEVICEADDRTYPE & 1);
    *(uint32_t*)dev_addr = NRF_FICR->DEVICEADDR[0]; // 4 lowest uint8_ts
    *(uint16_t*)(dev_addr+4) = (uint16_t) (NRF_FICR->DEVICEADDR[1] & low_mask(16)); // 2 higher uint8_ts
    dev_addr[5] |= 0b11000000;
}


void gpiote_out_init(uint32_t index, uint32_t pin, uint32_t polarity, uint32_t init_val) {
    NRF_GPIOTE->CONFIG[index] |= ((GPIOTE_CONFIG_MODE_Task << GPIOTE_CONFIG_MODE_Pos) & GPIOTE_CONFIG_MODE_Msk) |
                            ((pin << GPIOTE_CONFIG_PSEL_Pos) & GPIOTE_CONFIG_PSEL_Msk) |
                            ((polarity << GPIOTE_CONFIG_POLARITY_Pos) & GPIOTE_CONFIG_POLARITY_Msk) |
                            ((init_val << GPIOTE_CONFIG_OUTINIT_Pos) & GPIOTE_CONFIG_OUTINIT_Msk);
}

void init_ppi() {
    const uint32_t GPIO_CH0 = 0;
    const uint32_t GPIO_CH1 = 1;
    const uint32_t GPIO_CH2 = 2;
    const uint32_t GPIO_CH3 = 3;
    const uint32_t PPI_CH0 = 10;
    const uint32_t PPI_CH1 = 11;
    const uint32_t PPI_CH2 = 12;
    const uint32_t PPI_CH3 = 13;
    gpiote_out_init(GPIO_CH0, 21, GPIOTE_CONFIG_POLARITY_Toggle, GPIOTE_CONFIG_OUTINIT_Low); // ready
    gpiote_out_init(GPIO_CH1, 22, GPIOTE_CONFIG_POLARITY_Toggle, GPIOTE_CONFIG_OUTINIT_Low); // address
    gpiote_out_init(GPIO_CH2, 23, GPIOTE_CONFIG_POLARITY_Toggle, GPIOTE_CONFIG_OUTINIT_Low); // end
    gpiote_out_init(GPIO_CH3, 24, GPIOTE_CONFIG_POLARITY_Toggle, GPIOTE_CONFIG_OUTINIT_Low); // disabled

    NRF_PPI->CH[PPI_CH0].EEP = (uint32_t) &(NRF_RADIO->EVENTS_READY);
    NRF_PPI->CH[PPI_CH0].TEP = (uint32_t) &(NRF_GPIOTE->TASKS_OUT[GPIO_CH0]);

    NRF_PPI->CH[PPI_CH1].EEP = (uint32_t) &(NRF_RADIO->EVENTS_ADDRESS);
    NRF_PPI->CH[PPI_CH1].TEP = (uint32_t) &(NRF_GPIOTE->TASKS_OUT[GPIO_CH1]);

    NRF_PPI->CH[PPI_CH2].EEP = (uint32_t) &(NRF_RADIO->EVENTS_END);
    NRF_PPI->CH[PPI_CH2].TEP = (uint32_t) &(NRF_GPIOTE->TASKS_OUT[GPIO_CH2]);

    NRF_PPI->CH[PPI_CH3].EEP = (uint32_t) &(NRF_RADIO->EVENTS_DISABLED);
    NRF_PPI->CH[PPI_CH3].TEP = (uint32_t) &(NRF_GPIOTE->TASKS_OUT[GPIO_CH3]);

    NRF_PPI->CHENSET = (1 << PPI_CH0) | (1 << PPI_CH1) | (1 << PPI_CH2) | (1 << PPI_CH3);
}



void uart0_handler(void)
{
    isr_uart0(0);
}

void power_clock_handler(void)
{
    isr_power_clock(0);
}

void rtc0_handler(void)
{
    if (NRF_RTC0->EVENTS_COMPARE[0]) {
        NRF_RTC0->EVENTS_COMPARE[0] = 0;

        ticker_trigger(0);
    }

    if (NRF_RTC0->EVENTS_COMPARE[1]) {
        NRF_RTC0->EVENTS_COMPARE[1] = 0;

        ticker_trigger(1);
    }

    mayfly_run(MAYFLY_CALL_ID_0);
}

void swi4_handler(void)
{
    mayfly_run(MAYFLY_CALL_ID_1);
}

void rng_handler(void)
{
    isr_rand(0);
}

void radio_handler(void)
{
    isr_radio(0);
    rio_isr_radio();
}

void mayfly_enable_cb(uint8_t caller_id, uint8_t callee_id, uint8_t enable)
{
    (void)caller_id;

    ASSERT(callee_id == MAYFLY_CALL_ID_1);

    if (enable) {
        irq_enable(SWI4_IRQn);
    } else {
        irq_disable(SWI4_IRQn);
    }
}

uint32_t mayfly_is_enabled(uint8_t caller_id, uint8_t callee_id)
{
    (void)caller_id;

    if (callee_id == MAYFLY_CALL_ID_0) {
        return irq_is_enabled(RTC0_IRQn);
    } else if (callee_id == MAYFLY_CALL_ID_1) {
        return irq_is_enabled(SWI4_IRQn);
    }

    ASSERT(0);

    return 0;
}

uint32_t mayfly_prio_is_equal(uint8_t caller_id, uint8_t callee_id)
{
#if (CONFIG_BLUETOOTH_CONTROLLER_WORKER_PRIO == \
     CONFIG_BLUETOOTH_CONTROLLER_JOB_PRIO)
    return ((caller_id == callee_id) ||
        ((caller_id == MAYFLY_CALL_ID_0) &&
         (callee_id == MAYFLY_CALL_ID_1)) ||
        ((caller_id == MAYFLY_CALL_ID_1) &&
         (callee_id == MAYFLY_CALL_ID_0)));
#else
    return (caller_id == callee_id);
#endif
}

void mayfly_pend(uint8_t caller_id, uint8_t callee_id)
{
    (void)caller_id;

    switch (callee_id) {
    case MAYFLY_CALL_ID_0:
        irq_pending_set(RTC0_IRQn);
        break;

    case MAYFLY_CALL_ID_1:
        irq_pending_set(SWI4_IRQn);
        break;

    case MAYFLY_CALL_ID_PROGRAM:
    default:
        ASSERT(0);
        break;
    }
}

void radio_active_callback(uint8_t active)
{
    (void)active;
}
