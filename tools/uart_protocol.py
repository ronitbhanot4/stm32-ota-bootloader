import struct
from collections import namedtuple

from package import crc32

SOF_PC = 0xA5
SOF_BOARD = 0x5A

START = 0x01
DATA = 0x02
END = 0x03
ACK = 0x80
NAK = 0x81
TYPE_NAMES = {START: "START", DATA: "DATA", END: "END", ACK: "ACK", NAK: "NAK"}

FRAME_CRC = 1
BAD_SEQ = 2
BAD_HEADER = 3
WRONG_SLOT = 4
TOO_BIG = 5
FLASH_ERROR = 6
IMAGE_CRC = 7
NOT_STARTED = 8
REASON_NAMES = {
    FRAME_CRC: "FRAME_CRC",
    BAD_SEQ: "BAD_SEQ",
    BAD_HEADER: "BAD_HEADER",
    WRONG_SLOT: "WRONG_SLOT",
    TOO_BIG: "TOO_BIG",
    FLASH_ERROR: "FLASH_ERROR",
    IMAGE_CRC: "IMAGE_CRC",
    NOT_STARTED: "NOT_STARTED",
}

MAX_CHUNK = 256
HEAD = struct.Struct("<BHH")
OVERHEAD = 1 + HEAD.size + 4
MAX_FRAME = OVERHEAD + MAX_CHUNK

Frame = namedtuple("Frame", "type seq payload ok")


def encode(sof, ftype, seq, payload=b""):
    if len(payload) > MAX_CHUNK:
        raise ValueError(f"payload {len(payload)} bytes, max {MAX_CHUNK}")
    body = HEAD.pack(ftype, seq, len(payload)) + bytes(payload)
    return bytes([sof]) + body + struct.pack("<I", crc32(body))


class Decoder:
    def __init__(self, sof):
        self.sof = sof
        self.buf = bytearray()

    def reset(self):
        self.buf.clear()

    def feed(self, data):
        self.buf += data
        frames = []
        while True:
            start = self.buf.find(self.sof)
            if start < 0:
                self.buf.clear()
                return frames
            del self.buf[:start]
            if len(self.buf) < 1 + HEAD.size:
                return frames
            ftype, seq, length = HEAD.unpack_from(self.buf, 1)
            if length > MAX_CHUNK:
                del self.buf[0]
                continue
            total = OVERHEAD + length
            if len(self.buf) < total:
                return frames
            body = bytes(self.buf[1:1 + HEAD.size + length])
            (crc,) = struct.unpack_from("<I", self.buf, 1 + HEAD.size + length)
            del self.buf[:total]
            if crc == crc32(body):
                frames.append(Frame(ftype, seq, body[HEAD.size:], True))
            else:
                frames.append(Frame(ftype, seq, b"", False))
