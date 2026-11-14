#include "update_session.h"
#include "flash_slot_b.h"
#include "crc32.h"

#define SLOT_B_BASE  0x08040000u
#define CHUNK_SIZE   256u

uint8_t update_session_start(UpdateSession *session,
                             const UpdateFrame *frame)
{
    UpdateImageInfo image;

    if (frame->seq != 0u) {
        return 2u;  // BAD_SEQ
    }

    UpdateImageStatus status =
        update_image_validate_header(frame->payload, frame->length, &image);

    if (status != UPDATE_IMAGE_OK) {
        return (uint8_t)status;  // BAD_HEADER or WRONG_SLOT
    }

    session->started = false;

    if (!slot_b_erase()) {
        return 6u;  // FLASH_ERROR
    }

    session->image = image;
    session->next_seq = 0u;
    session->bytes_written = 0u;
    session->started = true;
    return 0u;  // The reply code will turn this into an ACK.
}


uint8_t update_session_data(UpdateSession *session,
                            const UpdateFrame *frame)
{
    if (!session->started) {
        return 8u;  // NOT_STARTED
    }

    bool duplicate = session->next_seq > 0u
                  && frame->seq == session->next_seq - 1u;

    if (frame->seq != session->next_seq && !duplicate) {
        return 2u;  // BAD_SEQ
    }

    uint32_t offset = (uint32_t)frame->seq * CHUNK_SIZE;

    if (offset >= session->image.payload_size) {
        return 5u;  // TOO_BIG
    }

    uint32_t remaining = session->image.payload_size - offset;
    uint32_t expected = remaining < CHUNK_SIZE ? remaining : CHUNK_SIZE;

    if (frame->length > expected) {
        return 5u;  // TOO_BIG
    }
    if (frame->length != expected) {
        return 2u;  // Short chunk would leave a gap.
    }

    if (duplicate) {
        return 0u;  // ACK again; do not write the chunk twice.
    }

    for (uint32_t i = 0u; i < expected; i += 4u) {
        const uint8_t *p = &frame->payload[i];
        uint32_t word = (uint32_t)p[0]
                      | ((uint32_t)p[1] << 8)
                      | ((uint32_t)p[2] << 16)
                      | ((uint32_t)p[3] << 24);
        uint32_t address = SLOT_B_BASE + offset + i;

        if (*(const volatile uint32_t *)(uintptr_t)address != UINT32_MAX
            || !slot_b_write_word(address, word)) {
            session->started = false;
            return 6u;  // FLASH_ERROR; require a fresh START.
        }
    }

    session->bytes_written += expected;
    session->next_seq++;
    return 0u;
}

uint8_t update_session_end(const UpdateSession *session,
                           const UpdateFrame *frame)
{
    if (!session->started) {
        return 8u;  // NOT_STARTED
    }

    if (frame->seq != 0u || frame->length != 0u
        || session->bytes_written != session->image.payload_size) {
        return 2u;  // Invalid END or chunks still missing.
    }

    const uint8_t *image = (const uint8_t *)(uintptr_t)SLOT_B_BASE;
    uint32_t actual_crc = crc32_ieee(image, session->image.payload_size);

    return actual_crc == session->image.payload_crc32 ? 0u : 7u;
}
