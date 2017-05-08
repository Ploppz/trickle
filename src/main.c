#include "trickle.h"
#include "rand.h"

#include "soc.h"
#include "cpu.h"
#include "irq.h"
#include "uart.h"

#include "misc.h"
#include "util.h"
#include "mayfly.h"

#include "clock.h"
#include "cntr.h"
#include "ticker.h"

#include "ll.h"
#include "ctrl.h"
#include "radio.h"


#include "debug.h"

trickle_config_t trickle_config = {
    .interval_min = 0xFF,
    .interval_max = 0xFFFF,
    .c_constant = 2
};
trickle_t trickle;

static uint8_t __noinit isr_stack[256];
static uint8_t __noinit main_stack[512];

void * const isr_stack_top = isr_stack + sizeof(isr_stack);
void * const main_stack_top = main_stack + sizeof(main_stack);

#define US_TO_TICKS(us) ((us * 32768) / 1000000)
#define TICKS_TO_US(ticks) ((1000000 * ticks) / 32768)

#define BASE_INTERVAL_US (1)


/* BLE consts */
#define ADV_INTERVAL_FAST  0x0020
#define ADV_INTERVAL_SLOW  0x0800

#define SCAN_INTERVAL      0x0100
#define SCAN_WINDOW        0x0050

#define CONN_INTERVAL      0x0028
#define CONN_LATENCY       0x0005
#define CONN_TIMEOUT       0x0064

#define ADV_FILTER_POLICY  0x00
#define SCAN_FILTER_POLICY 0x00

static uint8_t ALIGNED(4) ticker_nodes[RADIO_TICKER_NODES][TICKER_NODE_T_SIZE];
static uint8_t ALIGNED(4) ticker_users[MAYFLY_CALLER_COUNT][TICKER_USER_T_SIZE];
static uint8_t ALIGNED(4) ticker_user_ops[RADIO_TICKER_USER_OPS]
                        [TICKER_USER_OP_T_SIZE];

static uint8_t ALIGNED(4) radio[RADIO_MEM_MNG_SIZE];

void
toggle_line(uint32_t line) {
    NRF_GPIO->OUT ^= 1 << line;
}

void
uart0_handler(void) {
    isr_uart0(0);
}

void
power_clock_handler(void) {
    isr_power_clock(0);
}

