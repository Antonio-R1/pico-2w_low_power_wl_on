#include "radio.h"
#include "config.h"

#include <string.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "btstack.h"

/* CYW43439 registers (backplane addresses). */
#define CC_MIN_RES_MASK     0x18000618u /* PMU floor: resources always on       */
#define HOST_CTRL           0x18000d6cu /* host: BT signals                   */
#define HOST_CTRL_WAKE_BT   (1u << 17)  /* "BT, stay awake"                     */
extern volatile uint32_t host_ctrl_cache_reg;   /* SDK shadow of HOST_CTRL      */

#define HCI_VSC_WRITE_SLEEP_MODE 0xfc27 /* Broadcom vendor command              */
#define BT_SLEEP_MODE_SHARED_BUS 0x11   /* mode 17: the one that lets BT sleep  */

static cyw43_ll_t *ll(void) { return &cyw43_state.cyw43_ll; }

void radio_load_firmware(void) {
    if (cyw43_arch_init()) panic("cyw43_arch_init failed");
    /* cyw43_arch_init() leaves WL_ON LOW: the SDK powers the chip up and
     * downloads the firmware only on the first command that needs it. This
     * is that command, LED off = its reset state.                          */
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
}

/* BLE bring-up
 * BTstack runs in the background (cyw43 async context). The main loop only
 * starts things and waits for the events below.                             */
static volatile bool hci_working, sleep_mode_done, adv_done;

static void on_hci_event(uint8_t type, uint16_t ch, uint8_t *pkt, uint16_t size) {
    (void)ch; (void)size;
    if (type != HCI_EVENT_PACKET) return;
    switch (hci_event_packet_get_type(pkt)) {
    case BTSTACK_EVENT_STATE:
        hci_working = btstack_event_state_get_state(pkt) == HCI_STATE_WORKING;
        break;
    case HCI_EVENT_COMMAND_COMPLETE:
        if (hci_event_command_complete_get_command_opcode(pkt) == HCI_VSC_WRITE_SLEEP_MODE)
            sleep_mode_done = true;
        if (hci_event_command_complete_get_command_opcode(pkt) == HCI_OPCODE_HCI_LE_SET_ADVERTISE_ENABLE)
            adv_done = true;
        break;
    }
}

static void wait_for(volatile bool *flag, const char *what) {
    for (int i = 0; i < 1000 && !*flag; i++) sleep_ms(10);   /* <= 10 s */
    if (!*flag) panic("BLE: no %s", what);
}

/* HCI_Write_Sleep_Mode: mode, idle thresholds host/controller, BT_WAKE and
 * HOST_WAKE active high, allow host sleep, the rest 0, UART-only fields.   */
static const hci_cmd_t write_sleep_mode = { HCI_VSC_WRITE_SLEEP_MODE, "111111111111" };

static void send_sleep_mode(void) {
    for (;;) {
        cyw43_thread_enter();
        const bool sent = hci_can_send_command_packet_now() &&
            hci_send_cmd(&write_sleep_mode, BT_SLEEP_MODE_SHARED_BUS, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0) == 0;
        cyw43_thread_exit();
        if (sent) return;
        sleep_ms(10);
    }
}

static void start_advertising(uint32_t adv_ms) {
    static uint8_t data[3 + 2 + sizeof ADV_NAME - 1];
    const uint8_t n = sizeof ADV_NAME - 1;
    data[0] = 2; data[1] = BLUETOOTH_DATA_TYPE_FLAGS; data[2] = 0x06;   /* LE only */
    data[3] = n + 1; data[4] = BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME;
    memcpy(&data[5], ADV_NAME, n);
    const uint16_t units = (uint16_t)(adv_ms * 8u / 5u);                /* 0.625 ms */
    bd_addr_t none = { 0 };
    cyw43_thread_enter();
    gap_advertisements_set_params(units, units, 3 /* ADV_NONCONN_IND */, 0, none, 0x07, 0);
    gap_advertisements_set_data(sizeof data, data);
    gap_advertisements_enable(1);
    cyw43_thread_exit();
}

void radio_ble_start(uint32_t adv_ms) {
    static btstack_packet_callback_registration_t cb = { .callback = on_hci_event };
    cyw43_thread_enter();
    hci_add_event_handler(&cb);
    hci_power_control(HCI_POWER_ON);    /* downloads the BT firmware           */
    cyw43_thread_exit();
    wait_for(&hci_working, "HCI");

    send_sleep_mode();
    wait_for(&sleep_mode_done, "sleep mode");

    if (adv_ms) {
        start_advertising(adv_ms);
        wait_for(&adv_done, "advertising");
    }
    sleep_ms(500);                      /* let the last HCI events drain       */
}

/* low-power settings
 * From here on the cyw43 lock is held for good: every HCI transfer of the SDK
 * would set WAKE_BT again, and the driver's poll would wake the bus.        */
void radio_low_power(void) {
    cyw43_thread_enter();
    cyw43_ll_bus_sleep(ll(), false);                          /* bus awake     */
    cyw43_ll_write_backplane_reg(ll(), CC_MIN_RES_MASK, 0x1); /* floor 0x1     */
    host_ctrl_cache_reg &= ~HOST_CTRL_WAKE_BT;                /* BT may sleep  */
    cyw43_ll_write_backplane_reg(ll(), HOST_CTRL, host_ctrl_cache_reg);
}

void radio_bus_sleep(void) {
    cyw43_thread_enter();               /* recursive; never released           */
    cyw43_ll_bus_sleep(ll(), true);
}
