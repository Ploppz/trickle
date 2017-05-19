#include "trickle.h"
#include "tx.h"
#include "slice.h"
#include "toggle.h"
#include "positioning.h"

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

#define TICKER_NODES (RADIO_TICKER_NODES + 1 + TICKER_PER_TRICKLE * N_TRICKLE_INSTANCES + 20)

#define TICKER_USER_WORKER_OPS (RADIO_TICKER_USER_WORKER_OPS + 7)
#define TICKER_USER_JOB_OPS (RADIO_TICKER_USER_JOB_OPS + 7)
#define TICKER_USER_APP_OPS (RADIO_TICKER_USER_APP_OPS + 7)
#define TICKER_USER_OPS (TICKER_USER_WORKER_OPS + TICKER_USER_JOB_OPS + TICKER_USER_APP_OPS)

static uint8_t ALIGNED(4) ticker_nodes[TICKER_NODES][TICKER_NODE_T_SIZE];
static uint8_t ALIGNED(4) ticker_users[MAYFLY_CALLER_COUNT][TICKER_USER_T_SIZE];
static uint8_t ALIGNED(4) ticker_user_ops[TICKER_USER_OPS][TICKER_USER_OP_T_SIZE];
static uint8_t ALIGNED(4) rng[3 + 4 + 1];
static uint8_t ALIGNED(4) radio[RADIO_MEM_MNG_SIZE];


#define SCAN_INTERVAL      0x0010 // 160 ms
#define SCAN_WINDOW        0x000e // 50 ms
#define SCAN_FILTER_POLICY 0

#define TICKER_ID_APP (RADIO_TICKER_NODES)
#define TICKER_ID_TRICKLE (RADIO_TICKER_NODES+1)


/////////////
// Trickle //
/////////////

