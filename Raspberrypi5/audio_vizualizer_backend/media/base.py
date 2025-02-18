# media/base.py

from threading import Thread, Lock
from typing import Dict, Any, Protocol, Optional, Union

import logging
import time
from utils.formatters import clean_text

class AbstractMediaFetcher(Thread):
    def __init__(self, i2c_visualizer):
        super().__init__(daemon=True)
        self.visualizer = i2c_visualizer
        self.current_data: Dict[str, Any] = {}
        self.lock = Lock()
        self.connected = False
        self.last_sent_values: Dict[int, str] = {}
        self.setup_headers()

    def setup_headers(self):
        self.headers = {
            'title': 0x21,
            'artist': 0x22,
            'album': 0x23,
            'track': 0x24,
            'audio': 0x25,
            'bitrate': 0x26,
            'outrate': 0x27,
            'state': 0x28,
            'elapsed': 0x29,
            'duration': 0x2A,
            'volume': 0x2B,
            'repeat': 0x2C,
            'random': 0x2D,
            'single': 0x2E,
            'consume': 0x2F,
            'playlist': 0x30,
            'playlistlength': 0x31,

        }

    def clean_text(self, text):
        if not isinstance(text, str):
            text = str(text)
        return html.unescape(text).strip()

    def format_audio_info(self, audio_str):
        if not audio_str:
            return "No Format"
        try:
            rate, bits, channels = audio_str.split(':')
            rate = float(rate) / 1000
            return f"{rate}kHz/{bits}bit"
        except:
            return audio_str

    def format_time(self, seconds: Union[str, int, float]) -> str:
        try:
            total_seconds = int(float(seconds))
            hours = total_seconds // 3600
            minutes = (total_seconds % 3600) // 60
            seconds = total_seconds % 60

            if hours > 0:
                return f"{hours:02d}:{minutes:02d}:{seconds:02d}"
            return f"{minutes:02d}:{seconds:02d}"
        except:
            return "00:00"

    def send_field(self, header: int, value: Any, max_length: int = 30) -> None:
        try:
            if value is None:
                return

            cleaned_value = clean_text(str(value))
            truncated_value = cleaned_value[:max_length]

            if header not in self.last_sent_values or \
               self.last_sent_values[header] != truncated_value:
                formatted_data = [len(truncated_value)] + \
                               list(truncated_value.encode('utf-8'))
                self.visualizer.send_data(header, formatted_data)
                logging.info(f"I2C Update - Header: 0x{header:02X}, " + 
                           f"Value: {truncated_value}")
                self.last_sent_values[header] = truncated_value
        except Exception as e:
            logging.error(f"Error sending field {header}: {e}")

    def connect(self):
        """To be implemented by subclasses"""
        raise NotImplementedError

    def fetch_metadata(self):
        """To be implemented by subclasses"""
        raise NotImplementedError

    def run(self):
        last_metadata = {}
        while True:
            try:
                if not self.connected:
                    self.connect()
                    time.sleep(1)
                    continue

                metadata = self.fetch_metadata()
                if not metadata:
                    time.sleep(0.5)
                    continue

                self.current_data = metadata

                for key, header in self.headers.items():
                    if metadata.get(key) != last_metadata.get(key):
                        self.send_field(header, metadata.get(key))

                last_metadata = metadata.copy()

            except Exception as e:
                logging.error(f"{self.__class__.__name__} error: {e}")
                self.connected = False

            time.sleep(0.5)