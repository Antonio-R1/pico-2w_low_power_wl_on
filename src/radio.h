#pragma once
/* The CYW43439 side, in the order main() calls it. */
#include <stdint.h>

/* WL_ON high, WLAN firmware downloaded (SDK defaults).                        */
void radio_load_firmware(void);

/* BT firmware downloaded (HCI up), BT sleep mode 17 set, and BLE advertising
 * started as a non-connectable broadcast every adv_ms, 0 = no advertising. */
void radio_ble_start(uint32_t adv_ms);

/* The low-power settings of the CYW43439: PMU floor min_res = 0x1, WAKE_BT released.          */
void radio_low_power(void);

/* Put the gSPI bus to sleep (KSO off; in the low-power builds the driver also
 * clears WAKE_UP). Last radio call: the cyw43 lock is kept, so neither the
 * driver nor BTstack runs again.                                             */
void radio_bus_sleep(void);
