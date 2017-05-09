#include "trickle.h"

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

uint8_t __noinit isr_stack[512];
uint8_t __noinit main_stack[512];

void * const isr_stack_top = isr_stack + sizeof(isr_stack);
void * const main_stack_top = main_stack + sizeof(main_stack);

static uint8_t ALIGNED(4) ticker_nodes[RADIO_TICKER_NODES][TICKER_NODE_T_SIZE];
static uint8_t ALIGNED(4) ticker_users[MAYFLY_CALLER_COUNT][TICKER_USER_T_SIZE];
static uint8_t ALIGNED(4) ticker_user_ops[RADIO_TICKER_USER_OPS]
                        [TICKER_USER_OP_T_SIZE];

static uint8_t ALIGNED(4) rng[3 + 4 + 1];

static uint8_t ALIGNED(4) radio[RADIO_MEM_MNG_SIZE];

#define ADV_INTERVAL_FAST  0x0020
#define ADV_INTERVAL_SLOW  0x0800

#define SCAN_INTERVAL      0x0100
#define SCAN_WINDOW        0x0050

#define CONN_INTERVAL      0x0028
#define CONN_LATENCY       0x0005
#define CONN_TIMEOUT       0x0064

#define ADV_FILTER_POLICY  0x00
#define SCAN_FILTER_POLICY 0x00

trickle_config_t trickle_config = {
    .interval_min = 0xFF,
    .interval_max = 0xFFFF,
    .c_constant = 2
};
trickle_t trickle;

void update_has_happened(uint32_t status, void *context) {
    static uint32_t tick;

    switch ((++tick) & 1) {
    case 0:
        NRF_GPIO->OUTSET = (1 << 22);
        break;
    case 1:
        NRF_GPIO->OUTCLR = (1 << 22);
        break;
    }
}

void ticker_timeout(uint32_t ticks_at_expire, uint32_t remainder, uint16_t lazy,
            void *context)
{
    static uint32_t tick;

    (void)ticks_at_expire;
    (void)remainder;
    (void)lazy;
    (void)context;


    uint32_t interval = next_interval(&trickle);
    ticker_update(0, 3, 10, // instance, user, ticker_id
            // drift plus, drift minus:
            // Notice that the periodic interval is set to 0xFFFF
            // 0xFFFF - (0xFFFF - interval) = interval
            (interval - trickle_config.interval_min), 0,
            0, 0, // slot
            0, 1, // lazy, force
            update_has_happened, 0);


    uint32_t drift = 0x100;
        ticker_update(RADIO_TICKER_INSTANCE_ID_RADIO, RADIO_TICKER_USER_ID_APP, RADIO_TICKER_ID_ADV, // instance, user, ticker_id
            // drift plus, drift minus:
            // Notice that the periodic interval is set to 0xFFFF
            // 0xFFFF - (0xFFFF - interval) = interval
            get_next_radio_drift(&trickle), 0,
            0, 0, // slot
            0, 1, // lazy, force
            0, 0);

            drift += 0x100;
    switch ((++tick) & 1) {
    case 0:
        NRF_GPIO->OUTSET = (1 << 21);
        break;
    case 1:
        NRF_GPIO->OUTCLR = (1 << 21);
        break;
    }
}


