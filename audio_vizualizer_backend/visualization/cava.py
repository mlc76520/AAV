# visualization/cava.py

import subprocess
from subprocess import PIPE
import logging

class CavaProcess:
    def __init__(self):
        self.process = None

    def start(self):
        try:
            self.process = subprocess.Popen(
                ["cava", "-p", "/etc/cava.conf"], 
                stdout=PIPE
            )
            logging.info("Cava process started successfully")
        except FileNotFoundError:
            logging.error("Cava not found. Please install cava package.")
            raise

    def read_output(self, chunk_size):
        if self.process:
            return self.process.stdout.read(chunk_size)
        return None

    def cleanup(self):
        if self.process:
            self.process.terminate()