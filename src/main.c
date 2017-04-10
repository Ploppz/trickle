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

#include "config.h"

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

#define TICKER_NODE_COUNT 1
#define TICKER_USER_COUNT MAYFLY_CALLER_COUNT
#define TICKER_USER0_OP_COUNT 0
#define TICKER_USER1_OP_COUNT 0
#define TICKER_USER2_OP_COUNT 0
#define TICKER_USER3_OP_COUNT 5
#define TICKER_USER_OP_TOTAL (TICKER_USER0_OP_COUNT + \
                  TICKER_USER1_OP_COUNT + \
                  TICKER_USER2_OP_COUNT + \
                  TICKER_USER3_OP_COUNT)
static uint8_t ALIGNED(4) ticker_nodes[TICKER_NODE_COUNT][TICKER_NODE_T_SIZE];
static uint8_t ALIGNED(4) ticker_users[TICKER_USER_COUNT][TICKER_USER_T_SIZE];
static uint8_t ALIGNED(4) ticker_user_ops[TICKER_USER_OP_TOTAL]
                        [TICKER_USER_OP_T_SIZE];


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

void mayfly_enable_cb(uint8_t caller_id, uint8_t callee_id, uint8_t enable)
{
    (void)caller_id;
    (void)callee_id;
    (void)enable;
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
    return (caller_id == callee_id) ||
           ((caller_id == MAYFLY_CALL_ID_0) &&
        (callee_id == MAYFLY_CALL_ID_1)) ||
           ((caller_id == MAYFLY_CALL_ID_1) &&
        (callee_id == MAYFLY_CALL_ID_0));
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

    case MAYFLY_CALL_ID_2:
    case MAYFLY_CALL_ID_PROGRAM:
    default:
        ASSERT(0);
        break;
    }
}

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

int main(void)
{
    DEBUG_INIT();

    trickle_init(&trickle);

    /* Dongle RGB LED */
    NRF_GPIO->DIRSET = (1 << 21) | (1 << 22) | (1 << 23);
    NRF_GPIO->OUTSET = (1 << 21) | (1 << 22) | (1 << 23);

    /* Mayfly shall be initialized before any ISR executes */
    mayfly_init();

    #if 0
    uart_init(UART, 1);
    irq_priority_set(UART0_IRQn, 0xFF);
    irq_enable(UART0_IRQn);

    uart_tx_str("\n\n\nTicker.\n");

    {
        extern void assert_print(void);

        assert_print();
    }
    #endif

    clock_k32src_start(1);
    irq_priority_set(POWER_CLOCK_IRQn, 0xFF);
    irq_enable(POWER_CLOCK_IRQn);

    cntr_init();
    irq_priority_set(RTC0_IRQn, 0xFF);
    irq_enable(RTC0_IRQn);

    irq_priority_set(SWI4_IRQn, 0xFF);
    irq_enable(SWI4_IRQn);

    ticker_users[0][0]  = TICKER_USER0_OP_COUNT;
    ticker_users[1][0]  = TICKER_USER1_OP_COUNT;
    ticker_users[2][0]  = TICKER_USER2_OP_COUNT;
    ticker_users[3][0]  = TICKER_USER3_OP_COUNT;

    ticker_init(0,
        TICKER_NODE_COUNT, ticker_nodes,
        TICKER_USER_COUNT, ticker_users,
        TICKER_USER_OP_TOTAL, ticker_user_ops);

    ticker_start(0 /* instance */
        , 3 /* user */
        , 0 /* ticker id */
        , ticker_ticks_now_get() /* anchor point */
        , TICKER_US_TO_TICKS(trickle.interval) /* first interval */
        , TICKER_US_TO_TICKS(0xFFFF) /* periodic interval */
        , TICKER_REMAINDER(0) /* remainder */
        , 0 /* lazy */
        , 0 /* slot */
        , ticker_timeout /* timeout callback function */
        , 0 /* context */
        , 0 /* op func */
        , 0 /* op context */
        );


    while (1) {
        cpu_sleep();
    }
}
