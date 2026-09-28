#include "host.h"

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/low_power.h"
#include "pico/cyw43_arch.h"
#include "hardware/clocks.h"
#include "hardware/irq.h"
#include "hardware/pll.h"
#include "hardware/powman.h"
#include "hardware/sync.h"
#include "hardware/vreg.h"
#include "hardware/structs/scb.h"

#define RUN_KHZ   150000u               /* SDK default clock, used while awake */
#define SLEEP_KHZ 12000u                /* crystal: the clock while asleep     */
#define PIN_WL_ON CYW43_DEFAULT_PIN_WL_REG_ON   /* GPIO23 -> WL_REG_ON + BT_REG_ON */
#define PIN_WL_CS CYW43_DEFAULT_PIN_WL_CS       /* GPIO25 -> gSPI chip select      */

/* Core 1 has nothing to do: deep sleep, forever. */
static void core1_sleep(void) {
    scb_hw->scr |= ARM_CPU_PREFIXED(SCR_SLEEPDEEP_BITS);
    for (;;) __wfi();
}

/* POWMAN pin holds:
 * In Pstate the RP2350's GPIO block has no power. POWMAN, which stays powered,
 * can drive two pins, EXT_CTRL0/1: we use them to keep WL_ON and WL_CS high. */
static void powman_hold_high(uint idx, uint gpio) {
    /* The GPIO itself high as well: the pad latches this level at power-down. */
    gpio_put(gpio, 1);
    gpio_set_dir(gpio, GPIO_OUT);
    gpio_set_function(gpio, GPIO_FUNC_SIO);
    const uint32_t cfg = POWMAN_EXT_CTRL0_LP_EXIT_STATE_BITS | POWMAN_EXT_CTRL0_LP_ENTRY_STATE_BITS |
                         POWMAN_EXT_CTRL0_INIT_STATE_BITS | gpio;
    powman_hw->ext_ctrl[idx] = POWMAN_PASSWORD_BITS | cfg;
    powman_hw->ext_ctrl[idx] = POWMAN_PASSWORD_BITS | cfg | POWMAN_EXT_CTRL0_INIT_BITS;
}

static void powman_release_pins(void) {
    powman_hw->ext_ctrl[0] = POWMAN_PASSWORD_BITS | POWMAN_EXT_CTRL0_RESET;
    powman_hw->ext_ctrl[1] = POWMAN_PASSWORD_BITS | POWMAN_EXT_CTRL1_RESET;
}

// public

void host_init(void) {
    powman_release_pins();              // after a Pstate: give WL_ON back
    powman_hw->scratch[0] = 0;          // forget any Pstate LED state
    clock_stop(clk_usb);                // no USB in these builds
    clock_stop(clk_hstx);
    pll_deinit(pll_usb);
    low_power_set_pins_low_leakage_exclude_mask(0);  // all pins: no pulls, no input buffer
    multicore_launch_core1(core1_sleep);
}

void host_wl_on_high(void) {
    gpio_init(PIN_WL_CS);
    gpio_put(PIN_WL_CS, 1);
    gpio_set_dir(PIN_WL_CS, GPIO_OUT);
    gpio_init(PIN_WL_ON);
    gpio_put(PIN_WL_ON, 1);
    gpio_set_dir(PIN_WL_ON, GPIO_OUT);
    sleep_ms(500);                      /// let the chip boot its ROM
}

#if !WLP_PSTATE

/* brown-out detector:
 * Its reset threshold (0.946 V) is above the 0.90 V sleep rail, so it has to
 * come down before the rail does and go up after the rail is back.
 * POWMAN registers need the 0x5afe password in the top half-word.           */
static void bod_set(uint32_t vsel) {
    const uint32_t bod = (powman_hw->bod & ~POWMAN_BOD_VSEL_BITS) | (vsel << POWMAN_BOD_VSEL_LSB);
    powman_hw->bod = POWMAN_PASSWORD_BITS | (bod & 0xffffu);
}

