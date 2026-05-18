# Lomo C/R: Lomo'Instant Wide Glass Remote & Self-Timer

[English](README.md) | [简体中文](README.zh-CN.md) | [日本語](README.ja.md)

A ready-to-use, battery-powered IR self-timer and remote for the **Lomo'Instant Wide Glass**, built around the **M5StickS3**.

## Hardware

<img src="docs/images/device-overview.jpeg" alt="M5StickS3 self-timer connected to the external IR sender module" width="720" />

The external **U002 IR sender** is optional. The M5StickS3 can send the trigger signal to the camera by itself, and adding the U002 boosts output strength and gives you more placement flexibility.

<img src="docs/images/ir-sender-labeled-v2.jpeg" alt="Labeled photo showing the external IR sender module" width="720" />

## IR Receiver Locations

The camera can receive the trigger from either the front or rear IR receiver.

<img src="docs/images/camera-front-receiver.png" alt="Front IR receiver location on the Lomo'Instant Wide Glass" width="360" />
<img src="docs/images/camera-back-receiver.png" alt="Rear IR receiver location on the Lomo'Instant Wide Glass" width="360" />

## Quick Start

1. Install [Arduino IDE 2.x](https://www.arduino.cc/en/software).
2. Install the `M5Stack` board package and select `M5StickS3`.
3. Install the `M5Unified` library in the Library Manager.
4. Open and upload `firmware/self_timer/self_timer.ino` to your device.
5. Point the StickS3 itself, or the optional U002 IR sender, at the camera's IR receiver (works best within ~40cm).
6. **Controls:**
   - **Front blue bar button (`Front Btn`):** Click to cycle timer/exposure duration. Hold to toggle between **Shot Mode** and **Bulb Mode**.
   - **Side large rectangle button (`Side Btn`):** Start/cancel countdown or close Bulb exposure.
   - **Small side button:** Power control only; it is not used by the self-timer UI.
   - The title line shows battery percentage; `+` means the StickS3 is charging.

## Bulb Mode Limitations

Due to the nature of analog instant film and the Lomo mechanical shutter, true micro-second precision isn't feasible or necessary in Bulb mode:

- **Instax Film Chemistry**: You are shooting on Instax Wide film (ISO 800). This is an analog, chemical medium. At multi-second exposures, a 0.1s difference (like 4.1s vs 4.2s) is completely invisible.
- **Reciprocity Failure**: Reciprocity failure means film loses sensitivity during long exposures. Once your exposure passes 1 or 2 seconds, you usually need to double the time just to get one more stop of light. Micro-adjustments do not matter here.
- **Mechanical Lag**: The Lomo'Instant Wide Glass uses a mechanical leaf shutter triggered by your Arduino. There is inherent mechanical latency. An Arduino firing a signal for exactly 2.1 seconds does not mean the shutter blades open and close in exactly 2.100 seconds.

## Experiments & Deep Dive

If you're curious about how the Lomo IR protocol was decoded, reverse-engineered, and validated, you can read more in the [docs/](docs/) directory. If you want to learn signals from other cameras, start with `firmware/ir_capture/ir_capture.ino` and capture their remote output there. We keep the main README short—this repo is ready to use out of the box!
