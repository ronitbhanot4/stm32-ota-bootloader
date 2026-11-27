#include "boot_meta_flash.h"
#include "main.h"
#include <string.h>

#define META_COPY_0_ADDR 0x08008000u
#define META_COPY_1_ADDR 0x0800C000u

static void read_record(uint32_t address, BootMeta *record)
{
    const volatile uint8_t *source =
        (const volatile uint8_t *)(uintptr_t)address;
    uint8_t *destination = (uint8_t *)record;

    for (size_t i = 0u; i < sizeof *record; i++) {
        destination[i] = source[i];
    }
}

int boot_meta_flash_load(BootMeta *record)
{
    BootMeta copy0;
    BootMeta copy1;

    read_record(META_COPY_0_ADDR, &copy0);
    read_record(META_COPY_1_ADDR, &copy1);

    int selected = boot_meta_select(&copy0, &copy1);

    if (selected == 0) {
        *record = copy0;
    } else if (selected == 1) {
        *record = copy1;
    }

    return selected;
}

bool boot_meta_flash_save(const BootMeta *record)
{
    BootMeta current;
    int selected = boot_meta_flash_load(&current);

    if (selected >= 0 && current.seq == UINT32_MAX) {
        return false;  // Refuse sequence-number overflow.
    }

    BootMeta next = *record;
    uint32_t next_seq = selected < 0 ? 1u : current.seq + 1u;
    boot_meta_seal(&next, next_seq);

    if (!boot_meta_record_valid(&next)) {
        return false;
    }

    // Choose the opposite sector. First write uses Copy 0.
    uint32_t address = selected == 0
                     ? META_COPY_1_ADDR : META_COPY_0_ADDR;
    uint32_t sector = selected == 0
                    ? FLASH_SECTOR_3 : FLASH_SECTOR_2;

    FLASH_EraseInitTypeDef erase = {0};
    uint32_t sector_error = 0u;

    erase.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase.Sector = sector;
    erase.NbSectors = 1u;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;

    if (HAL_FLASH_Unlock() != HAL_OK) {
        return false;
    }

    HAL_StatusTypeDef status =
        HAL_FLASHEx_Erase(&erase, &sector_error);

    const uint8_t *bytes = (const uint8_t *)&next;

    for (size_t offset = 0u;
         status == HAL_OK && offset < sizeof next;
         offset += 4u) {
        uint32_t word;
        memcpy(&word, &bytes[offset], sizeof word);

        status = HAL_FLASH_Program(
            FLASH_TYPEPROGRAM_WORD, address + offset, word);
    }

    HAL_StatusTypeDef lock_status = HAL_FLASH_Lock();

    if (status != HAL_OK || lock_status != HAL_OK) {
        return false;
    }

    BootMeta verified;
    read_record(address, &verified);

    return boot_meta_record_valid(&verified)
        && memcmp(&verified, &next, sizeof next) == 0;
}
