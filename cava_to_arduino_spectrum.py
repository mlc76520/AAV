import subprocess
from subprocess import PIPE
import struct
from smbus import SMBus
import json
import urllib.request
import requests

addr = 0x08 # bus address L
bus = SMBus(1) # indicates /dev/ic2-1
left_ch = 0x01
right_ch = 0x02
SCR = 0x00
SBT = 0x00
SLEEP = 0x00

BARS_NUMBER = 14

current_song_link = "http://localhost/command/?cmd=get_currentsong"
MESSAGE = "Hello World"
bytetype, bytesize, bytenorm = ("B", 1, 255)
chunk = bytesize * BARS_NUMBER
fmt = bytetype * BARS_NUMBER


def StringToBytes(val):
    retVal = []
    for c in val:
            retVal.append(ord(c))
    return retVal

def main():
    p=subprocess.Popen(["cava", "-p", "/etc/cava.conf"], stdout=PIPE)
    current_song_ps = "coucou"
    previousmodes = (0x00, 0x00, 0x00)
    while True:
        output = p.stdout.read(chunk)
        sample = [i for i in struct.unpack(fmt, output)]  # raw values without norming
        modes = (SCR, SBT, SLEEP)
        #r = requests.get(current_song_link)
        #r.json()
        #f = urllib.request.urlopen(current_song_link)
        #metadata_json = f.read()
        #current_song = json.loads(metadata_json)
        #if current_song_ps != current_song:
            #print(current_song["title"],"|", current_song["artist"],"|", current_song["album"],"|", current_song["state"])
            #bus.write_i2c_block_data(addrL, CMD_metaData, StringToBytes(current_song["state"]))
            #bus.write_i2c_block_data(addrT, CMD_metaData, StringToBytes(current_song["state"]))
            #current_song_ps = current_song

        try:
            bus.write_i2c_block_data(addr, left_ch, sample[0:7])
            bus.write_i2c_block_data(addr, right_ch, sample[7:14])
            #print(sample[0:7], "|", sample[7:14])

            if modes != previousmodes:
                bus.write_i2c_block_data(addr, 0x03, modes[0:3])
                previousmodes = modes
        except IOError:
            subprocess.call(['i2cdetect', '-y', '1'])
            #flag = 1     #optional flag to signal your code to resend 

if __name__ == "__main__":
    main()

