#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "boot_meta.h"
#include "crc32.h"

int main(void)
{
    BootMeta record = {0};

    record.magic = BOOT_META_MAGIC;
    record.format_version = BOOT_META_VERSION;
    record.record_size = BOOT_META_RECORD_SIZE;
    record.seq = 1u;
    record.active_slot = 0u;
    record.slot[0].state = SLOT_CONFIRMED;
    record.slot[1].state = SLOT_EMPTY;

    record.crc32 = crc32_ieee(
        (const uint8_t *)&record, offsetof(BootMeta, crc32));

    assert(boot_meta_record_valid(&record));
    puts("PASS: valid metadata accepted");

    record.slot[0].fw_version ^= 1u;

    assert(!boot_meta_record_valid(&record));
    puts("PASS: changed metadata rejected");

    memset(&record, 0xFF, sizeof record);

    assert(!boot_meta_record_valid(&record));
    puts("PASS: erased metadata rejected");


        BootMeta copy0 = {0};

    copy0.magic = BOOT_META_MAGIC;
    copy0.format_version = BOOT_META_VERSION;
    copy0.record_size = BOOT_META_RECORD_SIZE;
    copy0.seq = 1u;
    copy0.slot[0].state = SLOT_CONFIRMED;
    copy0.crc32 = crc32_ieee(
        (const uint8_t *)&copy0, offsetof(BootMeta, crc32));

    BootMeta copy1 = copy0;
    copy1.seq = 2u;
    copy1.crc32 = crc32_ieee(
        (const uint8_t *)&copy1, offsetof(BootMeta, crc32));

    assert(boot_meta_select(&copy0, &copy1) == 1);
    puts("PASS: newer valid copy selected");

    copy1.slot[0].fw_version ^= 1u;

    assert(boot_meta_select(&copy0, &copy1) == 0);
    puts("PASS: damaged newer copy falls back to older copy");

    assert(boot_meta_select(&copy1, &copy0) == 1);
    puts("PASS: valid Copy 1 selected when Copy 0 is damaged");

    assert(boot_meta_select(&copy1, &copy1) == -1);
    puts("PASS: two invalid copies rejected");



        BootMeta fresh = {0};
    fresh.active_slot = 0u;
    fresh.slot[0].state = SLOT_CONFIRMED;

    boot_meta_seal(&fresh, 1u);

    assert(boot_meta_record_valid(&fresh));
    assert(fresh.seq == 1u);
    puts("PASS: sealed first-boot record is valid");

    BootMeta trial = {0};
    trial.slot[0].state = SLOT_CONFIRMED;
    trial.slot[1].state = SLOT_PENDING;
    trial.slot[1].attempts_left = BOOT_META_MAX_ATTEMPTS;

    assert(boot_meta_begin_trial(&trial, 1u));
    assert(trial.active_slot == 1u);
    assert(trial.slot[1].state == SLOT_TRIAL);
    assert(trial.slot[1].attempts_left == 2u);
    assert(trial.slot[0].state == SLOT_CONFIRMED);
    puts("PASS: first trial selects B and preserves confirmed A");

    assert(boot_meta_begin_trial(&trial, 1u));
    assert(trial.slot[1].attempts_left == 1u);

    assert(boot_meta_begin_trial(&trial, 1u));
    assert(trial.slot[1].attempts_left == 0u);
    puts("PASS: three trial attempts allowed");

    BootMeta before_refusal = trial;

    assert(!boot_meta_begin_trial(&trial, 1u));
    assert(memcmp(&trial, &before_refusal, sizeof trial) == 0);
    puts("PASS: fourth trial refused without changing metadata");

    assert(!boot_meta_begin_trial(&trial, 0u));
    assert(!boot_meta_begin_trial(&trial, 2u));
    assert(memcmp(&trial, &before_refusal, sizeof trial) == 0);
    puts("PASS: confirmed slot and invalid slot refused");



        BootMeta rollback = {0};
    rollback.active_slot = 1u;
    rollback.slot[0].state = SLOT_CONFIRMED;
    rollback.slot[1].state = SLOT_TRIAL;
    rollback.slot[1].attempts_left = 0u;

    assert(boot_meta_rollback(&rollback, 1u));
    assert(rollback.active_slot == 0u);
    assert(rollback.slot[0].state == SLOT_CONFIRMED);
    assert(rollback.slot[1].state == SLOT_BAD);
    puts("PASS: exhausted trial rolls back to confirmed A");

    BootMeta no_fallback = {0};
    no_fallback.active_slot = 1u;
    no_fallback.slot[1].state = SLOT_TRIAL;

    assert(!boot_meta_rollback(&no_fallback, 1u));
    assert(no_fallback.slot[1].state == SLOT_BAD);
    assert(no_fallback.active_slot == 1u);
    puts("PASS: no fallback refuses boot and marks trial bad");







    return 0;
}