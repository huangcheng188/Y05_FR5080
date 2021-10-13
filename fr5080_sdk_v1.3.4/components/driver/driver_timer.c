#include "driver_timer.h"
#include <stdio.h>

__attribute__((section("ram_code"))) void timer0_isr_ram(void)
{
    timer_clear_interrupt(TIMER0);

}

__attribute__((section("ram_code"))) void timer1_isr_ram(void)
{
    timer_clear_interrupt(TIMER1);

}

