import dbus
import dbus.mainloop.glib
import logging
from typing import Dict, Any, Optional

from media.base import AbstractMediaFetcher
from utils.formatters import format_time

class BluetoothFetcher(AbstractMediaFetcher):
    def __init__(self, i2c_visualizer):
        super().__init__(i2c_visualizer)
        self.bus = None
        self.media_player = None
        self.props_iface = None

    def connect(self):
        """
        Establish connection to Bluetooth media player
        """
        try:
            # Ensure GLib main loop is set up for DBus
            dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
            
            # Connect to system bus
            self.bus = dbus.SystemBus()
            
            # Find and connect to the first available media player
            self._find_media_player()
            
            # Update connection status
            self.connected = bool(self.media_player)
            
            if self.connected:
                logging.info("Bluetooth media player connected")
            else:
                logging.warning("No Bluetooth media player found")
        
        except Exception as e:
            logging.error(f"Bluetooth connection setup error: {e}")
            self.connected = False
            self.bus = None
            self.media_player = None
            self.props_iface = None

    def _find_media_player(self):
        """
        Discover and set up the first available Bluetooth media player
        """
        try:
            # Get object manager for Bluez
            remote_om = dbus.Interface(
                self.bus.get_object("org.bluez", "/"),
                "org.freedesktop.DBus.ObjectManager"
            )
            
            # Get all managed objects
            objects = remote_om.GetManagedObjects()

            # Find first available media player
            for path, interfaces in objects.items():
                if "org.bluez.MediaPlayer1" in interfaces:
                    # Set up media player object
                    self.media_player = self.bus.get_object("org.bluez", path)
                    
                    # Set up properties interface
                    self.props_iface = dbus.Interface(
                        self.media_player,
                        "org.freedesktop.DBus.Properties"
                    )
                    
                    logging.info(f"Found Bluetooth media player at: {path}")
                    return

            # No media player found
            self.media_player = None
            self.props_iface = None

        except Exception as e:
            logging.error(f"Error finding media player: {e}")
            self.media_player = None
            self.props_iface = None

    def fetch_metadata(self) -> Optional[Dict[str, Any]]:
        """
        Fetch current track and playback metadata from Bluetooth media player
        
        Returns:
            Dict with song and playback information, or None if fetch fails
        """
        if not self.connected or not self.props_iface:
            return None

        try:
            # Get all properties for the media player
            properties = self.props_iface.GetAll('org.bluez.MediaPlayer1')

            # Get current track info
            track = properties.get('Track', {})
            status = str(properties.get('Status', 'unknown')).lower()

            # Map BlueZ status to standard states
            state_map = {
                'playing': 'play',
                'paused': 'pause',
                'stopped': 'stop',
                'forward-seek': 'play',
                'reverse-seek': 'play',
                'error': 'stop'
            }

            # Get position and duration
            try:
                position = properties.get('Position', 0) / 1000  # Convert to seconds
            except Exception:
                position = 0

            try:
                duration = int(track.get('Duration', 0)) / 1000  # Convert to seconds
            except Exception:
                duration = 0

            return {
                "title": str(track.get('Title', 'No Title')),
                "artist": str(track.get('Artist', 'No Artist')),
                "album": str(track.get('Album', 'No Album')),
                "track": str(track.get('TrackNumber', 'No Track')),
                "bitrate": "Unknown",  # Bluetooth typically doesn't provide this
                "audio": "Unknown",    # Bluetooth typically doesn't provide detailed audio format
                "state": state_map.get(status, status),
                "elapsed": format_time(position),
                "duration": format_time(duration),
                "volume": properties.get('Volume', 0)
            }

        except dbus.exceptions.DBusException as e:
            logging.error(f"DBus error fetching metadata: {e}")
            # Try to reconnect if we encounter a DBus error
            self._find_media_player()
            return None
        except Exception as e:
            logging.error(f"Error fetching Bluetooth metadata: {e}")
            return None

    def next_song(self):
        """
        Skip to next track if possible
        """
        try:
            if self.media_player:
                method = self.media_player.get_dbus_method('Next', 'org.bluez.MediaPlayer1')
                method()
                logging.info("Bluetooth: Next track")
        except Exception as e:
            logging.error(f"Error skipping to next track: {e}")

    def previous_song(self):
        """
        Go back to previous track if possible
        """
        try:
            if self.media_player:
                method = self.media_player.get_dbus_method('Previous', 'org.bluez.MediaPlayer1')
                method()
                logging.info("Bluetooth: Previous track")
        except Exception as e:
            logging.error(f"Error skipping to previous track: {e}")

    def toggle_play_pause(self):
        """
        Toggle between play and pause states
        """
        try:
            if self.media_player:
                method = self.media_player.get_dbus_method('PlayPause', 'org.bluez.MediaPlayer1')
                method()
                logging.info("Bluetooth: Toggle Play/Pause")
        except Exception as e:
            logging.error(f"Error toggling play/pause: {e}")

    def cleanup(self):
        """
        Clean up Bluetooth connection resources
        """
        try:
            # Reset all Bluetooth-related attributes
            if self.bus:
                self.bus.close()
            
            self.bus = None
            self.media_player = None
            self.props_iface = None
            self.connected = False
            
            logging.info("Bluetooth client cleaned up")
        except Exception as e:
            logging.error(f"Error during Bluetooth cleanup: {e}")