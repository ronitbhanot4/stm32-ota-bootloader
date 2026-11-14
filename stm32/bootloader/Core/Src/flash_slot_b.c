#include "flash_slot_b.h"
#include "main.h"

#define SLOT_B_START           0x08040000u
#define SLOT_B_LAST_WORD_START 0x0807FFFCu

bool slot_b_word_address_valid(uint32_t address)
{
    return address >= SLOT_B_START
        && address <= SLOT_B_LAST_WORD_START
        && (address % 4u) == 0u;
}

bool slot_b_write_word(uint32_t address, uint32_t word)
{
    if (!slot_b_word_address_valid(address)) {
        return false;
    }

    if (HAL_FLASH_Unlock() != HAL_OK) {
        return false;
    }

    HAL_StatusTypeDef write_status =
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, address, word);

    HAL_StatusTypeDef lock_status = HAL_FLASH_Lock();

    return write_status == HAL_OK && lock_status == HAL_OK;
}

bool slot_b_erase(void)
{
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t sector_error = 0;

    erase.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase.Sector = FLASH_SECTOR_6;
    erase.NbSectors = 2u;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;  // 3.3 V Nucleo supply

    if (HAL_FLASH_Unlock() != HAL_OK) {
        return false;
    }

    HAL_StatusTypeDef erase_status =
        HAL_FLASHEx_Erase(&erase, &sector_error);

    HAL_StatusTypeDef lock_status = HAL_FLASH_Lock();

    return erase_status == HAL_OK && lock_status == HAL_OK;
}
