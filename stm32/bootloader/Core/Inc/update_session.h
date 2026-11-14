#ifndef UPDATE_SESSION_H
#define UPDATE_SESSION_H

#include <stdbool.h>
#include "update_frame.h"
#include "update_image.h"

typedef struct {
    bool started;
    uint16_t next_seq;
    uint32_t bytes_written;
    UpdateImageInfo image;
} UpdateSession;

uint8_t update_session_start(UpdateSession *session,
                             const UpdateFrame *frame);


uint8_t update_session_data(UpdateSession *session,
                            const UpdateFrame *frame);

uint8_t update_session_end(const UpdateSession *session,
                           const UpdateFrame *frame);

#endif
