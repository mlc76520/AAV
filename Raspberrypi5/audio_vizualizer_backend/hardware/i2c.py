# hardware/i2c.py

import logging
import smbus
from typing import List

class I2CVisualizer:
    def __init__(self, i2c_address: int, bus_number: int):
        self.i2c_bus = smbus.SMBus(bus_number)
        self.i2c_address = i2c_address
        self.bus_number = bus_number

    def send_data(self, header: int, data: List[int]) -> None:
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