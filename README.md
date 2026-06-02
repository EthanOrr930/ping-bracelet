# Ping Bracelet

A wearable "thinking of you" device. Press the button on one bracelet and its paired partner lights up and buzzes — a one-tap, wordless ping between two people. Custom firmware, circuit, and 3D-printed enclosure.

## How it works

Two bracelets pair over Bluetooth Low Energy. A press on one sends a ping; the other responds with light and a haptic buzz. The firmware is built for low power so the device lasts on a small battery in a wearable form factor.

## Repository layout

- `Code/` — device firmware, including the BLE link, button input, and LED/haptic feedback. See `Code/BLE_COMMUNICATION_GUIDE.md` for the communication protocol.
- `Casing/` — 3D-printable enclosure, iterated across several revisions (V1–V6) with `.obj` / `.3mf` print files, Fusion 360 source, and renders.
- `Schematics/` — circuit schematics.

## Hardware

- ESP32-class microcontroller with BLE
- Push button input
- LED + vibration motor feedback
- Small LiPo battery
- Custom 3D-printed two-part enclosure

## Status

Working prototype. Multiple enclosure revisions in `Casing/`.
