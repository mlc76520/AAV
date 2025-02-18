# config/constants.py

# Project Structure:
'''
audio_visualizer/
├── config/
│   └── constants.py
├── hardware/
│   ├── __init__.py
│   ├── i2c.py
│   ├── gpio_manager.py
│   └── rotary_encoder.py
├── media/
│   ├── __init__.py
│   ├── base.py
│   ├── mpd_client.py
│   └── bluetooth_client.py
├── visualization/
│   ├── __init__.py
│   ├── cava.py
│   └── spectrum.py
├── utils/
│   ├── __init__.py
│   └── formatters.py
└── main.py
'''

# config/constants.py
import logging

# Visualization Constants
BARS_NUMBER = 14
CHUNK_SIZE = BARS_NUMBER

# GPIO Pin Configuration
GPIO_PINS = {
    "CLK1": 17,
    "DT1": 22,
    "SW1": 27,
    "CLK2": 23,
    "DT2": 24,
    "SW2": 26,
    "LED": 16,
    "SW3": 12,
}

# Logging Configuration
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(levelname)s - %(message)s'
)