from enum import Enum

class TextEncoding(Enum):
    UTF8 = 0
    Unicode = 1

def from_int(num : bytes) -> TextEncoding:
        if num == 0:
            return TextEncoding.UTF8
        else:
            return TextEncoding.Unicode

def get_decoding(text_encoding : TextEncoding) -> str:
    if text_encoding == TextEncoding.UTF8:
        return "utf-8-le"
    else:
        return "utf-16-le"

def as_bytes(text_encoding : TextEncoding) -> bytes:
    if text_encoding == TextEncoding.UTF8:
        return b'\x00'
    else:
        return b'\x01'