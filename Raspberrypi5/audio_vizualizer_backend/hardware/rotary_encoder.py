# hardware/rotary_encoder.py

import RPi.GPIO as GPIO
import logging
from typing import Optional, Callable

class RotaryEncoder:
    def __init__(self, 
                 clk_pin: int, 
                 dt_pin: int, 
                 sw_pin: int, 
                 encoder_header: int, 
                 switch_header: int, 
                 i2c_visualizer,
                 callback: Optional[Callable] = None, 
                 rotation_callback: Optional[Callable] = None):
        self.clk_pin = clk_pin
        self.dt_pin = dt_pin
        self.sw_pin = sw_pin
        self.callback = callback
        self.rotation_callback = rotation_callback
        self.i2c = i2c_visualizer
        self.encoder_header = encoder_header
        self.switch_header = switch_header
        self.value = 0
        self.internal_counter = 0
        self.setup_gpio()
        
    def setup_gpio(self):
        GPIO.setup(self.clk_pin, GPIO.IN, pull_up_down=GPIO.PUD_UP)
        GPIO.setup(self.dt_pin, GPIO.IN, pull_up_down=GPIO.PUD_UP)
        GPIO.setup(self.sw_pin, GPIO.IN, pull_up_down=GPIO.PUD_UP)
        
        self.last_state = self._get_state()
        
        GPIO.add_event_detect(self.clk_pin, GPIO.BOTH, 
                            callback=self._encoder_callback, bouncetime=1)
        GPIO.add_event_detect(self.dt_pin, GPIO.BOTH, 
                            callback=self._encoder_callback, bouncetime=1)
        GPIO.add_event_detect(self.sw_pin, GPIO.BOTH, 
                            callback=self.switch_state_changed, bouncetime=300)

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