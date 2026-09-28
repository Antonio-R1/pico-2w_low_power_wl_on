#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Cold boot: 150 MHz for the bring-up, USB PLL off, core 1 parked, unused
 * pins in their low-leakage state, POWMAN pin holds from a Pstate released. */
void host_init(void);

/* Drive WL_ON, GPIO23, high and keep WL_CS, GPIO25, high = chip powered and
 * the gSPI slave deselected. Used by the RADIO_NO_FW build.                  */
void host_wl_on_high(void);

/* Sleep and never wake, the Pstate host wakes once a day, reboots and goes
 * straight back. radio_on: keep WL_ON and WL_CS high while sleeping.       */
void host_sleep_forever(bool radio_on) __attribute__((noreturn));

#if !WLP_PSTATE
/* 12 MHz host: sleep for ms, clock-gated WFI at 12 MHz,, return at 150 MHz. */
void host_sleep_ms(uint32_t ms);
#else
/* Pstate host: power down for ms; the wake is a reboot into main(). led_on is
 * remembered across it, POWMAN scratch register.                            */
void host_pstate_for_ms(uint32_t ms, bool led_on) __attribute__((noreturn));
/* true if this boot is a wake from host_pstate_for_ms / host_sleep_forever. */
bool host_woke_from_pstate(void);
/* The led_on value stored by the last host_pstate_for_ms.                   */
bool host_pstate_led_was_on(void);
#endif
