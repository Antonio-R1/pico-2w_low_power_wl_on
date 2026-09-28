#pragma once
/* What each build does. CMake sets WLP_MODE (1..8) and WLP_PSTATE (0/1).
 *
 *   RADIO        how far the CYW43439 is brought up
 *   ADV_MS       BLE advertising interval, 0 = no advertising
 *   LED_ON_MS    on-board LED on time, 0 = LED never used
 *   LED_OFF_MS   on-board LED off time                                       */

#define RADIO_OFF        0   /* WL_ON low: the chip has no power              */
#define RADIO_NO_FW      1   /* WL_ON high, nothing loaded (chip ROM only)    */
#define RADIO_FW         2   /* WL_ON high, WLAN firmware loaded (SDK default)*/
#define RADIO_LOW_POWER  3   /* + BT firmware, every low-power step (README)  */

#if   WLP_MODE == 1                     /* 1_wl_on_off                        */
#define RADIO RADIO_OFF
#elif WLP_MODE == 2                     /* 2_wl_on_no_firmware                */
#define RADIO RADIO_NO_FW
#elif WLP_MODE == 3                     /* 3_wl_on_firmware                   */
#define RADIO RADIO_FW
#elif WLP_MODE == 4                     /* 4_lowest_no_adv                    */
#define RADIO RADIO_LOW_POWER
#elif WLP_MODE == 5                     /* 5_lowest_no_adv_led_5s             */
#define RADIO RADIO_LOW_POWER
#define LED_ON_MS   5000
#define LED_OFF_MS  5000
#elif WLP_MODE == 6                     /* 6_lowest_adv_100ms                 */
#define RADIO RADIO_LOW_POWER
#define ADV_MS 100
#elif WLP_MODE == 7                     /* 7_lowest_adv_1s                    */
#define RADIO RADIO_LOW_POWER
#define ADV_MS 1000
#elif WLP_MODE == 8                     /* 8_lowest_adv_100ms_led             */
#define RADIO RADIO_LOW_POWER
#define ADV_MS 100
#define LED_ON_MS   100
#define LED_OFF_MS  9900
#else
#error "WLP_MODE must be 1..8"
#endif

#ifndef ADV_MS
#define ADV_MS 0
#endif
#ifndef LED_ON_MS
#define LED_ON_MS  0
#define LED_OFF_MS 0
#endif

#define ADV_NAME "PicoLP"       /* name seen by a BLE scanner                  */
