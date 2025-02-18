# media/mpd_client.py

from typing import Dict, Any, Optional
import logging
from mpd import MPDClient

from media.base import AbstractMediaFetcher
from utils.formatters import format_audio_info, format_time

class MPDFetcher(AbstractMediaFetcher):
    def __init__(self, i2c_visualizer):
        super().__init__(i2c_visualizer)
        self.client = MPDClient()

    def connect(self):
        """
        Establish connection to the MPD server
        """
        try:
            self.client.connect("localhost", 6600)
            self.client.timeout = 10
            self.connected = True
            logging.info("Connected to MPD server")
        except Exception as e:
            self.connected = False
            logging.error(f"MPD connection error: {e}")

    def fetch_metadata(self) -> Optional[Dict[str, Any]]:
        """
        Fetch current song and playback metadata from MPD
        
        Returns:
            Dict with song and playback information, or None if fetch fails
        """
        if not self.connected:
            return None

        try:
            # Fetch current status and song information
            status = self.client.status()
            current = self.client.currentsong()

            return {
                "title": current.get('title', 'No Title'),
                "artist": current.get('artist', 'No Artist'),
                "album": current.get('album', 'No Album'),
                "track": current.get('track', 'No Track'),
                "audio": format_audio_info(status.get('audio', 'Unknown')),
                "bitrate": f"{status.get('bitrate', '0')} kbps",
                "state": status.get('state', 'unknown'),
                "elapsed": format_time(status.get('elapsed', '0')),
                "duration": format_time(status.get('duration', '0')),
                "volume": status.get('volume', 0),
                "repeat": int(status.get('repeat', 0)),
                "random": int(status.get('random', 0)),
                "single": int(status.get('single', 0)),
                "consume": int(status.get('consume', 0))
            }
        except Exception as e:
            logging.error(f"MPD metadata fetch error: {e}")
            self.connected = False
            return None

    def next_song(self):
        """
        Skip to next song in the playlist
        """
        try:
            self.client.next()
            logging.info("MPD: Next song")
        except Exception as e:
            logging.error(f"Error skipping to next song: {e}")

    def previous_song(self):
        """
        Go back to previous song in the playlist
        """
        try:
            self.client.previous()
            logging.info("MPD: Previous song")
        except Exception as e:
            logging.error(f"Error skipping to previous song: {e}")

    def toggle_play_pause(self):
        """
        Toggle between play and pause states
        """
        try:
            status = self.client.status()
            if status.get('state') == 'play':
                self.client.pause()
                logging.info("MPD: Paused")
            else:
                self.client.play()
                logging.info("MPD: Playing")
        except Exception as e:
            logging.error(f"Error toggling play/pause: {e}")

    def set_volume(self, volume: int):
        """
        Set the playback volume
        
        Args:
            volume (int): Volume level between 0 and 100
        """
        try:
            # Ensure volume is within valid range
            volume = max(0, min(100, volume))
            self.client.setvol(volume)
            logging.info(f"MPD: Volume set to {volume}")
        except Exception as e:
            logging.error(f"Error setting volume: {e}")

    def toggle_repeat(self):
        """
        Toggle repeat mode on/off
        """
        try:
            status = self.client.status()
            new_repeat = 1 if status.get('repeat') == '0' else 0
            self.client.repeat(new_repeat)
            logging.info(f"MPD: Repeat {'enabled' if new_repeat else 'disabled'}")
        except Exception as e:
            logging.error(f"Error toggling repeat: {e}")

    def toggle_random(self):
        """
        Toggle random playback mode on/off
        """
        try:
            status = self.client.status()
            new_random = 1 if status.get('random') == '0' else 0
            self.client.random(new_random)
            logging.info(f"MPD: Random {'enabled' if new_random else 'disabled'}")
        except Exception as e:
            logging.error(f"Error toggling random: {e}")

    def cleanup(self):
        """
        Close MPD client connection
        """
        try:
            if self.connected:
                self.client.close()
                self.client.disconnect()
                logging.info("MPD client disconnected")
        except Exception as e:
            logging.error(f"Error closing MPD connection: {e}")