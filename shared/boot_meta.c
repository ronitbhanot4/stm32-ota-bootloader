#include "boot_meta.h"
#include "crc32.h"

bool boot_meta_record_valid(const BootMeta *record)
{
    if (record->magic != BOOT_META_MAGIC
        || record->format_version != BOOT_META_VERSION
        || record->record_size != BOOT_META_RECORD_SIZE
        || record->seq == 0u) {
        return false;
    }

    if (record->active_slot > 1u || record->flags != 0u
        || record->reserved != 0u) {
        return false;
    }

    for (unsigned i = 0u; i < 2u; i++) {
        const SlotMeta *slot = &record->slot[i];

        if (slot->state > SLOT_BAD || slot->reserved != 0u) {
            return false;
        }
    }

    for (unsigned i = 0u; i < sizeof record->reserved2; i++) {
        if (record->reserved2[i] != 0u) {
            return false;
        }
    }

    uint32_t actual_crc =
        crc32_ieee((const uint8_t *)record, offsetof(BootMeta, crc32));

    return actual_crc == record->crc32;
}

int boot_meta_select(const BootMeta *copy0, const BootMeta *copy1)
{
    bool valid0 = boot_meta_record_valid(copy0);
    bool valid1 = boot_meta_record_valid(copy1);

    if (!valid0 && !valid1) {
        return -1;
    }

    if (!valid0) {
        return 1;
    }

    if (!valid1) {
        return 0;
    }

    return copy1->seq > copy0->seq ? 1 : 0;
}


void boot_meta_seal(BootMeta *record, uint32_t seq)
{
    record->magic = BOOT_META_MAGIC;
    record->format_version = BOOT_META_VERSION;
    record->record_size = BOOT_META_RECORD_SIZE;
    record->seq = seq;

    record->crc32 = crc32_ieee(
        (const uint8_t *)record, offsetof(BootMeta, crc32));
}

bool boot_meta_begin_trial(BootMeta *record, unsigned slot)
{
    if (slot > 1u) {
        return false;
    }

    SlotMeta *candidate = &record->slot[slot];

    if ((candidate->state != SLOT_PENDING
         && candidate->state != SLOT_TRIAL)
        || candidate->attempts_left == 0u
        || candidate->attempts_left > BOOT_META_MAX_ATTEMPTS) {
        return false;
    }

    candidate->state = SLOT_TRIAL;
    candidate->attempts_left--;
    record->active_slot = (uint8_t)slot;

    return true;
}



bool boot_meta_rollback(BootMeta *record, unsigned failed_slot)
{
    if (failed_slot > 1u) {
        return false;
    }

    SlotMeta *failed = &record->slot[failed_slot];

    if (failed->state != SLOT_TRIAL || failed->attempts_left != 0u) {
        return false;
    }

    unsigned fallback = 1u - failed_slot;

    failed->state = SLOT_BAD;

    if (record->slot[fallback].state != SLOT_CONFIRMED) {
        return false;
    }

    record->active_slot = (uint8_t)fallback;
    return true;
}
