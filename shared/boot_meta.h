#ifndef BOOT_META_H
#define BOOT_META_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>


#define BOOT_META_MAGIC         0x4154454Du
#define BOOT_META_VERSION       1u
#define BOOT_META_RECORD_SIZE   64u
#define BOOT_META_MAX_ATTEMPTS   3u

typedef enum {
    SLOT_EMPTY     = 0,
    SLOT_PENDING   = 1,
    SLOT_TRIAL     = 2,
    SLOT_CONFIRMED = 3,
    SLOT_BAD       = 4
} SlotState;

typedef struct {
    uint8_t state;
    uint8_t attempts_left;
    uint16_t reserved;
    uint32_t fw_version;
    uint32_t payload_size;
    uint32_t payload_crc32;
} SlotMeta;

typedef struct {
    uint32_t magic;
    uint16_t format_version;
    uint16_t record_size;
    uint32_t seq;
    uint8_t active_slot;
    uint8_t flags;
    uint16_t reserved;
    SlotMeta slot[2];
    uint8_t reserved2[12];
    uint32_t crc32;
} BootMeta;

_Static_assert(sizeof(SlotMeta) == 16u,
               "SlotMeta must be 16 bytes");
_Static_assert(sizeof(BootMeta) == 64u,
               "BootMeta must be 64 bytes");
_Static_assert(offsetof(BootMeta, slot) == 16u,
               "Slot entries must start at byte 16");
_Static_assert(offsetof(BootMeta, crc32) == 60u,
               "CRC must start at byte 60");

bool boot_meta_record_valid(const BootMeta *record);

int boot_meta_select(const BootMeta *copy0, const BootMeta *copy1);

void boot_meta_seal(BootMeta *record, uint32_t seq);

bool boot_meta_begin_trial(BootMeta *record, unsigned slot);
bool boot_meta_rollback(BootMeta *record, unsigned failed_slot);

#endif
