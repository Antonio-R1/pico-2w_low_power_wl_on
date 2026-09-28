#include "config.h"
#include "host.h"
#include "radio.h"
#include "wl_led.h"

#if WLP_PSTATE
/* Pstate host: every wake is a reboot. The CYW43439 kept running; only the
 * LED has to be switched then back to Pstate.                             */
static void after_pstate_wake(void) {
#if LED_ON_MS
    const bool led_on = !host_pstate_led_was_on();
    wl_led_use_new_bus();
    wl_led_set(led_on);
    host_pstate_for_ms(led_on ? LED_ON_MS : LED_OFF_MS, led_on);
#else
    host_sleep_forever(RADIO != RADIO_OFF);
#endif
}
#endif

int main(void) {
#if WLP_PSTATE
    if (host_woke_from_pstate()) after_pstate_wake();
#endif
    host_init();

    // bring the CYW43439 up as far as this mode needs
#if RADIO == RADIO_NO_FW
    host_wl_on_high();
#elif RADIO >= RADIO_FW
    radio_load_firmware();
#endif
#if RADIO == RADIO_LOW_POWER
    radio_ble_start(ADV_MS);
    radio_low_power();
#endif
#if RADIO >= RADIO_FW
    radio_bus_sleep();
#endif

    // sleep, with or without the LED
#if LED_ON_MS == 0
    host_sleep_forever(RADIO != RADIO_OFF);
#else
    wl_led_use_driver_bus();
    wl_led_set(true);
#if WLP_PSTATE
    host_pstate_for_ms(LED_ON_MS, true);        // continues in after_pstate_wake
#else
    for (;;) {
        host_sleep_ms(LED_ON_MS);
        wl_led_set(false);
        host_sleep_ms(LED_OFF_MS);
        wl_led_set(true);
    }
#endif
#endif
}
