# (R)AAV
(Raspberry) Arduino Audio Vizualizer

This is a music visualization system that displays audio spectrum analyzers, VU meters, and music player information on two OLED displays (128x64 pixels each) connected to a Raspberry Pi.
Key Components
Custom-built multimedia box powered by a Raspberry Pi 3B running Raspbian Bookworm, MPD, CAVA, and Pipewire. 

Featuring:
Network access to your music repository and display songs informations on the OLED screens.
Bluetooth Audio: Stream music wirelessly to your multimedia box and display informations on the OLED screens.
2 Rotary Encoders: Navigate your playlist and switch visualizers effortlessly.
Arduino Nano Integration: Handles two 128x64 OLED screens with a full U8g2 implementation, thanks to a custom Python script communicating over I2C.
3D Printed Case: Inspired by the sleek design of the Yamaha AS701 amplifier.

1. Display System

Uses two SSD1309 OLED displays connected via SPI
Left and right displays can show different visualizations
Supports display power management (on/off)

2. Visualizations

Bar Visualization: Classic spectrum analyzer with frequency bars
Empty Bar Visualization: Outline-only version of the bars
VU Meter: Analog-style volume unit meter with needle animation
Player Info: Shows song metadata and playback status

3. Audio Processing

Uses CAVA (Console-based Audio Visualizer for ALSA) to process audio
Captures audio from ALSA and converts to frequency data
Different CAVA configurations for different visualization types

4. Music Players

MPD (Music Player Daemon): Network music player support
Bluetooth: A2DP Bluetooth audio device support
Automatically switches between players based on connection status

5. Control Systems

Console Controller: Keyboard-based control for debugging
Rotary Encoder Controller: Hardware rotary encoders and buttons for:

Switching visualizations
Changing physics modes (for VU meter)
Power control

6. Physics Engine

PID controller implementation for VU meter needle physics
Three modes: Direct (no physics), Underdamped, Overdamped

7. Additional Features

LED power indicator
Text scrolling for long song titles/artist names
Automatic display power management (turns off when paused/stopped)
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

![(r)AAV](https://github.com/user-attachments/assets/2dfd8dc2-c54a-46a2-a781-7adb17d37767)

Youtube: https://youtu.be/BoUWVKt8RYw

![AAC](https://github.com/user-attachments/assets/ff4b844f-b73c-4f5e-b41c-9dc6e054ddd6)

Parts list:
- 1 Raspberry pi 3B
- 1 Arduino nano ESP32
- 2 Rotary encoders
- 2 128x64 waveshare SPI OLED screens
- 1 5v power supply
- 1 pushbutton
- 1 LED


Using raspian bookworm x64 or using specialized multimedia distro like moode player https://moodeaudio.org/
Copy audio_vizualizer_backend to your raspberry pi and run main.py with python3
Upload vumeter_spectrum_U8g2.ino to your arduino nano (esp32 preffered but should work with nano33iot aswell)

How to install Starting from a specialized distro like moode audio player (Easy way)

From the moode web interface:
- Navigate to Configure > Audio > Activate loopback. This loopback interface is required for cava to read audio stream and create spectrum
- connect your Moode instance using ssh:
- Install latest cava version either by cloning their repo or downloading latest release and compile
- You could use cava version in the repo using apt but it is an older version that is a bit tricky to configure properly work with the script and get a good behaviour
- OPTIONAL: At this stage you can test with music sample that cava runs well with graphical configuration
- git clone this repository
- upload the arduino sketch to your board (vumeter_spectrum_U8g2)
- 
