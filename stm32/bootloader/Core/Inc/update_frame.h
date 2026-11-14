#ifndef UPDATE_FRAME_H
#define UPDATE_FRAME_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    UPDATE_FRAME_OK,
    UPDATE_FRAME_BAD_FORMAT,
    UPDATE_FRAME_BAD_CRC
} UpdateFrameStatus;

typedef struct {
    uint8_t type;
    uint16_t seq;
    uint16_t length;
    const uint8_t *payload;
} UpdateFrame;

UpdateFrameStatus update_frame_decode(const uint8_t *bytes,
                                      size_t size,
                                      UpdateFrame *frame);

#define UPDATE_REPLY_CAPACITY 12u

size_t update_frame_encode_reply(uint8_t bytes[UPDATE_REPLY_CAPACITY],
                                 uint8_t request_type,
                                 uint16_t seq,
                                 uint8_t reason);

#endif
