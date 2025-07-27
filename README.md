# (R)AAV
(Raspberry) Audio Vizualizer

This is a music visualization system that displays audio spectrum analyzers, VU meters, and music player information on two OLED displays (128x64 pixels each) connected to a Raspberry Pi.
Key Components
Custom-built multimedia box powered by a Raspberry Pi 3B running Raspbian Bookworm, MPD, and ALSA or Pipewire. 

Featuring:
Network access to your music repository and display songs informations on the OLED screens.
Bluetooth Audio: Stream music wirelessly to your multimedia box and display informations on the OLED screens.
2 Rotary Encoders: Navigate your playlist and switch visualizers effortlessly.
Arduino Nano Integration: Handles two 128x64 OLED screens with a full U8g2 implementation, thanks to a custom Python script communicating over I2C.
3D Printed Case: Inspired by the sleek design of the Yamaha AS701 amplifier.

1. Display System

Uses two SSD1309 OLED displays via SPI
Left and right displays can show different visualizations
Supports display power management (on/off)

2. Visualizations

Bar Visualization: Classic spectrum analyzer with frequency bars
Empty Bar Visualization: Outline-only version of the bars
VU Meter: Analog-style volume unit meter with needle animation
Player Info: Shows song metadata and playback status

3. Audio Processing

Uses internal FFT to process audio
Captures audio from ALSA and converts to frequency data
Different configurations for different visualization types

4. Music Players

MPD (Music Player Daemon): Network music player support
Bluetooth: A2DP Bluetooth audio device support
Automatically switches between players based on connection status

5. Control Systems

Rotary Encoder Controller: Hardware rotary encoders and buttons for:

Switching visualizations
Changing physics modes (for VU meter)
Power control

7. Additional Features

LED power indicator
Text scrolling for long song titles/artist names
Automatic power management (turns off when no sound detected)
GPIO resource management to prevent conflicts

Key Functions
The main program flow:

Initializes hardware (displays, GPIO, LED)
Starts the selected controller (rotary or console)
Monitors music players and switches automatically
Reads audio data from CAVA
Applies physics calculations if needed
Renders visualizations on both displays
Handles player metadata and status updates

Hardware Requirements

Raspberry Pi with GPIO
2x SSD1309 OLED displays (128x64)
Optional: 2x rotary encoders with push buttons
Optional: Power button and LED
Audio input (ALSA device named "cava")

![20250723_213114](https://github.com/user-attachments/assets/7b956db7-38ed-444a-9afb-34b1cc27b2f7)
![20250723_213018](https://github.com/user-attachments/assets/942ac27d-6711-48e9-93c5-a4e3bffb03a8)

Youtube: https://youtu.be/BoUWVKt8RYw

<img width="1058" height="595" alt="AAC" src="https://github.com/user-attachments/assets/d2f882d4-fa7f-4727-b433-ace2f22a3c1c" />

Parts list:
- 1 Raspberry pi 3B
- 2 Rotary encoders
- 2 128x64 waveshare SPI OLED screens
- 1 5v power supply
- 1 pushbutton
- 1 LED


Using raspian bookworm x64 or using specialized multimedia distro like moode player https://moodeaudio.org/
Copy audio_vizualizer_backend to your raspberry pi and run main.py with python3


How to install Starting from a specialized distro like moode audio player (Easy way)

From the moode web interface:
- Navigate to Configure > Audio > Activate loopback. This loopback interface is required to read audio stream and create spectrum
- connect your Moode instance using ssh:
- git clone this repository 
