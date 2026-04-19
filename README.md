# Lomo C/R: Lomo'Instant Wide Glass Remote & Self-Timer

[English](README.md) | [简体中文](README.zh-CN.md) | [日本語](README.ja.md)

A ready-to-use, battery-powered IR self-timer and remote for the **Lomo'Instant Wide Glass**, built around the **M5StickS3**.

## Quick Start

1. Install [Arduino IDE 2.x](https://www.arduino.cc/en/software).
2. Install the `M5Stack` board package and select `M5StickS3`.
3. Install the `M5Unified` library in the Library Manager.
4. Open and upload `firmware/self_timer/self_timer.ino` to your device.
5. Point the StickS3 at the camera's front IR receiver (works best within ~40cm).
6. **Controls:**
   - **BtnB (Front/M5):** Start/cancel countdown.
   - **BtnA (Side):** Click to cycle timer/exposure duration. Hold to toggle between **Shot Mode** and **Bulb Mode**.

## Bulb Mode Limitations

Due to the nature of analog instant film and the Lomo mechanical shutter, true micro-second precision isn't feasible or necessary in Bulb mode:

- **Instax Film Chemistry**: You are shooting on Instax Wide film (ISO 800). This is an analog, chemical medium. At multi-second exposures, a 0.1s difference (like 4.1s vs 4.2s) is completely invisible.
- **Reciprocity Failure**: Reciprocity failure means film loses sensitivity during long exposures. Once your exposure passes 1 or 2 seconds, you usually need to double the time just to get one more stop of light. Micro-adjustments do not matter here.
- **Mechanical Lag**: The Lomo'Instant Wide Glass uses a mechanical leaf shutter triggered by your Arduino. There is inherent mechanical latency. An Arduino firing a signal for exactly 2.1 seconds does not mean the shutter blades open and close in exactly 2.100 seconds.

## Experiments & Deep Dive

If you're curious about how the Lomo IR protocol was decoded, reverse-engineered, and validated, you can read more in the [docs/](docs/) directory. We keep the main README short—this repo is ready to use out of the box!