void toggle_line(uint32_t line)
{
    NRF_GPIO->OUT ^= 1 << line;
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

void radio_event_callback(void)
{
        toggle_line(14);
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
    gpiote_out_init(GPIO_CH1, 11, GPIOTE_CONFIG_POLARITY_Toggle, GPIOTE_CONFIG_OUTINIT_Low); // devmatch
    gpiote_out_init(GPIO_CH2, 12, GPIOTE_CONFIG_POLARITY_Toggle, GPIOTE_CONFIG_OUTINIT_Low); // disable

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

int main(void)
{
    trickle_init(&trickle);
    uint32_t retval;

    DEBUG_INIT();

    /* Dongle RGB LED */
    NRF_GPIO->DIRSET = (1 << 13) | (1 << 14) | (1 << 21);
    NRF_GPIO->OUTCLR = (1 << 13) | (1 << 14) | (1 << 21);

    NRF_GPIO->DIRSET = (1 << 15);
    NRF_GPIO->OUTSET = (1 << 15);
    init_ppi();

    /* Mayfly shall be initialized before any ISR executes */
    mayfly_init();


    clock_k32src_start(1);
    irq_priority_set(POWER_CLOCK_IRQn, 0xFF);
    irq_enable(POWER_CLOCK_IRQn);

    cntr_init();
    irq_priority_set(RTC0_IRQn, CONFIG_BLUETOOTH_CONTROLLER_WORKER_PRIO);
    irq_enable(RTC0_IRQn);

    irq_priority_set(SWI4_IRQn, CONFIG_BLUETOOTH_CONTROLLER_JOB_PRIO);
    irq_enable(SWI4_IRQn);

    ticker_users[MAYFLY_CALL_ID_0][0] = RADIO_TICKER_USER_WORKER_OPS;
    ticker_users[MAYFLY_CALL_ID_1][0] = RADIO_TICKER_USER_JOB_OPS;
    ticker_users[MAYFLY_CALL_ID_2][0] = 0;
    ticker_users[MAYFLY_CALL_ID_PROGRAM][0] = RADIO_TICKER_USER_APP_OPS;

    ticker_init(RADIO_TICKER_INSTANCE_ID_RADIO, RADIO_TICKER_NODES,
            &ticker_nodes[0], MAYFLY_CALLER_COUNT, &ticker_users[0],
            RADIO_TICKER_USER_OPS, &ticker_user_ops[0]);

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
    ASSERT(retval == 0);

    irq_priority_set(RADIO_IRQn, CONFIG_BLUETOOTH_CONTROLLER_WORKER_PRIO);


    /* initialise address, adv and scan data */
    {
        uint8_t own_bdaddr[BDADDR_SIZE] = {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
        uint8_t own_bdaddr_type = 1;
        //uint8_t adv_data[] = {1,2,3,4};
        uint8_t adv_data[] = {0x02, 0x01, 0x06, 0x0B, 0x08, 'P', 'h', 'o', 'e', 'n', 'i', 'x', ' ', 'L', 'L'};
        uint8_t scn_data[] = {0x02, 0x01, 0x06, 0x0B, 0x08, 'P', 'h', 'o', 'e', 'n', 'i', 'x', ' ', 'L', 'L'};
        //uint8_t scn_data[] = {0x03, 0x02, 0x02, 0x18};

        ll_address_set(own_bdaddr_type, own_bdaddr);

        ll_scan_data_set(sizeof(scn_data), scn_data);

        ll_adv_data_set(sizeof(adv_data), adv_data);
    }

    /* initialise adv and scan params */
    ll_adv_params_set(get_t_value(&trickle), PDU_ADV_TYPE_ADV_IND, 0x01, 0, 0, 0x07, ADV_FILTER_POLICY);
    ll_scan_params_set(1, SCAN_INTERVAL, SCAN_WINDOW, 1, SCAN_FILTER_POLICY);


#if 1
    retval = ticker_start(RADIO_TICKER_INSTANCE_ID_RADIO /* instance */
        , 3 /* user */
        , 10 /* ticker id */
        , ticker_ticks_now_get() /* anchor point */
        , TICKER_US_TO_TICKS(trickle_config.interval_min) /* first interval */
        , trickle_config.interval_min /* periodic interval */
        , TICKER_REMAINDER(0) /* remainder */
        , 0 /* lazy */
        , 0 /* slot */
        , ticker_timeout /* timeout callback function */
        , 0 /* context */
        , 0 /* op func */
        , 0 /* op context */
        );
        ASSERT(!retval);
#endif

    ASSERT(!retval);

    retval = ll_adv_enable(1);
    ASSERT(!retval);

    #if 0
    retval = ll_scan_enable(1);
    int o = 1;
    ASSERT(!retval);
    #endif

    while (1) {
    /*
        int a = 0;
        uint16_t handle = 0;
        struct radio_pdu_node_rx *node_rx = 0;

        uint8_t num_complete = radio_rx_get(&node_rx, &handle);

        if (node_rx) {
            radio_rx_dequeue();
            // Handle PDU
            toggle_line(13);
            //
            radio_rx_mem_release(&node_rx);
        } else {
            a = 0;
        }
        int c = 1;
        */
    }
}
