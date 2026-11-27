#ifndef BOOT_META_FLASH_H
#define BOOT_META_FLASH_H

#include "boot_meta.h"

int boot_meta_flash_load(BootMeta *record);



bool boot_meta_flash_save(const BootMeta *record);

#endif