trickle_config_t trickle_config = {
    .interval_min_us = 1000,
    .interval_max_us = 1000000,
    .c_threshold = 2,
    
    .first_ticker_id =  TICKER_ID_TRICKLE,

    .get_key_fp = &toggle_get_key,
    .get_val_fp = &toggle_get_val,
    .get_instance_fp = &toggle_get_instance,
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
void app_timeout(uint32_t ticks_at_expire, uint32_t remainder, uint16_t lazy, void *context);
void read_address();

static uint8_t trickle_val = 0;

uint8_t dev_addr[6];
address_type_t addr_type;

int main(void)
{
    uint32_t retval;
    printf("Hello World %d!\n", 42);

    DEBUG_INIT();

    /* Dongle RGB LED */
    NRF_GPIO->DIRSET = (0b1111 << 21) | (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4) | (1 << 10) | (1 << 11) | (1 << 12) | (1 << 16) | (1 << 17) | (1 << 18) | (1 << 19) | (1 << 20);
    NRF_GPIO->OUTSET = (0b1111 << 21) | (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4) | (1 << 10) | (1 << 11) | (1 << 12) | (1 << 16) | (1 << 17) | (1 << 18) | (1 << 19) | (1 << 20);
    
    NRF_GPIO->OUTCLR = (1 << 1);
    NRF_GPIO->DIRSET = (1 << 15);
    NRF_GPIO->OUTSET = (1 << 15);


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
    //init_ppi();

    clock_k32src_start(1);
    irq_priority_set(POWER_CLOCK_IRQn, 0xFF);
    irq_enable(POWER_CLOCK_IRQn);

    cntr_init();
    irq_priority_set(RTC0_IRQn, CONFIG_BLUETOOTH_CONTROLLER_WORKER_PRIO);
    irq_enable(RTC0_IRQn);

    irq_priority_set(SWI4_IRQn, CONFIG_BLUETOOTH_CONTROLLER_JOB_PRIO);
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

    retval = radio_init(7, /* 20ppm = 7 ... 250ppm = 1, 500ppm = 0 */
                RADIO_CONNECTION_CONTEXT_MAX,
                RADIO_PACKET_COUNT_RX_MAX,
                RADIO_PACKET_COUNT_TX_MAX,
                RADIO_LL_LENGTH_OCTETS_RX_MAX,
                RADIO_PACKET_TX_DATA_SIZE,
                &radio[0], sizeof(radio));
    ASSERT(!retval);

    irq_priority_set(RADIO_IRQn, CONFIG_BLUETOOTH_CONTROLLER_WORKER_PRIO);

    read_address();
    // Start scanning
    // (TODO investigate which of these lines are necessary)

    uint8_t scn_data[] = {0x02, 0x01, 0x06, 0x0B, 0x08, 'P', 'h', 'o', 'e', 'n', 'i', 'x', ' ', 'L', 'L'};
    ll_address_set(addr_type, dev_addr);
    ll_scan_data_set(sizeof(scn_data), scn_data);

    // TODO passive (0)
#if 1
    ll_scan_params_set(0, SCAN_INTERVAL, SCAN_WINDOW, addr_type, SCAN_FILTER_POLICY);
    retval = ll_scan_enable(1);
    ASSERT(!retval);
#endif


    toggle_app_init();

#if 1
#define PERIOD_MS 1000
    uint32_t err = ticker_start(RADIO_TICKER_INSTANCE_ID_RADIO // instance
        , MAYFLY_CALL_ID_PROGRAM // user
        , TICKER_ID_APP // ticker id
        , ticker_ticks_now_get() // anchor point
        , TICKER_US_TO_TICKS(PERIOD_MS * 1000) // first interval
        , TICKER_US_TO_TICKS(PERIOD_MS * 1000) // periodic interval
        , TICKER_REMAINDER(PERIOD_MS * 1000) // remainder
        , 0 // lazy
        , 0 // slot
        , app_timeout // timeout callback function
        , 0 // context
        , 0 // op func
        , 0 // op context
        );
    ASSERT(!err);
#endif

    while (1) { 
        uint16_t handle = 0;
        struct radio_pdu_node_rx *node_rx = 0;

        toggle_line(14);

        uint8_t num_complete = radio_rx_get(&node_rx, &handle);

        if (node_rx) {
            radio_rx_dequeue();
            // Handle PDU
            trickle_pdu_handle(&node_rx->pdu_data[9], node_rx->pdu_data[1] - 6);

            toggle_line(13);
            //
            node_rx->hdr.onion.next = 0;
            radio_rx_mem_release(&node_rx);
        }

        if (NRF_RADIO->STATE == 3 || NRF_RADIO->STATE == 2 || NRF_RADIO->STATE == 1) {
            NRF_GPIO->OUTSET = (1 << 2);
        } else {
            NRF_GPIO->OUTCLR = (1 << 2);
        }
    }
}


void
app_timeout(uint32_t ticks_at_expire, uint32_t remainder, uint16_t lazy, void *context) {
    trickle_val = !trickle_val;
    slice_t key = new_slice(dev_addr, 6);
    slice_t val = new_slice(&trickle_val, 1);
    trickle_value_write(toggle_get_instance(key), key, val, MAYFLY_CALL_ID_0);
}


void
read_address() {
    addr_type = (address_type_t) (NRF_FICR->DEVICEADDRTYPE & 1);
    *(uint32_t*)dev_addr = NRF_FICR->DEVICEADDR[0]; // 4 lowest uint8_ts
    *(uint16_t*)(dev_addr+4) = (uint16_t) (NRF_FICR->DEVICEADDR[1] & low_mask(16)); // 2 higher uint8_ts
    dev_addr[5] |= 0b11000000;
}




// Will be called when a packet has been received
void radio_event_callback(void)
{
}

void toggle_line(uint32_t line)
{
    NRF_GPIO->OUT ^= 1 << line;
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
    const uint32_t PPI_CH0 = 10;
    const uint32_t PPI_CH1 = 11;
    const uint32_t PPI_CH2 = 12;
    gpiote_out_init(GPIO_CH0, 10, GPIOTE_CONFIG_POLARITY_Toggle, GPIOTE_CONFIG_OUTINIT_Low); // ready
    gpiote_out_init(GPIO_CH1, 11, GPIOTE_CONFIG_POLARITY_Toggle, GPIOTE_CONFIG_OUTINIT_Low); // address
    gpiote_out_init(GPIO_CH2, 12, GPIOTE_CONFIG_POLARITY_Toggle, GPIOTE_CONFIG_OUTINIT_Low); // end

    NRF_PPI->CH[PPI_CH0].EEP = (uint32_t) &(NRF_RADIO->EVENTS_READY);
    NRF_PPI->CH[PPI_CH0].TEP = (uint32_t) &(NRF_GPIOTE->TASKS_OUT[GPIO_CH0]);

    NRF_PPI->CH[PPI_CH1].EEP = (uint32_t) &(NRF_RADIO->EVENTS_ADDRESS);
    NRF_PPI->CH[PPI_CH1].TEP = (uint32_t) &(NRF_GPIOTE->TASKS_OUT[GPIO_CH1]);

    NRF_PPI->CH[PPI_CH2].EEP = (uint32_t) &(NRF_RADIO->EVENTS_END);
    NRF_PPI->CH[PPI_CH2].TEP = (uint32_t) &(NRF_GPIOTE->TASKS_OUT[GPIO_CH2]);

    NRF_PPI->CHENSET = (1 << PPI_CH0) | (1 << PPI_CH1) | (1 << PPI_CH2);
}


uint32_t low_mask(uint8_t n) {
  return (1<<(n+1)) - 1;
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
