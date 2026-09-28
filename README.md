# Pico 2W low-power with WL_ON enabled

The goal of this repository is to reduce the power consumption of the CYW43439 while still being able to control the onboard LED of the Pico 2W or
also keep the BLE advertising working.

> [!WARNING]
> The code from this repository is experimental and many parts are done with trial-and-error.

Using the onboard LED, BLE or WiFi requires enabling the `WL_ON` pin which is connected to the `WLAN_ON` and `BT_ON` registers of the CYW43439.
The CYW43439 has, according to the documentation [[1]](#1), some low-power modes.
Since the `WL_ON` pin is connected to both the `WLAN_ON` and `BT_ON` registers, we need to use the low-power modes of both parts.
We tried to enable these low-power modes and were able to reduce the current used by the board with `WLAN_ON`, pstate and RAM turned off by about 50% according to the measurement described in ["With a Voltmeter over a 10 Ohm resistor connected to VBUS"](#with-a-voltmeter-over-a-10-ohm-resistor-connected-to-vbus) section.
The code might still be improved, since we might not have released some resources of the wlan or bt core, or also of some other component.
Furthermore, it still needs to be tested what is the minimum subset of settings and patches to get the low-power mode working.

## Build and flash

### Install the required tools
```
sudo apt install git cmake python3 build-essential gcc-arm-none-eabi libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib
```

### Install the pico-sdk

You can adapt the part `/SOME_PATH/pico` for installing the `pico-sdk` in a different path.

```
export PICO_SDK_FOLDER=/SOME_PATH/pico
export PICO_SDK_PATH=$PICO_SDK_FOLDER/pico-sdk
mkdir -p $PICO_SDK_FOLDER && cd $PICO_SDK_FOLDER
git clone --branch 2.3.1 --recurse-submodules https://github.com/raspberrypi/pico-sdk.git
```

### Patch the cyw43-driver

The low-power modes need a small change in the CYW43439 driver inside the SDK.
The patch in the `patches/cyw43-driver-release-wake-up.patch` file updates the `src/cyw43_ll.c` file and adds the compile option
`CYW43_SPI_RELEASE_WAKE_UP_ON_SLEEP`: While the gSPI bus sleeps, the driver releases the `WAKE_UP` bit, so the chip's
crystal can stop, which is explained in the "Enabling the Low-Power Mode on the CYW43439" section. The option defaults to 0, so other projects using the
same SDK are not affected. Execute the following commands from the folder of this repository:

```
git -C "$PICO_SDK_PATH/lib/cyw43-driver" apply --check "$PWD/patches/cyw43-driver-release-wake-up.patch"
git -C "$PICO_SDK_PATH/lib/cyw43-driver" apply         "$PWD/patches/cyw43-driver-release-wake-up.patch"
```

The first command only tests and no output means the patch fits.

- `git -C "$PICO_SDK_PATH/lib/cyw43-driver" diff` shows the patch.
- `git -C "$PICO_SDK_PATH/lib/cyw43-driver" checkout src/cyw43_ll.c` removes the patch.

Without the patch, the CMake configure step stops with an error.

### Build

Execute the following command from this folder:
```
./build.sh
```

This builds all 8 modes for both host sleep modes into `builds/HOST/MODE/MODE.uf2`, where `HOST` is `12MHz_LPO` or `pstate_ram_off`.

### Flash

Hold BOOTSEL on the Pico 2W while plugging it in. It shows up as the USB drive `RP2350`. Copy one `.uf2` onto it and
the board reboots into the firmware.

| `HOST` | How the RP2350 sleeps |
|---|---|
| `12MHz_LPO` | clk_sys from the 12 MHz crystal, PLLs off, core at 0.90 V, clock-gated WFI |
| `pstate_ram_off` | pstate, SRAM off, The GPIO block loses power, so POWMAN holds `WL_ON` and `WL_CS` high. Every wake is a reboot, the LED is toggled as described in the ["Toggle the LED" section](#toggle-the-led). |

| `MODE` | CYW43439 | test |
|---|---|---|
| `1_wl_on_off` | WL_ON low | — |
| `2_wl_on_no_firmware` | WL_ON high, chip runs its ROM only | — |
| `3_wl_on_firmware` | WL_ON high, WLAN firmware loaded, SDK defaults | — |
| `4_lowest_no_adv` | lowest mode, BT firmware loaded, no advertising | — |
| `5_lowest_no_adv_led_5s` | as 4, LED 5 s on / 5 s off | LED blinks |
| `6_lowest_adv_100ms` | lowest mode, advertising every 100 ms | check whether it broadcasts `PicoLP` |
| `7_lowest_adv_1s` | lowest mode, advertising every 1 s | check whether it broadcasts `PicoLP` |
| `8_lowest_adv_100ms_led` | lowest mode, advertising every 100 ms, LED on for 100 ms every 10 s | check whether it broadcasts `PicoLP` and whether the LED blinks every 10 seconds |

## Power Modes of the CPU
We use the 12 MHz LPO for the builds in the `12MHz_LPO` folder and `Pstate (All SRAM Off)` in `pstate_ram_off` as described in the `pico-sdk/src/rp2_common/pico_low_power/include/pico/low_power.h` file[[2]](#2).

## Enabling the Low-Power Mode on the CYW43439

1. **Load both firmware** (`cyw43_arch_init`, `hci_power_control`). Without BT firmware the BT ROM never sleeps.
2. **BT sleep mode:** vendor HCI command `0xFC27`, `Write_Sleep_Mode`, with mode `0x11`. The SDK never sends it, so by default BT never sleeps.
3. **PMU floor:** Update `min_res_mask` from `0x7efafdf7` to `0x00000001`. The WLAN firmware sets the high floor because the Pico NVRAM says `boardflags3 = FORCE_INT_LPO`. That floor keeps the crystal, the PLL and the radio on permanently.
4. **Release WAKE_BT:** clear bit 17 of `HOST_CTRL`. The SDK sets it on every HCI transfer and never clears it, so BT stays awake.
5. **Bus sleep:** `cyw43_ll_bus_sleep(true)` = KSO, Keep SDIO On, off. With the driver patch it also clears the gSPI `WAKE_UP` bit, F0 register 0, bit 7. While `WAKE_UP` is set, the crystal keeps running.
6. **Nothing touches the chip again:** the cyw43 lock is kept, so neither the driver poll nor BTstack runs. Every HCI transfer would set WAKE_BT again.

## Toggle the LED

The LED is on CYW43439 pin `WL_GPIO0`. `src/wl_led.c` switches it by writing ChipCommon `gpioouten`/`gpioout`, `0x18000068`/`0x18000064`, bit 0, over gSPI: wake the bus, write, sleep the bus. There is no firmware call, so the WLAN CPU is not woken. The Pstate host reboots on every wake. The CYW43439 keeps running, and only the gSPI pins/PIO are set up again, `wl_led_use_new_bus`, without resetting the chip. The LED state survives in POWMAN `scratch[0]`.

## Files

| File | Content |
|---|---|
| `src/config.h` | the 8 modes: radio level, advertising interval, LED timing |
| `src/main.c` | the sequence: host init, radio up, low-power steps, sleep, LED |
| `src/host.c` | RP2350: 12 MHz sleep, Pstate, POWMAN pin holds |
| `src/radio.c` | CYW43439: firmware, BLE (sleep mode, advertising), floor, WAKE_BT, bus sleep |
| `src/wl_led.c` | LED through direct gSPI register access |
| `patches/cyw43-driver-release-wake-up.patch` | the cyw43-driver patch, applied to the SDK in the section "Build and flash" |

## Measurements

### With a Voltmeter over a 10 Ohm resistor connected to VBUS

The board is powered with 5V and with a 10 Ohm resistor, which serves as a shunt resistor, between the power source and VBUS
we measure the voltage over the resistor.

| `mode` | measurement |
| --- | --- |
| `3_wl_on_firmware` | 88 mV, which is about 8.8 mA |
| `8_lowest_adv_100ms_led` | 43 mV, which is about 4.3 mA  |

## Contribute
If you find any bugs or find a way to further decrease the power consumption of the Pico 2W while keeping `WL_ON` enabled, feel free to open an issue or a pullrequest.

## License

MIT (see `LICENSE`), except `patches/cyw43-driver-release-wake-up.patch`: its added lines are MIT, its unchanged
context lines are excerpts of cyw43-driver (George Robotics) under the cyw43-driver license, whose full text is in
the patch header.

## References
- <a id="1">[1]</a> <https://github.com/raspberrypi/pico-feedback/issues/294>, archived: <https://web.archive.org/web/20221219231644/https://www.infineon.com/dgdl/Infineon-CYW43439-DataSheet-v03_00-EN.pdf?fileId=8ac78c8c8386267f0183c320336c029f>
- <a id="2">[2]</a> <https://github.com/raspberrypi/pico-sdk/blob/079c6f39023649b154152db30f1d781e884879bc/src/rp2_common/pico_low_power/include/pico/low_power.h#L24-L69>

