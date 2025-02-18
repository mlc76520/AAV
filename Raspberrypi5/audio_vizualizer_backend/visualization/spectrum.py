import logging
import struct
from typing import List, Optional

class SpectrumVisualizer:
    """
    Handles audio spectrum visualization for I2C display
    """
    def __init__(self, bars_number: int = 14):
        """
        Initialize Spectrum Visualizer
        
        Args:
            bars_number (int): Number of spectrum bars to process
        """
        self.bars_number = bars_number
        self.prev_sample = [0] * bars_number

    def process_spectrum(self, cava_output: bytes) -> Optional[List[int]]:
        """
        Process raw Cava output and convert to spectrum bars
        
        Args:
            cava_output (bytes): Raw audio spectrum data from Cava
        
        Returns:
            List of processed bar heights or None if processing fails
        """
        try:
            # Unpack raw bytes into integer values
            sample = list(struct.unpack(f'{"B" * self.bars_number}', cava_output))
            
            # Return spectrum bars, limited to bars_number
            return sample[:self.bars_number]
        
        except Exception as e:
            logging.error(f"Spectrum processing error: {e}")
            return None