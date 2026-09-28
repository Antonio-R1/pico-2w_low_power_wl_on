#include "wl_led.h"

#include <string.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/powman.h"
#include "cyw43_internal.h"             /* cyw43_int_t, cyw43_read/write_reg_*  */
#include "cyw43_spi.h"                  /* cyw43_spi_init                       */

/* gSPI function 0 (the bus core) and function 1 (backplane) registers. */
/* SPI_BUS_CONTROL (F0 0x0000) comes from cyw43_spi.h; bit 7 = WAKE_UP.      */
#define SPI_WAKE_UP         0x80u
#define F1_WINDOW_LOW       0x1000au    /* backplane address bits 15..8         */
#define F1_WINDOW_MID       0x1000bu    /*                    bits 23..16       */
#define F1_WINDOW_HIGH      0x1000cu    /*                    bits 31..24       */
#define F1_SLEEP_CSR        0x1001fu    /* bit 0 KSO (keep SDIO on), bit 1 on   */
#define F1_ACCESS_32BIT     0x8000u

/* ChipCommon GPIO registers; bit 0 = WL_GPIO0 = the LED. */
#define CC_GPIO_OUT         0x18000064u
#define CC_GPIO_OUT_EN      0x18000068u
#define LED_BIT             0x1u

static cyw43_int_t *bus;

void wl_led_use_driver_bus(void) {
    bus = (cyw43_int_t *)&cyw43_state.cyw43_ll;
}

void wl_led_use_new_bus(void) {
    static cyw43_int_t fresh;
    memset(&fresh, 0, sizeof fresh);
    /* WL_CS: take it over from the POWMAN hold, high (deselected) throughout. */
    gpio_put(CYW43_DEFAULT_PIN_WL_CS, 1);
    gpio_set_dir(CYW43_DEFAULT_PIN_WL_CS, GPIO_OUT);
    gpio_set_function(CYW43_DEFAULT_PIN_WL_CS, GPIO_FUNC_SIO);
    powman_hw->ext_ctrl[1] = POWMAN_PASSWORD_BITS | POWMAN_EXT_CTRL1_RESET;
    /* WL_ON stays held high by POWMAN (EXT_CTRL0) and is never touched here. */
    if (cyw43_spi_init(&fresh) != 0) panic("gSPI init");
    bus = &fresh;
}

/* gSPI bus sleep/wake (same register sequence as the driver) */
static void bus_wake(void) {
    const uint8_t ctl = (uint8_t)cyw43_read_reg_u32(bus, BUS_FUNCTION, SPI_BUS_CONTROL);
    cyw43_write_reg_u8(bus, BUS_FUNCTION, SPI_BUS_CONTROL, ctl | SPI_WAKE_UP);
    cyw43_write_reg_u8(bus, BACKPLANE_FUNCTION, F1_SLEEP_CSR, 1);
    for (int i = 0; i < 50; i++) {      /* KSO on, wait for "device on"         */
        const int v = cyw43_read_reg_u8(bus, BACKPLANE_FUNCTION, F1_SLEEP_CSR);
        if (v >= 0 && v != 0xff && (v & 3) == 3) return;
        busy_wait_us_32(1000);      /* no timer IRQ needed */
        cyw43_write_reg_u8(bus, BACKPLANE_FUNCTION, F1_SLEEP_CSR, 1);
    }
}

static void bus_sleep(void) {
    cyw43_write_reg_u8(bus, BACKPLANE_FUNCTION, F1_SLEEP_CSR, 0);    /* KSO off */
    for (int i = 0; i < 50; i++) {
        const int v = cyw43_read_reg_u8(bus, BACKPLANE_FUNCTION, F1_SLEEP_CSR);
        /* 0xff: F1 no longer answers because the backplane clock is gone = asleep */
        if (v == 0xff || (v >= 0 && (v & 1) == 0)) break;
        busy_wait_us_32(1000);      /* no timer IRQ needed */
        cyw43_write_reg_u8(bus, BACKPLANE_FUNCTION, F1_SLEEP_CSR, 0);
    }
    const uint8_t ctl = (uint8_t)cyw43_read_reg_u32(bus, BUS_FUNCTION, SPI_BUS_CONTROL);
    cyw43_write_reg_u8(bus, BUS_FUNCTION, SPI_BUS_CONTROL, ctl & ~SPI_WAKE_UP);
}

/* backplane access through the F1 address window */
static void window(uint32_t addr) {
    cyw43_write_reg_u8(bus, BACKPLANE_FUNCTION, F1_WINDOW_LOW,  (addr >> 8) & 0xff);
    cyw43_write_reg_u8(bus, BACKPLANE_FUNCTION, F1_WINDOW_MID,  (addr >> 16) & 0xff);
    cyw43_write_reg_u8(bus, BACKPLANE_FUNCTION, F1_WINDOW_HIGH, (addr >> 24) & 0xff);
}

static uint32_t bp_read(uint32_t addr) {
    window(addr);
    return cyw43_read_reg_u32(bus, BACKPLANE_FUNCTION, (addr & 0x7fff) | F1_ACCESS_32BIT);
}

static void bp_write(uint32_t addr, uint32_t val) {
    window(addr);
    cyw43_write_reg_u32(bus, BACKPLANE_FUNCTION, (addr & 0x7fff) | F1_ACCESS_32BIT, val);
}

void wl_led_set(bool on) {
    bus_wake();
    bp_write(CC_GPIO_OUT_EN, bp_read(CC_GPIO_OUT_EN) | LED_BIT);
    const uint32_t out = bp_read(CC_GPIO_OUT);
    bp_write(CC_GPIO_OUT, on ? (out | LED_BIT) : (out & ~LED_BIT));
    bus_sleep();
}
