#ifndef UPDATE_UART_H
#define UPDATE_UART_H

#include <stdbool.h>
#include <stddef.h>
#include "main.h"

#define UPDATE_UART_FRAME_CAPACITY 266u

bool update_uart_read_frame(UART_HandleTypeDef *uart,
                            uint8_t buffer[UPDATE_UART_FRAME_CAPACITY],
                            size_t *frame_size);



bool update_uart_send_reply(UART_HandleTypeDef *uart,
                            uint8_t request_type,
                            uint16_t seq,
                            uint8_t reason);
#endif
