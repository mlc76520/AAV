import signal
import logging
import struct
import RPi.GPIO as GPIO
from time import sleep

# Configuration and Constants
from config.constants import (
    I2C_ADDRESS, I2C_BUS, GPIO_PINS, 
    SPECTRUM_HEADER, SLEEP_MODE_HEADER, 
    BARS_NUMBER, CHUNK_SIZE
)

# Hardware Components
from hardware.i2c import I2CVisualizer
from hardware.rotary_encoder import RotaryEncoder

# Media Sources
from media.mpd_client import MPDFetcher
from media.bluetooth_client import BluetoothFetcher

# Visualization
from visualization.cava import CavaProcess

class AudioVisualizerSystem:
    def __init__(self):
        # Control and state management
        self.running = True
        self.is_sleeping = 0
        self.was_sleeping = 0

        # Setup system components
        self._setup_gpio()
        self._setup_signal_handlers()
        self._initialize_components()

    def _setup_gpio(self):
        """
        Configure GPIO settings
        """
        GPIO.setmode(GPIO.BCM)
        # LED Setup
        GPIO.setup(GPIO_PINS["LED"], GPIO.OUT)
        pwm = GPIO.PWM(GPIO_PINS["LED"], 100)
        pwm.start(80)

    def _setup_signal_handlers(self):
        """
        Configure signal handlers for graceful shutdown
        """
        signal.signal(signal.SIGINT, self._signal_handler)
        signal.signal(signal.SIGTERM, self._signal_handler)

    def _initialize_components(self):
        """
        Initialize all system components
        """
        # I2C Visualizer
        self.visualizer = I2CVisualizer(I2C_ADDRESS, I2C_BUS)

        # Media Fetchers
        self.data_fetcher = MPDFetcher(self.visualizer)
        self.bluetooth_fetcher = BluetoothFetcher(self.visualizer)

        # Cava Spectrum Visualization
        self.cava = CavaProcess()

        # Rotary Encoders
        self._setup_rotary_encoders()

    def _setup_rotary_encoders(self):
        """
        Setup rotary encoders with their respective callbacks
        """
        try:
            self.encoder_1 = RotaryEncoder(
                clk_pin=GPIO_PINS["CLK1"],
                dt_pin=GPIO_PINS["DT1"],
                sw_pin=GPIO_PINS["SW1"],
                encoder_header=0x1D,  # ENCODER_1_HEADER
                switch_header=0x1F,   # SWITCH_1_HEADER
                i2c_visualizer=self.visualizer,
                callback=self._on_switch_1,
                rotation_callback=self._handle_encoder_1_rotation
            )

            self.encoder_2 = RotaryEncoder(
                clk_pin=GPIO_PINS["CLK2"],
                dt_pin=GPIO_PINS["DT2"],
                sw_pin=GPIO_PINS["SW2"],
                encoder_header=0x1E,  # ENCODER_2_HEADER
                switch_header=0x20,   # SWITCH_2_HEADER
                i2c_visualizer=self.visualizer,
                callback=self._on_switch_2
            )
        except Exception as e:
            logging.error(f"Encoder initialization error: {e}")
            self.cleanup()
            raise

    def _signal_handler(self, signum, frame):
        """
        Handle system shutdown signals
        """
        logging.info(f"Received signal {signum}. Initiating shutdown...")
        self.running = False

    def _handle_encoder_1_rotation(self, direction):
        """
        Handle rotation of first encoder (media controls)
        """
        try:
            if direction == "CW":
                self.data_fetcher.next_song()
            else:
                self.data_fetcher.previous_song()
        except Exception as e:
            logging.error(f"Encoder 1 rotation error: {e}")

    def _on_switch_1(self, channel):
        """
        Handle switch 1 press (play/pause toggle)
        """
        try:
            if GPIO.input(self.encoder_1.sw_pin) == 0:
                logging.info("Switch 1 pressed - toggling play/pause")
                self.data_fetcher.toggle_play_pause()
        except Exception as e:
            logging.error(f"Switch 1 handler error: {e}")

    def _on_switch_2(self, channel):
        """
        Handle switch 2 press (placeholder for additional functionality)
        """
        try:
            logging.info("Encoder 2 button pressed")
            # Add custom functionality as needed
        except Exception as e:
            logging.error(f"Switch 2 handler error: {e}")

    def _handle_sleep_mode(self):
        """
        Detect and manage sleep mode based on media playback state
        """
        try:
            # Combine states from both sources
            current_state_mpd = self.data_fetcher.current_data.get('state', '').lower()
            current_state_bt = self.bluetooth_fetcher.current_data.get('state', '').lower()

            # Active play states
            play_states = {'play', 'playing'}

            # Determine sleep mode - wake up if either source is playing
            self.is_sleeping = 0 if (current_state_mpd in play_states or current_state_bt in play_states) else 1

            if self.is_sleeping != self.was_sleeping:
                self.visualizer.send_data(SLEEP_MODE_HEADER, [self.is_sleeping])
                logging.info(f"Sleep mode: {'Activated' if self.is_sleeping else 'Deactivated'} " +
                             f"(MPD: {current_state_mpd}, BT: {current_state_bt})")

            self.was_sleeping = self.is_sleeping
        except Exception as e:
            logging.error(f"Sleep mode error: {e}")

    def cleanup(self):
        """
        Gracefully clean up all system resources
        """
        logging.info("Starting system cleanup...")
        
        try:
            # Cleanup encoders
            if hasattr(self, 'encoder_1'):
                self.encoder_1.cleanup()
            if hasattr(self, 'encoder_2'):
                self.encoder_2.cleanup()
        except Exception as e:
            logging.error(f"Encoder cleanup error: {e}")

        try:
            # Cleanup media fetchers
            self.data_fetcher.cleanup()
            self.bluetooth_fetcher.cleanup()
        except Exception as e:
            logging.error(f"Media fetcher cleanup error: {e}")

        try:
            # Terminate Cava process
            if self.cava.process:
                self.cava.process.terminate()
        except Exception as e:
            logging.error(f"Cava process cleanup error: {e}")

        try:
            # GPIO cleanup
            GPIO.cleanup()
        except Exception as e:
            logging.error(f"GPIO cleanup error: {e}")

        logging.info("System cleanup complete")

    def run(self):
        """
        Main system execution loop
        """
        try:
            # Start background processes
            self.cava.start()
            self.data_fetcher.start()
            self.bluetooth_fetcher.start()
            
            logging.info("Audio visualization system started")

            while self.running:
                try:
                    # Read Cava spectrum output
                    c_output = self.cava.read_output(CHUNK_SIZE)
                    if c_output:
                        # Process and send spectrum data
                        sample = list(struct.unpack(f"{'B' * BARS_NUMBER}", c_output))
                        self.visualizer.send_data(SPECTRUM_HEADER, sample[:BARS_NUMBER])

                        # Handle sleep mode detection
                        self._handle_sleep_mode()

                except Exception as e:
                    logging.error(f"Main loop processing error: {e}")

                # Small delay to prevent CPU overload
                sleep(0.01)

        except Exception as e:
            logging.error(f"Fatal error in main loop: {e}")
        finally:
            self.cleanup()

def main():
    """
    Application entry point
    """
    try:
        system = AudioVisualizerSystem()
        system.run()
    except Exception as e:
        logging.critical(f"Unhandled application error: {e}")
        GPIO.cleanup()  # Emergency cleanup

if __name__ == "__main__":
    main()