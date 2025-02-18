# (R)AAV
(Raspberry) Arduino Audio Vizualizer

Custom-built multimedia box powered by a Raspberry Pi 3B running Raspbian Bookworm, MPD, CAVA, and Pipewire. 

Featuring:
Network access to your music repository and display songs informations on the OLED screens.
Bluetooth Audio: Stream music wirelessly to your multimedia box and display informations on the OLED screens.
2 Rotary Encoders: Navigate your playlist and switch visualizers effortlessly.
Arduino Nano Integration: Handles two 128x64 OLED screens with a full U8g2 implementation, thanks to a custom Python script communicating over I2C.
3D Printed Case: Inspired by the sleek design of the Yamaha AS701 amplifier.

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
