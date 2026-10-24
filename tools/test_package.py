import struct
import unittest

import package as p


def fake_bin(slot, size=1001):
    vectors = struct.pack("<II", 0x20020000, p.SLOTS[slot] + 0x1C5)
    return vectors + b"\x01" * (size - len(vectors))


class TestPackage(unittest.TestCase):
    def test_crc_check_value(self):
        self.assertEqual(p.crc32(b"123456789"), 0xCBF43926)

    def test_round_trip(self):
        image = p.build_image(fake_bin("B"), "B", 2)
        info = p.parse_image(image)
        self.assertEqual(info["slot"], "B")
        self.assertEqual(info["fw_version"], 2)
        self.assertEqual(info["payload_size"] % 4, 0)
        self.assertEqual(len(image), p.HEADER_SIZE + info["payload_size"])

    def test_padding_is_ff(self):
        image = p.build_image(fake_bin("A", 1001), "A", 1)
        self.assertTrue(image.endswith(b"\xFF" * 3))

    def test_wrong_slot_rejected(self):
        with self.assertRaises(p.ImageError):
            p.build_image(fake_bin("A"), "B", 1)

    def test_too_big_rejected(self):
        with self.assertRaises(p.ImageError):
            p.build_image(fake_bin("A", p.MAX_PAYLOAD + 4), "A", 1)

    def test_flipped_payload_byte_rejected(self):
        image = bytearray(p.build_image(fake_bin("B"), "B", 1))
        image[-1] ^= 0x01
        with self.assertRaises(p.ImageError):
            p.parse_image(bytes(image))

    def test_flipped_header_byte_rejected(self):
        image = bytearray(p.build_image(fake_bin("B"), "B", 1))
        image[8] ^= 0x01
        with self.assertRaises(p.ImageError):
            p.parse_image(bytes(image))

    def test_bad_magic_rejected(self):
        image = bytearray(p.build_image(fake_bin("B"), "B", 1))
        image[0:4] = b"NOPE"
        with self.assertRaises(p.ImageError):
            p.parse_image(bytes(image))


if __name__ == "__main__":
    unittest.main()
