# utils/formatters.py

import html
from typing import Union

def clean_text(text: str) -> str:
    if not isinstance(text, str):
        text = str(text)
    return html.unescape(text).strip()

def format_audio_info(audio_str: str) -> str:
    if not audio_str:
        return "No Format"
    try:
        rate, bits, channels = audio_str.split(':')
        rate = float(rate) / 1000
        return f"{rate}kHz/{bits}bit"
    except:
        return audio_str

def format_time(seconds: Union[str, int, float]) -> str:
    try:
        total_seconds = int(float(seconds))
        hours = total_seconds // 3600
        minutes = (total_seconds % 3600) // 60
        seconds = total_seconds % 60

        if hours > 0:
            return f"{hours:02d}:{minutes:02d}:{seconds:02d}"
        return f"{minutes:02d}:{seconds:02d}"
    except:
        return "00:00"