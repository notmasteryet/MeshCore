# MeshSyd: touch UI for the CYD + LR1121 companion

MeshSyd is an LVGL 8.3 touch interface for MeshCore's companion radio firmware. It's built for
a modified ESP32-2432S028 "Cheap Yellow Display" (classic ESP32, 4 MB flash, no PSRAM) with a
Semtech LR1121 LoRa radio. The screen is used in portrait at 240×320 with resistive XPT2046 touch.

It works on its own and alongside the official MeshCore phone apps over Bluetooth.

## Hardware

| Function | Pins |
|---|---|
| LR1121 (VSPI) | SCK 18, MISO 19, MOSI 23, NSS 32, RESET 25, IRQ/DIO9 35, BUSY 39. TCXO 3.0 V. RF switch DIO5/DIO6 |
| Display ST7789 240×320 (HSPI) | SCK 14, MOSI 13, MISO 12, CS 15, DC 2, backlight 27 |
| Touch XPT2046 (shares HSPI) | CS 33 |
| RGB LED (common anode, active LOW) | red 4, green 17, blue 16 |
| Battery sense | GPIO34, divider ×2.48 |
| Button | GPIO0 (BOOT) |
| I2C (optional RTC) | SDA 21, SCL 22. A DS3231 / PCF8563 / RV3028 is detected automatically |

The pin map comes from the Meshtastic CYD port
(`notmasteryet/firmware`, `variants/esp32/cyd`). The board definition is in `variants/cyd_lr1121/`.

## Build

### 1. Install PlatformIO

Use either:

- **The command line:** `pip install -U platformio` (needs Python 3.8+). The tool is `pio`.
- **VS Code:** install the "PlatformIO IDE" extension.

The first build downloads the ESP32 toolchain and libraries (LVGL, LovyanGFX, RadioLib).
That takes a few minutes.

### 2. Get the source

```bash
git clone -b meshsyd https://github.com/notmasteryet/MeshCore.git
cd MeshCore
```

### 3. Build

```bash
pio run -e CYD_LR1121_meshsyd          # release
pio run -e CYD_LR1121_meshsyd_debug    # same, plus serial logs (heap, touch, every packet)
```

The firmware ends up at `.pio/build/<env>/firmware.bin` (about 1.7 MB of the 3 MB app partition).

## Flash

Connect the board **directly** to the computer with a **data** USB cable. Some cables only carry
power, and some USB hubs can't power the board while the radio transmits, which causes a reboot loop.

Find the serial port (the board's USB chip is a CH340):

| OS | Port looks like |
|---|---|
| macOS | `/dev/cu.usbserial-XXXX` (`ls /dev/cu.usbserial*`) |
| Linux | `/dev/ttyUSB0` |
| Windows | `COM3` etc. (Device Manager; install the CH340 driver if missing) |

### First install

The first install, or switching from Meshtastic or another layout, needs an erase:

```bash
pio pkg exec -p tool-esptoolpy -- esptool.py --port <PORT> erase_flash
pio run -e CYD_LR1121_meshsyd -t upload --upload-port <PORT>
```

The erase wipes everything. The device starts with a **new node identity**, so re-pair the phone app afterwards.

### Updates

Later updates keep contacts, channels, chat history and settings:

```bash
pio run -e CYD_LR1121_meshsyd -t upload --upload-port <PORT>
```

### Serial monitor

```bash
pio device monitor -p <PORT> -b 115200
```

### Optional: back up the old firmware first

```bash
pio pkg exec -p tool-esptoolpy -- esptool.py --port <PORT> read_flash 0 0x400000 backup-4MB.bin
# restore later with:
pio pkg exec -p tool-esptoolpy -- esptool.py --port <PORT> write_flash 0 backup-4MB.bin
```

## First run

1. The splash screen is followed by **Home**.
2. The Bluetooth PIN shows on Home until a phone connects. Pair the MeshCore app with it.
3. The app sets the region radio preset and the clock. Without the phone or an RTC, the clock is
   taken from the first advert heard on the mesh. Fine-tune it in **Settings › Clock**.
4. Set your **time zone** in Settings.

## Using it

- **Home:** clock, radio settings (frequency · bandwidth · SF · CR · TX power), unread messages, nodes heard in 24 h, last RX signal, Send advert.
- **Chats:**
  - Channels and direct messages. Sent messages show *heard N repeats* or *delivered ✓*.
  - **+** adds a channel: a name plus a 32-hex key, or an empty key to create a new private channel.
  - Tap a channel's name to see its key, share it by DM, or leave the channel.
- **Contacts:**
  - **+** adds a contact by public key (64 hex).
  - The tower icon shares your contact by advert, to nearby nodes or the whole mesh.
  - Tap a contact for Message, Ping (trace), Reset path, Edit path (pick repeaters or type hex IDs) and Delete.
  - Repeaters and rooms also get **Status** and **Console**: admin login, with the password saved per node and a Forget option.
- **Settings:** node name, radio (view only), Bluetooth, brightness, screen-off, message LED, clock, time zone.
- **BOOT button:** click turns the screen on/off. Hold for 1 s to go Home.
- **LED:** amber means a direct message, green a channel message. It repeats every 5 s while anything is unread.

Chat history (24 conversations, 48 messages), settings and saved admin passwords are kept in
SPIFFS. Saved passwords are stored in plain text.

## Troubleshooting

| Symptom | Cause / fix |
|---|---|
| Board not detected | Charge-only cable, or a hub in the way. Use a data cable, directly. |
| "Loading" flashing / reboot loop | Not enough power on USB (hub, weak port). Plug in directly. |
| Repeater login gets no reply | Wrong password, **or** the clock is behind: repeaters drop logins older than the last accepted one. Connect the phone app (it sets the clock), then retry. |
| Clock a few minutes off after a reflash | The clock stands still while flashing. The phone app moves it forward; **Settings › Clock** can move it either way. |
| Touch dead after the screen sleeps | Fixed in this branch; update. |

## Development notes

- UI code: `examples/companion_radio/ui-cyd/` (`UITask.cpp`), LVGL config `lv_conf.h`.
- Mesh hooks: small `ui*` helpers and listener callbacks added to `examples/companion_radio/MyMesh.{h,cpp}`.
- Memory: classic ESP32 with no PSRAM. LVGL uses the heap (`LV_MEM_CUSTOM`). Bluetooth Classic RAM is
  released at boot. About 30–55 KB of heap stays free. LVGL is kept out of IRAM (it's full).
- LVGL asserts reboot with a log line instead of hanging.

## Fonts

The `fonts/*.c` files are generated with `tools/cyd_ui/mkfont.py` (Python + Pillow + fontTools, a
stand-in for lv_font_conv):

```bash
python tools/cyd_ui/mkfont.py <font.ttf> <size_px> <c_name> "<ranges>" <out.c>
# e.g. python tools/cyd_ui/mkfont.py Inter-Regular.ttf 12 inter_12 "0x20-0x7E,0xA0-0xFF,0x400-0x45F" inter_12.c
```

Sources:

- [Inter](https://rsms.me/inter/) 4.1 by Rasmus Andersson, SIL Open Font License 1.1
- Font Awesome 5 Free (from LVGL's `scripts/built_in_font`), SIL Open Font License 1.1
