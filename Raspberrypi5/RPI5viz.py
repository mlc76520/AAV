from luma.core.interface.serial import i2c, spi
from luma.core.render import canvas
from luma.oled.device import ssd1309
from PIL import Image, ImageDraw, ImageFont
import time
import smbus2 as smbus
import math
from collections import deque
import os
import RPi.GPIO as GPIO

class AudioVisualizer:
    def __init__(self):
        # Set up GPIO mode
        GPIO.setmode(GPIO.BCM)
        
        # Pin definitions for RPi 5
        self.CS1_PIN = 8    # Left Display CS (GPIO8, physical pin 24)
        self.DC1_PIN = 25   # Left Display DC
        self.RST1_PIN = 24  # Left Display Reset
        self.CS2_PIN = 7    # Right Display CS (GPIO7, physical pin 26)
        self.DC2_PIN = 23   # Right Display DC
        self.RST2_PIN = 18  # Right Display Reset
        
        # Initialize SPI interface for both displays
        # RPi 5 has two hardware SPI buses: SPI0 and SPI1
        self.serial_left = spi(port=0, device=0,  # SPI0
                             gpio_DC=self.DC1_PIN,
                             gpio_RST=self.RST1_PIN,
                             gpio_CS=self.CS1_PIN)
        
        self.serial_right = spi(port=1, device=0,  # SPI1
                              gpio_DC=self.DC2_PIN,
                              gpio_RST=self.RST2_PIN,
                              gpio_CS=self.CS2_PIN)
        
        # Initialize displays
        self.display_left = ssd1309(self.serial_left)
        self.display_right = ssd1309(self.serial_right)
        
        # Set initial rotation (0 degrees)
        self.display_left.rotate(0)
        self.display_right.rotate(0)
        
        # Constants
        self.SPECTRUM_SIZE = 7
        self.BUFFER_SIZE = 32
        self.I2C_ADDR = 0x13
        self.I2C_BUS = 1  # RPi 5 default I2C bus
        
        # Initialize I2C with SMBus 2
        try:
            self.bus = smbus.SMBus(self.I2C_BUS)
        except FileNotFoundError:
            print(f"Error: I2C bus {self.I2C_BUS} not found")
            print("Please ensure I2C is enabled in raspi-config")
            raise
        
        # Audio visualization variables
        self.levels_left = [0] * self.SPECTRUM_SIZE
        self.levels_right = [0] * self.SPECTRUM_SIZE
        self.audio_bar_height_left = [0] * self.SPECTRUM_SIZE
        self.audio_bar_height_right = [0] * self.SPECTRUM_SIZE
        self.audio_bar_peak_left = [0] * self.SPECTRUM_SIZE
        self.audio_bar_peak_right = [0] * self.SPECTRUM_SIZE
        
        # VU meter variables
        self.pos0 = 0
        self.pos1 = 0
        self.err_accum0 = 0
        self.err_accum1 = 0
        
        # Physics parameters
        self.p_gain = 0.2
        self.i_gain = 0.8
        self.phys_mode = 1  # 0=none, 1=underdamped, 2=overdamped
        
        # Display state
        self.sleep_mode = False
        self.brightness = 255
        self.current_screen = 0
        self.spectrum_type = 0
        
        # Song info
        self.song_info = {
            'title': '',
            'artist': '',
            'album': '',
            'track': '',
            'state': '',
            'elapsed': '',
            'duration': '',
            'encoded': '',
            'bitrate': ''
        }
        
        # Try to load system fonts, fall back to default if not found
        try:
            self.font = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf', 10)
            self.font_large = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf', 14)
        except OSError:
            print("Warning: DejaVu fonts not found, using default font")
            self.font = ImageFont.load_default()
            self.font_large = ImageFont.load_default()

    def cleanup(self):
        """Clean up GPIO and display resources"""
        self.display_left.cleanup()
        self.display_right.cleanup()
        GPIO.cleanup()

    def process_i2c_data(self, data):
        """Process incoming I2C data based on command header"""
        try:
            cmd = data[0]
            if cmd == 0x1A:  # CMD_SPECTRUM
                self.handle_spectrum(data[1:])
            elif cmd == 0x1C:  # CMD_SLEEP_MODE
                self.sleep_mode = bool(data[1])
            # Add other command handlers as needed
        except Exception as e:
            print(f"Error processing I2C data: {e}")
            
    def handle_spectrum(self, data):
        """Process spectrum data for both channels"""
        try:
            for i in range(self.SPECTRUM_SIZE):
                self.levels_left[i] = data[i]
                self.levels_right[i] = data[i + self.SPECTRUM_SIZE]
        except IndexError:
            print("Error: Incomplete spectrum data received")
            
    def draw_spectrum_bars(self):
        """Draw spectrum analyzer bars on both displays"""
        try:
            # Left display
            with canvas(self.display_left) as draw:
                for i in range(self.SPECTRUM_SIZE):
                    height = int(self.levels_left[i] * 53 / 255)
                    x = 2 + (i * 19)
                    y = 53 - height
                    
                    # Draw bar
                    draw.rectangle([x, y, x + 10, 53], fill="white")
                    
                    # Draw peak
                    if height > self.audio_bar_peak_left[i]:
                        self.audio_bar_peak_left[i] = height
                    draw.line([x, 53 - self.audio_bar_peak_left[i], x + 10, 
                              53 - self.audio_bar_peak_left[i]], fill="white")
                    
                    # Peak fall
                    if self.audio_bar_peak_left[i] > 0:
                        self.audio_bar_peak_left[i] -= 1
                        
                # Draw frequency labels
                labels = ["16K", "6.3K", "2.5K", "1K", "400", "160", "63"]
                for i, label in enumerate(labels):
                    draw.text((1 + i * 19, 54), label, font=self.font, fill="white")
                    
            # Right display - similar process
            with canvas(self.display_right) as draw:
                for i in range(self.SPECTRUM_SIZE):
                    height = int(self.levels_right[i] * 53 / 255)
                    x = 2 + (i * 19)
                    y = 53 - height
                    
                    draw.rectangle([x, y, x + 10, 53], fill="white")
                    
                    if height > self.audio_bar_peak_right[i]:
                        self.audio_bar_peak_right[i] = height
                    draw.line([x, 53 - self.audio_bar_peak_right[i], x + 10, 
                              53 - self.audio_bar_peak_right[i]], fill="white")
                    
                    if self.audio_bar_peak_right[i] > 0:
                        self.audio_bar_peak_right[i] -= 1
                        
                # Draw frequency labels (reversed order for right display)
                labels = ["63", "160", "400", "1K", "2.5K", "6.3K", "16K"]
                for i, label in enumerate(labels):
                    draw.text((1 + i * 19, 54), label, font=self.font, fill="white")
        except Exception as e:
            print(f"Error drawing spectrum bars: {e}")

    def run(self):
        """Main program loop"""
        print("Starting Audio Visualizer...")
        print("Press Ctrl+C to exit")
        
        try:
            while True:
                if self.sleep_mode:
                    self.display_left.clear()
                    self.display_right.clear()
                    time.sleep(0.1)
                    continue
                    
                # Check for I2C data
                try:
                    data = self.bus.read_i2c_block_data(self.I2C_ADDR, 0, self.BUFFER_SIZE)
                    self.process_i2c_data(data)
                except IOError as e:
                    print(f"I2C communication error: {e}")
                except Exception as e:
                    print(f"Unexpected error reading I2C: {e}")
                    
                # Update display based on current mode
                try:
                    if self.current_screen == 0:
                        self.draw_info_screen()
                    elif self.current_screen == 1:
                        self.draw_spectrum_bars()
                    elif self.current_screen == 2:
                        self.draw_vu_meter()
                except Exception as e:
                    print(f"Error updating display: {e}")
                    
                time.sleep(0.033)  # ~30 FPS
                
        except KeyboardInterrupt:
            print("\nShutting down...")
        finally:
            self.cleanup()

if __name__ == "__main__":
    visualizer = AudioVisualizer()
    try:
        visualizer.run()
    except Exception as e:
        print(f"Fatal error: {e}")
        visualizer.cleanup()