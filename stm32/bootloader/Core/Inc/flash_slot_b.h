#ifndef FLASH_SLOT_B_H
#define FLASH_SLOT_B_H

#include <stdbool.h>
#include <stdint.h>

bool slot_b_word_address_valid(uint32_t address);
bool slot_b_write_word(uint32_t address, uint32_t word);

bool slot_b_erase(void);

#endif
