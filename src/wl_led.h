#pragma once
/* The on-board LED hangs on the CYW43439 pin WL_GPIO0. It is switched here by
 * writing the chip's GPIO registers directly over gSPI (no firmware call, so
 * the WLAN CPU stays asleep). Each call wakes the gSPI bus and puts it back to
 * sleep afterwards.                                                          */
#include <stdbool.h>

/* Use the SDK driver's bus (after radio_load_firmware, same boot). */
void wl_led_use_driver_bus(void);

/* After a Pstate reboot: the chip kept running, only the RP2350 restarted.
 * Set up the gSPI pins/PIO again, without resetting the chip.               */
void wl_led_use_new_bus(void);

void wl_led_set(bool on);
