#include "update_frame.h"
#include "crc32.h"

UpdateFrameStatus update_frame_decode(const uint8_t *bytes,
                                      size_t size,
                                      UpdateFrame *frame)
{
    if (size < 10u || bytes[0] != 0xA5u) {
        return UPDATE_FRAME_BAD_FORMAT;
    }

    uint16_t length = (uint16_t)bytes[4]
                    | ((uint16_t)bytes[5] << 8);

    if (length > 256u || size != 10u + length) {
        return UPDATE_FRAME_BAD_FORMAT;
    }

    frame->type = bytes[1];
    frame->seq = (uint16_t)bytes[2]
               | ((uint16_t)bytes[3] << 8);
    frame->length = length;
    frame->payload = &bytes[6];

    const uint8_t *crc_bytes = &bytes[6u + length];
    uint32_t expected_crc = (uint32_t)crc_bytes[0]
                          | ((uint32_t)crc_bytes[1] << 8)
                          | ((uint32_t)crc_bytes[2] << 16)
                          | ((uint32_t)crc_bytes[3] << 24);

    if (crc32_ieee(&bytes[1], 5u + length) != expected_crc) {
        return UPDATE_FRAME_BAD_CRC;
    }

    return UPDATE_FRAME_OK;
}

size_t update_frame_encode_reply(uint8_t bytes[UPDATE_REPLY_CAPACITY],
                                 uint8_t request_type,
                                 uint16_t seq,
                                 uint8_t reason)
{
    uint8_t payload_length = (reason == 0u) ? 1u : 2u;

    bytes[0] = 0x5Au;                          // Board-to-PC start marker
    bytes[1] = (reason == 0u) ? 0x80u : 0x81u; // ACK or NAK
    bytes[2] = (uint8_t)seq;
    bytes[3] = (uint8_t)(seq >> 8);
    bytes[4] = payload_length;
    bytes[5] = 0u;
    bytes[6] = request_type;

    if (reason != 0u) {
        bytes[7] = reason;
    }

    uint32_t crc = crc32_ieee(&bytes[1], 5u + payload_length);
    size_t crc_offset = 6u + payload_length;

    for (unsigned i = 0u; i < 4u; i++) {
        bytes[crc_offset + i] = (uint8_t)(crc >> (8u * i));
    }

    return crc_offset + 4u;
}
