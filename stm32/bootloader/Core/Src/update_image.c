#include "update_image.h"
#include "crc32.h"

static uint32_t read_le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0]
         | ((uint32_t)bytes[1] << 8)
         | ((uint32_t)bytes[2] << 16)
         | ((uint32_t)bytes[3] << 24);
}

UpdateImageStatus update_image_validate_header(const uint8_t *header,
                                               size_t length,
                                               UpdateImageInfo *info)
{
    if (length != 128u) {
        return UPDATE_IMAGE_BAD_HEADER;
    }

    if (read_le32(&header[0]) != 0x3141544Fu
        || header[4] != 1u || header[5] != 0u
        || header[6] != 128u || header[7] != 0u) {
        return UPDATE_IMAGE_BAD_HEADER;
    }

    if (crc32_ieee(header, 28u) != read_le32(&header[28])) {
        return UPDATE_IMAGE_BAD_HEADER;
    }

    uint32_t payload_size = read_le32(&header[12]);

    if (payload_size == 0u || payload_size > 196608u
        || payload_size % 4u != 0u) {
        return UPDATE_IMAGE_BAD_HEADER;
    }

    if (read_le32(&header[20]) != 0x08040000u) {
        return UPDATE_IMAGE_WRONG_SLOT;
    }

    info->payload_size = payload_size;
    info->payload_crc32 = read_le32(&header[16]);
    return UPDATE_IMAGE_OK;
}
