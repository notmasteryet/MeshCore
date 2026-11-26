# MeshSyd: touch UI for the CYD + LR1121 companion

An LVGL 8.3 touch interface for MeshCore's companion radio, built for a modified
ESP32-2432S028 "Cheap Yellow Display" (classic ESP32, 4 MB flash, no PSRAM) with a
Semtech LR1121 radio. Portrait 240×320, resistive XPT2046 touch.

Build and flash (board variant: `variants/cyd_lr1121`):

```bash
pio run -e CYD_LR1121_meshsyd -t upload          # release
pio run -e CYD_LR1121_meshsyd_debug -t upload    # + serial logs (heap, touch, packets)
```

The first flash after switching from another layout needs `esptool.py erase_flash`
(the env uses `huge_app.csv`). That creates a new node identity.

## Screens

- **Home:** clock, unread messages, heard nodes, last RX, advert
- **Chats:** conversation, compose
- **Contacts:** each contact has a detail page with Message, Ping (trace), Reset path, Edit path (pick repeaters or type hex IDs), and Delete
- **Repeaters / rooms:** admin login (password saved per node), Status, Console
- **Settings:** node name, Bluetooth, brightness, screen-off, time zone

Chat history (24 conversations, 48 messages) and UI prefs are stored in SPIFFS.

## Fonts

The `fonts/*.c` files are generated with `tools/cyd_ui/mkfont.py` (Python + Pillow, a stand-in
for lv_font_conv) from:

- [Inter](https://rsms.me/inter/) 4.1 by Rasmus Andersson, SIL Open Font License 1.1
- Font Awesome 5 Free (from LVGL's `scripts/built_in_font`), SIL Open Font License 1.1
