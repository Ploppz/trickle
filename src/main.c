#include "cpu.h" // cpu_sleep
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

static uint8_t __noinit isr_stack[256];
static uint8_t __noinit main_stack[512];

void * const isr_stack_top = isr_stack + sizeof(isr_stack);
void * const main_stack_top = main_stack + sizeof(main_stack);

void main(void) {
  while (1) {}
}
