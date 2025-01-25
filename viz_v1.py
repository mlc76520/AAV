import subprocess
from subprocess import PIPE
import struct
import time
import smbus
import logging
import RPi.GPIO as GPIO
from time import sleep
import html
from mpd import MPDClient
from threading import Thread, Lock
from typing import Protocol, Dict, Any, Optional, Union
import signal

# Logging configuration
logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')

# Constants and configurations
I2C_ADDRESS = 0x13
SPECTRUM_HEADER = 0x1A
SLEEP_MODE_HEADER = 0x1C
ENCODER_1_HEADER = 0x1D
ENCODER_2_HEADER = 0x1E
SWITCH_1_HEADER = 0x1F
SWITCH_2_HEADER = 0x20
SONG_INFO_HEADER = 0x21
FORMAT_INFO_HEADER = 0x22
BARS_NUMBER = 14
CHUNK_SIZE = BARS_NUMBER
I2C_BUS = 1

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

class MPDClientProtocol(Protocol):
    def connect(self, host: str, port: int) -> None: ...
    def close(self) -> None: ...
    def disconnect(self) -> None: ...
    def status(self) -> Dict[str, Any]: ...
    def currentsong(self) -> Dict[str, Any]: ...
    def next(self) -> None: ...
    def previous(self) -> None: ...
    timeout: int

