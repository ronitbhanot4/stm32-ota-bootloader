#ifndef UPDATE_IMAGE_H
#define UPDATE_IMAGE_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    UPDATE_IMAGE_OK = 0,
    UPDATE_IMAGE_BAD_HEADER = 3,
    UPDATE_IMAGE_WRONG_SLOT = 4
} UpdateImageStatus;

typedef struct {
    uint32_t payload_size;
    uint32_t payload_crc32;
} UpdateImageInfo;

UpdateImageStatus update_image_validate_header(const uint8_t *header,
                                               size_t length,
                                               UpdateImageInfo *info);

#endif