void
rtc0_handler(void) {
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

void
swi4_handler(void) {
    mayfly_run(MAYFLY_CALL_ID_1);
}

void
radio_handler(void) {
    isr_radio(0);
}

void
mayfly_enable_cb(uint8_t caller_id, uint8_t callee_id, uint8_t enable) {
    (void)caller_id;

    ASSERT(callee_id == MAYFLY_CALL_ID_1);

    if (enable) {
        irq_enable(SWI4_IRQn);
    } else {
        irq_disable(SWI4_IRQn);
    }
}

uint32_t
mayfly_is_enabled(uint8_t caller_id, uint8_t callee_id) {
    (void)caller_id;

    if (callee_id == MAYFLY_CALL_ID_0) {
        return irq_is_enabled(RTC0_IRQn);
    } else if (callee_id == MAYFLY_CALL_ID_1) {
        return irq_is_enabled(SWI4_IRQn);
    }

    ASSERT(0);

    return 0;
}

uint32_t
mayfly_prio_is_equal(uint8_t caller_id, uint8_t callee_id) {
#if (CONFIG_BLUETOOTH_CONTROLLER_WORKER_PRIO == CONFIG_BLUETOOTH_CONTROLLER_JOB_PRIO)
    return ((caller_id == callee_id) ||
        ((caller_id == MAYFLY_CALL_ID_0) &&
         (callee_id == MAYFLY_CALL_ID_1)) ||
        ((caller_id == MAYFLY_CALL_ID_1) &&
         (callee_id == MAYFLY_CALL_ID_0)));
#else
    return (caller_id == callee_id);
#endif
}

void
mayfly_pend(uint8_t caller_id, uint8_t callee_id) {
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

void
update_has_happened(uint32_t status, void *context) {
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

void
ticker_timeout(uint32_t ticks_at_expire, uint32_t remainder, uint16_t lazy, void *context) {
    static uint32_t tick;

    (void)ticks_at_expire;
    (void)remainder;
    (void)lazy;
    (void)context;


    uint32_t interval = next_interval(&trickle);
    ticker_update(0, 3, 0, // instance, user, ticker_id
            // drift plus, drift minus:
            // Notice that the periodic interval is set to 0xFFFF
            // 0xFFFF - (0xFFFF - interval) = interval
            0, 0xFFFF - interval,
            0, 0, // slot
            0, 1, // lazy, force
            update_has_happened, 0);

    switch ((++tick) & 1) {
    case 0:
        NRF_GPIO->OUTSET = (1 << 21);
        break;
    case 1:
        NRF_GPIO->OUTCLR = (1 << 21);
        break;
    }
}


void
radio_active_callback(uint8_t active) {
    (void)active;
}

void
radio_event_callback(void) {
        toggle_line(14);
}


int
main(void) {
    uint32_t retval;
    DEBUG_INIT();

    trickle_init(&trickle);

    /* Dongle RGB LED */
    NRF_GPIO->DIRSET = (1 << 21) | (1 << 22) | (1 << 23);
    NRF_GPIO->OUTSET = (1 << 21) | (1 << 22) | (1 << 23);

    /* Mayfly shall be initialized before any ISR executes */
    mayfly_init();

    clock_k32src_start(1);
    irq_priority_set(POWER_CLOCK_IRQn, 0xFF);
    irq_enable(POWER_CLOCK_IRQn);

    cntr_init();
    irq_priority_set(RTC0_IRQn, 0xFF);
    irq_enable(RTC0_IRQn);

    irq_priority_set(SWI4_IRQn, 0xFF);
    irq_enable(SWI4_IRQn);

    ticker_users[MAYFLY_CALL_ID_0][0] = RADIO_TICKER_USER_WORKER_OPS;
    ticker_users[MAYFLY_CALL_ID_1][0] = RADIO_TICKER_USER_JOB_OPS;
    ticker_users[MAYFLY_CALL_ID_2][0] = 0;
    ticker_users[MAYFLY_CALL_ID_PROGRAM][0] = RADIO_TICKER_USER_APP_OPS;

    ticker_init(RADIO_TICKER_INSTANCE_ID_RADIO,
            RADIO_TICKER_NODES, &ticker_nodes[0],
            MAYFLY_CALLER_COUNT, &ticker_users[0],
            RADIO_TICKER_USER_OPS, &ticker_user_ops[0]);

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
        uint8_t adv_data[] = {0x02, 0x01, 0x06, 0x0B, 0x08, 'P', 'h', 'o', 'e', 'n', 'i', 'x', '2', 'L', 'L'};
        uint8_t scn_data[] = {0x02, 0x01, 0x06, 0x0B, 0x08, 'P', 'h', 'o', 'e', 'n', 'i', 'x', '2', 'L', 'L'};
        //uint8_t scn_data[] = {0x03, 0x02, 0x02, 0x18};

        ll_address_set(own_bdaddr_type, own_bdaddr);

        ll_scan_data_set(sizeof(scn_data), scn_data);

        ll_adv_data_set(sizeof(adv_data), adv_data);
    }

    /* initialise adv and scan params */
    ll_adv_params_set(21*64, PDU_ADV_TYPE_ADV_IND, 0x01, 0, 0, 0x07, ADV_FILTER_POLICY);
    ll_scan_params_set(1, SCAN_INTERVAL, SCAN_WINDOW, 1, SCAN_FILTER_POLICY);

#if 0
    ticker_start(RADIO_TICKER_INSTANCE_ID_RADIO /* instance */
        , 3 /* user */
        , 10 /* ticker id */
        , ticker_ticks_now_get() /* anchor point */
        , TICKER_US_TO_TICKS(trickle.interval) /* first interval */
        , 0xFFFF /* periodic interval */
        , TICKER_REMAINDER(0) /* remainder */
        , 0 /* lazy */
        , 0 /* slot */
        , ticker_timeout /* timeout callback function */
        , 0 /* context */
        , 0 /* op func */
        , 0 /* op context */
        );
#endif

    /* Test: Just advertise a packet */
    retval = ll_adv_one_shot(1);
    ASSERT(!retval);

    while (1) {
        cpu_sleep();
    }
}