class DataFetcher(Thread):
    def __init__(self, i2c_visualizer: Any) -> None:
        super().__init__(daemon=True)
        self.client: MPDClientProtocol = MPDClient()
        self.visualizer = i2c_visualizer
        self.current_data: Dict[str, Any] = {}
        self.lock: Lock = Lock()
        self.connected: bool = False
        self.last_sent_values: Dict[int, str] = {}

        self.headers: Dict[str, int] = {
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

    def next_song(self):
        """Play next song in playlist"""
        try:
            with self.lock:
                if self.connected:
                    self.client.next()
                    logging.info("Playing next song")
        except Exception as e:
            logging.error(f"Error playing next song: {e}")

    def previous_song(self):
        """Play previous song in playlist"""
        try:
            with self.lock:
                if self.connected:
                    self.client.previous()
                    logging.info("Playing previous song")
        except Exception as e:
            logging.error(f"Error playing previous song: {e}")

    def play(self):
        """Start playback"""
        try:
            with self.lock:
                if self.connected:
                    self.client.play()
                    logging.info("Playback started")
        except Exception as e:
            logging.error(f"Error starting playback: {e}")

    def pause(self):
        """Pause playback"""
        try:
            with self.lock:
                if self.connected:
                    self.client.pause()
                    logging.info("Playback paused")
        except Exception as e:
            logging.error(f"Error pausing playback: {e}")

    def toggle_play_pause(self):
        """Toggle between play and pause states"""
        try:
            with self.lock:
                if self.connected:
                    status = self.client.status()
                    current_state = status.get('state', '')
                    logging.info(f"Current state before toggle: {current_state}")

                    if current_state == 'play':
                        self.client.pause(1)  # 1 means pause
                        logging.info("Playback paused")
                    elif current_state in ['pause', 'stop']:
                        self.client.pause(0)  # 0 means play
                        logging.info("Playback started")
        except Exception as e:
            logging.error(f"Error toggling play/pause: {e}")

    def clean_text(self, text):
        if not isinstance(text, str):
            text = str(text)
        return html.unescape(text)

    def connect(self):
        try:
            self.client.connect("localhost", 6600)
            self.client.timeout = 10
            self.connected = True
            logging.info("Connected to MPD server")
        except Exception as e:
            self.connected = False
            logging.error(f"MPD connection error: {e}")

    def format_audio_info(self, audio_str):
        if not audio_str:
            return "No Format"
        try:
            rate, bits, channels = audio_str.split(':')
            rate = float(rate) / 1000
            return f"{rate}kHz/{bits}bit"
        except:
            return audio_str

    def format_time(self, seconds: str) -> str:
        try:
            total_seconds = int(float(seconds))
            hours = total_seconds // 3600
            minutes = (total_seconds % 3600) // 60
            seconds = total_seconds % 60

            if hours > 0:
                return f"{hours:02d}:{minutes:02d}:{seconds:02d}"
            else:
                return f"{minutes:02d}:{seconds:02d}"
        except:
            return "00:00"

    def send_field(self, header, value, max_length=30):
        try:
            cleaned_value = self.clean_text(value)
            truncated_value = cleaned_value[:max_length]

            if header not in self.last_sent_values or self.last_sent_values[header] != truncated_value:
                formatted_data = [len(truncated_value)] + list(truncated_value.encode('utf-8'))
                self.visualizer.send_data(header, formatted_data)
                logging.info(f"I2C Update - Header: 0x{header:02X}, New: {truncated_value}, Old: {self.last_sent_values.get(header, 'None')}")
                self.last_sent_values[header] = truncated_value
            else:
                logging.debug(f"Skip I2C - Header: 0x{header:02X}, Unchanged: {truncated_value}")
        except Exception as e:
            logging.error(f"Error sending field {header}: {e}")

    def run(self):
        last_status = {}
        last_song = {}

        while True:
            try:
                if not self.connected:
                    self.connect()
                    time.sleep(1)
                    continue

                with self.lock:
                    status = self.client.status()
                    current = self.client.currentsong()

                    self.current_data = {**status, **current}

                    song_changed = current != last_song
                    status_changed = status != last_status

                    if song_changed:
                        logging.info("Song change detected:")
                        if current.get('title') != last_song.get('title'):
                            self.send_field(self.headers['title'], current.get('title', 'No Title'))
                        if current.get('artist') != last_song.get('artist'):
                            self.send_field(self.headers['artist'], current.get('artist', 'No Artist'))
                        if current.get('album') != last_song.get('album'):
                            self.send_field(self.headers['album'], current.get('album', 'No Album'))
                        if current.get('track') != last_song.get('track'):
                            self.send_field(self.headers['track'], current.get('track', '0'))
                        last_song = current.copy()

                    if status_changed:
                        logging.info("Status change detected:")
                        if status.get('audio') != last_status.get('audio'):
                            formatted_audio = self.format_audio_info(status.get('audio'))
                            self.send_field(self.headers['audio'], formatted_audio)

                        if status.get('bitrate') != last_status.get('bitrate'):
                            self.send_field(self.headers['bitrate'], f"{status.get('bitrate', '0')}kbps")

                        if status.get('state') != last_status.get('state'):
                            self.send_field(self.headers['state'], status.get('state', 'unknown'))

                        if status.get('elapsed') != last_status.get('elapsed'):
                            formatted_time = self.format_time(status.get('elapsed', '0'))
                            self.send_field(self.headers['elapsed'], formatted_time)

                        if status.get('duration') != last_status.get('duration'):
                            formatted_time = self.format_time(status.get('duration', '0'))
                            self.send_field(self.headers['duration'], formatted_time)

                        if status.get('volume') != last_status.get('volume'):
                            self.send_field(self.headers['volume'], status.get('volume', '0'))

                        for field in ['repeat', 'random', 'single', 'consume', 'playlist', 'playlistlength']:
                            if status.get(field) != last_status.get(field):
                                self.send_field(self.headers[field], status.get(field, '0'))

                        last_status = status.copy()

            except Exception as e:
                logging.error(f"MPD error: {e}")
                self.connected = False
                time.sleep(1)

            time.sleep(0.1)

    def cleanup(self):
        try:
            if self.connected:
                self.client.close()
                self.client.disconnect()
        except:
            pass

class RotaryEncoder:
    def __init__(self, clk_pin, dt_pin, sw_pin, encoder_header, switch_header, i2c, callback=None, rotation_callback=None):
        self.clk_pin = clk_pin
        self.dt_pin = dt_pin
        self.sw_pin = sw_pin
        self.callback = callback
        self.rotation_callback = rotation_callback
        self.i2c = i2c
        self.encoder_header = encoder_header
        self.switch_header = switch_header
        self.value = 0
        self.internal_counter = 0

        # State variables
        self.last_state = None
        self.current_state = None

        # GPIO setup
        GPIO.setup(self.clk_pin, GPIO.IN, pull_up_down=GPIO.PUD_UP)
        GPIO.setup(self.dt_pin, GPIO.IN, pull_up_down=GPIO.PUD_UP)
        GPIO.setup(self.sw_pin, GPIO.IN, pull_up_down=GPIO.PUD_UP)

        # Initialize state
        self.last_state = self._get_state()

        # Setup interrupts for both pins
        GPIO.add_event_detect(self.clk_pin, GPIO.BOTH, callback=self._encoder_callback, bouncetime=1)
        GPIO.add_event_detect(self.dt_pin, GPIO.BOTH, callback=self._encoder_callback, bouncetime=1)
        GPIO.add_event_detect(self.sw_pin, GPIO.BOTH, callback=self.switch_state_changed, bouncetime=300)

    def _get_state(self):
        return (GPIO.input(self.clk_pin), GPIO.input(self.dt_pin))

    def _encoder_callback(self, channel):
        self.current_state = self._get_state()

        if self.current_state != self.last_state:
            # State transition table
            if self.last_state == (1, 1):
                if self.current_state == (0, 1):
                    self._increment()
                elif self.current_state == (1, 0):
                    self._decrement()
            elif self.last_state == (0, 1):
                if self.current_state == (0, 0):
                    self._increment()
                elif self.current_state == (1, 1):
                    self._decrement()
            elif self.last_state == (0, 0):
                if self.current_state == (1, 0):
                    self._increment()
                elif self.current_state == (0, 1):
                    self._decrement()
            elif self.last_state == (1, 0):
                if self.current_state == (1, 1):
                    self._increment()
                elif self.current_state == (0, 0):
                    self._decrement()

            self.last_state = self.current_state

    def _increment(self):
        self.internal_counter = (self.internal_counter + 1) % 5
        if self.internal_counter == 0:
            self.value = (self.value + 1) % 5
            if self.i2c:
                self.i2c.send_data(self.encoder_header, [self.value])
            if self.rotation_callback:
                self.rotation_callback("CW")
            logging.info(f"Encoder {self.encoder_header}: CW Value: {self.value}")

    def _decrement(self):
        self.internal_counter = (self.internal_counter - 1) % 5
        if self.internal_counter == 0:
            self.value = (self.value - 1) % 5
            if self.i2c:
                self.i2c.send_data(self.encoder_header, [self.value])
            if self.rotation_callback:
                self.rotation_callback("CCW")
            logging.info(f"Encoder {self.encoder_header}: CCW Value: {self.value}")

    def switch_state_changed(self, channel):
        state = GPIO.input(self.sw_pin)
        if self.i2c:
            self.i2c.send_data(self.switch_header, [0 if state == 1 else 1])
        if self.callback:
            self.callback(channel)
        logging.info(f"Switch {self.switch_header}: {'Released' if state else 'Pressed'}")

    def cleanup(self):
        GPIO.remove_event_detect(self.clk_pin)
        GPIO.remove_event_detect(self.dt_pin)
        GPIO.remove_event_detect(self.sw_pin)

class CavaProcess:
    def __init__(self):
        self.process = None

    def start(self):
        try:
            self.process = subprocess.Popen(["cava", "-p", "/etc/cava.conf"], stdout=PIPE)
            logging.info("Cava process started successfully")
        except FileNotFoundError:
            logging.error("Cava not found. Please install cava package.")
            raise

    def read_output(self, chunk_size):
        if self.process:
            return self.process.stdout.read(chunk_size)
        return None

class SpectrumVisualizer:
    def __init__(self, i2c_address, bus_number):
        self.i2c_bus = smbus.SMBus(bus_number)
        self.i2c_address = i2c_address
        self.bus_number = bus_number

    def send_data(self, header, data):
        try:
            self.i2c_bus.write_i2c_block_data(self.i2c_address, header, data)
            logging.debug(f"I2C data sent - Header: 0x{header:02X}, Data: {data}")
        except OSError as e:
            logging.error(f"I2C communication error: {e}")
            try:
                self.i2c_bus = smbus.SMBus(self.bus_number)
                self.i2c_bus.write_i2c_block_data(self.i2c_address, header, data)
            except Exception as retry_error:
                logging.error(f"I2C recovery failed: {retry_error}")
        except Exception as e:
            logging.error(f"Unexpected I2C error: {e}")

class MainProgram:
    def __init__(self):
        # Control flag for graceful shutdown
        self.running = True

        # Setup GPIO
        GPIO.setmode(GPIO.BCM)

        # Setup signal handlers for graceful shutdown
        self.setup_signal_handlers()

        # Initialize I2C visualizer
        self.visualizer = SpectrumVisualizer(I2C_ADDRESS, I2C_BUS)
        self.data_fetcher = DataFetcher(self.visualizer)

        # Initialize Cava process
        self.cava = CavaProcess()

        # LED light Up
        LEDPIN = GPIO_PINS["LED"]
        GPIO.setup(LEDPIN, GPIO.OUT)
        GPIO.output(LEDPIN, GPIO.HIGH)

        # Initialize rotary encoders with error handling
        try:
            self.encoder_1 = RotaryEncoder(
                GPIO_PINS["CLK1"],
                GPIO_PINS["DT1"],
                GPIO_PINS["SW1"],
                ENCODER_1_HEADER,
                SWITCH_1_HEADER,
                self.visualizer,
                self.on_switch_1,
                rotation_callback=self.handle_encoder_1_rotation
            )

            self.encoder_2 = RotaryEncoder(
                GPIO_PINS["CLK2"],
                GPIO_PINS["DT2"],
                GPIO_PINS["SW2"],
                ENCODER_2_HEADER,
                SWITCH_2_HEADER,
                self.visualizer,
                self.on_switch_2
            )
        except Exception as e:
            logging.error(f"Error initializing encoders: {e}")
            self.cleanup()
            raise

        # Initialize state variables
        self.prev_sample = [0] * BARS_NUMBER
        self.is_sleeping = 0
        self.was_sleeping = 0

    def setup_signal_handlers(self):
        """Setup handlers for graceful shutdown"""
        signal.signal(signal.SIGINT, self.signal_handler)
        signal.signal(signal.SIGTERM, self.signal_handler)

    def signal_handler(self, signum, frame):
        """Handle shutdown signals"""
        logging.info(f"Received signal {signum}. Initiating shutdown...")
        self.running = False

    def handle_encoder_1_rotation(self, direction):
        """Handle rotation of encoder 1 for previous/next song"""
        try:
            if direction == "CW":  # Clockwise
                self.data_fetcher.next_song()
            else:  # Counter-clockwise
                self.data_fetcher.previous_song()
        except Exception as e:
            logging.error(f"Error handling encoder 1 rotation: {e}")

    def on_switch_1(self, channel):
        """Handler for encoder 1 button press - Toggle Play/Pause"""
        try:
            if GPIO.input(self.encoder_1.sw_pin) == 0:  # Button pressed (active low)
                logging.info("Switch 1 pressed - toggling play/pause")
                self.data_fetcher.toggle_play_pause()
        except Exception as e:
            logging.error(f"Error in switch 1 handler: {e}")

    def on_switch_2(self, channel):
        """Handler for encoder 2 button press"""
        try:
            logging.info("Encoder 2 button pressed")
            # Add your button 2 logic here
        except Exception as e:
            logging.error(f"Error in switch 2 handler: {e}")

    def handle_sleep_mode(self):
        """Handle sleep mode state changes"""
        try:
            current_state = self.data_fetcher.current_data.get('state', '').lower()
            self.is_sleeping = 1 if current_state in ['stop', 'pause'] else 0

            if self.is_sleeping != self.was_sleeping:
                self.visualizer.send_data(SLEEP_MODE_HEADER, [self.is_sleeping])
                logging.info(f"Sleep mode: {'Activated' if self.is_sleeping else 'Deactivated'} (State: {current_state})")
            self.was_sleeping = self.is_sleeping
        except Exception as e:
            logging.error(f"Sleep mode error: {e}")

    def cleanup(self):
        """Cleanup all resources"""
        logging.info("Starting cleanup...")

        try:
            if hasattr(self, 'encoder_1'):
                self.encoder_1.cleanup()
            if hasattr(self, 'encoder_2'):
                self.encoder_2.cleanup()
        except Exception as e:
            logging.error(f"Error cleaning up encoders: {e}")

        try:
            if hasattr(self, 'data_fetcher'):
                self.data_fetcher.cleanup()
        except Exception as e:
            logging.error(f"Error cleaning up data fetcher: {e}")

        try:
            if hasattr(self, 'cava') and self.cava.process:
                self.cava.process.terminate()
        except Exception as e:
            logging.error(f"Error cleaning up Cava process: {e}")

        try:
            GPIO.cleanup()
        except Exception as e:
            logging.error(f"Error in GPIO cleanup: {e}")

        logging.info("Cleanup complete")

    def run(self):
        """Main program loop"""
        try:
            # Start processes
            self.cava.start()
            self.data_fetcher.start()
            logging.info("Audio visualization system started")

            # Main loop
            while self.running:
                try:
                    # Read Cava output
                    c_output = self.cava.read_output(CHUNK_SIZE)
                    if c_output:
                        # Process audio data
                        sample = list(struct.unpack(f"{'B' * BARS_NUMBER}", c_output))
                        self.visualizer.send_data(SPECTRUM_HEADER, sample[:BARS_NUMBER])

                        # Handle sleep mode
                        self.handle_sleep_mode()

                except Exception as e:
                    logging.error(f"Error in main loop: {e}")
                    continue

                # Small delay to prevent CPU overload
                sleep(0.01)

        except Exception as e:
            logging.error(f"Fatal error in main loop: {e}")
        finally:
            self.cleanup()

if __name__ == "__main__":
    try:
        program = MainProgram()
        program.run()
    except Exception as e:
        logging.critical(f"Fatal error: {e}")
        GPIO.cleanup()  # Emergency cleanup
