import struct
import time

import package
import uart_protocol as up

SLOT_B = package.SLOTS["B"]
SLOT_B_SIZE = package.SLOT_SIZE["B"]


class FakeBoard:
    def __init__(self, drop_ack=(), corrupt=(), junk=False):
        self.flash = bytearray(b"\xFF" * SLOT_B_SIZE)
        self.decoder = up.Decoder(up.SOF_PC)
        self.out = bytearray()
        self.header = None
        self.expected = 0
        self.writes = 0
        self.erases = 0
        self.drop_ack = set(drop_ack)
        self.corrupt = set(corrupt)
        self.junk = junk

    def receive(self, data):
        data = bytearray(data)
        if len(data) >= 4 and data[1] == up.DATA:
            (seq,) = struct.unpack_from("<H", data, 2)
            if seq in self.corrupt:
                self.corrupt.discard(seq)
                data[-5] ^= 0x01
        for frame in self.decoder.feed(data):
            self.handle(frame)

    def send(self, rtype, ftype, seq, reason=None):
        payload = bytes([ftype]) if reason is None else bytes([ftype, reason])
        if self.junk:
            self.out += b"\x00\xFF\xA5junk"
        self.out += up.encode(up.SOF_BOARD, rtype, seq, payload)

    def ack(self, ftype, seq):
        if ftype == up.DATA and seq in self.drop_ack:
            self.drop_ack.discard(seq)
            return
        self.send(up.ACK, ftype, seq)

    def nak(self, ftype, seq, reason):
        self.send(up.NAK, ftype, seq, reason)

    def handle(self, f):
        if not f.ok:
            return self.nak(f.type, f.seq, up.FRAME_CRC)
        if f.type == up.START:
            return self.on_start(f)
        if f.type == up.DATA:
            return self.on_data(f)
        if f.type == up.END:
            return self.on_end(f)

    def on_start(self, f):
        self.header = None
        try:
            info = package.parse_header(f.payload)
        except package.ImageError:
            return self.nak(f.type, f.seq, up.BAD_HEADER)
        if info["load_addr"] != SLOT_B:
            return self.nak(f.type, f.seq, up.WRONG_SLOT)
        self.flash[:] = b"\xFF" * SLOT_B_SIZE
        self.erases += 1
        self.header = info
        self.expected = 0
        self.ack(f.type, f.seq)

    def on_data(self, f):
        if self.header is None:
            return self.nak(f.type, f.seq, up.NOT_STARTED)
        if f.seq == self.expected - 1:
            return self.ack(f.type, f.seq)
        if f.seq != self.expected:
            return self.nak(f.type, f.seq, up.BAD_SEQ)
        offset = f.seq * up.MAX_CHUNK
        end = offset + len(f.payload)
        if end > self.header["payload_size"]:
            return self.nak(f.type, f.seq, up.TOO_BIG)
        if any(b != 0xFF for b in self.flash[offset:end]):
            return self.nak(f.type, f.seq, up.FLASH_ERROR)
        self.flash[offset:end] = f.payload
        self.writes += 1
        self.expected += 1
        self.ack(f.type, f.seq)

    def on_end(self, f):
        if self.header is None:
            return self.nak(f.type, f.seq, up.NOT_STARTED)
        size = self.header["payload_size"]
        if package.crc32(bytes(self.flash[:size])) != self.header["payload_crc32"]:
            return self.nak(f.type, f.seq, up.IMAGE_CRC)
        self.ack(f.type, f.seq)


class FakePort:
    def __init__(self, board, timeout=0.01):
        self.board = board
        self.timeout = timeout

    def write(self, data):
        self.board.receive(data)
        return len(data)

    def read(self, n):
        if not self.board.out:
            time.sleep(self.timeout)
            return b""
        chunk = bytes(self.board.out[:n])
        del self.board.out[:n]
        return chunk

    def reset_input_buffer(self):
        self.board.out.clear()

    def close(self):
        pass