/* 150 MHz -> 12 MHz: clk_sys straight from the crystal, pll_sys off, 0.90 V. */
static void clocks_down(void) {
    clock_configure(clk_sys, CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLK_REF, 0,
                    SLEEP_KHZ * 1000u, SLEEP_KHZ * 1000u);
    pll_deinit(pll_sys);
    bod_set(0x08);                      /* 0.817 V */
    vreg_set_voltage(VREG_VOLTAGE_0_90);
}

/* 12 MHz -> 150 MHz: rail first, then the detector, then the PLL. */
static void clocks_up(void) {
    uint vco, pd1, pd2;
    check_sys_clock_khz(RUN_KHZ, &vco, &pd1, &pd2);
    vreg_set_voltage(VREG_VOLTAGE_1_10);
    busy_wait_us_32(200);
    bod_set(POWMAN_BOD_VSEL_RESET);     /* 0.946 V */
    pll_init(pll_sys, PLL_SYS_REFDIV, vco, pd1, pd2);
    clock_configure_undivided(clk_sys, CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLKSRC_CLK_SYS_AUX,
                              CLOCKS_CLK_SYS_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS, RUN_KHZ * 1000u);
}

/* Every interrupt off at the NVIC, pending ones cleared: nothing but our own
 * wake-up alarm may end a sleep (the SDK sleep call enables that one).      */
static void irqs_off(void) {
    for (uint i = 0; i < NUM_IRQS; i++) {
        irq_set_enabled(i, false);
        irq_clear(i);
    }
}

void host_sleep_forever(bool radio_on) {
    (void)radio_on;                     /* the GPIOs keep their level          */
    irqs_off();
    save_and_disable_interrupts();
    clocks_down();
    for (;;) low_power_sleep_until_irq(NULL);   /* clock-gated WFI, no wake source */
}

void host_sleep_ms(uint32_t ms) {
    const absolute_time_t until = make_timeout_time_ms(ms);
    irqs_off();
    clocks_down();
    low_power_sleep_until_default_timer(until, NULL, true);
    clocks_up();
}

#else /* WLP_PSTATE */

#define PSTATE_MAGIC  0x574c0000u       /* "WL" in scratch[0]: a Pstate is ours */
#define PSTATE_LED_ON 0x1u
#define ONE_DAY_MS    86400000u

void host_pstate_for_ms(uint32_t ms, bool led_on) {
    powman_hw->scratch[0] = PSTATE_MAGIC | (led_on ? PSTATE_LED_ON : 0);
    powman_hold_high(0, PIN_WL_ON);
    powman_hold_high(1, PIN_WL_CS);
    scb_hw->scr |= ARM_CPU_PREFIXED(SCR_SLEEPDEEP_BITS);
    pstate_bitset_t all_off = pstate_bitset_none();          /* all SRAM off  */
    low_power_pstate_for_ms(ms, &all_off, NULL);             /* reboots on wake */
    for (;;) __wfi();                   /* only reached if the SDK refused     */
}

void host_sleep_forever(bool radio_on) {
    if (radio_on) {
        host_pstate_for_ms(ONE_DAY_MS, false);
    }
    powman_hw->scratch[0] = PSTATE_MAGIC;
    scb_hw->scr |= ARM_CPU_PREFIXED(SCR_SLEEPDEEP_BITS);
    pstate_bitset_t all_off = pstate_bitset_none();
    low_power_pstate_for_ms(ONE_DAY_MS, &all_off, NULL);
    for (;;) __wfi();
}

bool host_woke_from_pstate(void) {
    return (powman_hw->chip_reset & POWMAN_CHIP_RESET_HAD_SWCORE_PD_BITS) &&
           (powman_hw->scratch[0] & 0xffff0000u) == PSTATE_MAGIC;
}

bool host_pstate_led_was_on(void) {
    return powman_hw->scratch[0] & PSTATE_LED_ON;
}

#endif
