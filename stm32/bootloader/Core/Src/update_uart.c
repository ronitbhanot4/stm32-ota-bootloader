#include "update_uart.h"
#include "update_frame.h"

bool update_uart_read_frame(UART_HandleTypeDef *uart,
                            uint8_t buffer[UPDATE_UART_FRAME_CAPACITY],
                            size_t *frame_size)
{
    *frame_size = 0u;

    if (HAL_UART_Receive(uart, &buffer[0], 1u, 100u) != HAL_OK
        || buffer[0] != 0xA5u) {
        return false;
    }

    for (size_t i = 1u; i < 6u; i++) {
        if (HAL_UART_Receive(uart, &buffer[i], 1u, 100u) != HAL_OK) {
            return false;
        }
    }

    uint16_t length = (uint16_t)buffer[4]
                    | ((uint16_t)buffer[5] << 8);

    if (length > 256u) {
        return false;
    }

    size_t total = 10u + length;

    for (size_t i = 6u; i < total; i++) {
        if (HAL_UART_Receive(uart, &buffer[i], 1u, 100u) != HAL_OK) {
            return false;
        }
    }

    *frame_size = total;
    return true;
}




bool update_uart_send_reply(UART_HandleTypeDef *uart,
                            uint8_t request_type,
                            uint16_t seq,
                            uint8_t reason)
{
    uint8_t reply[UPDATE_REPLY_CAPACITY];

    size_t size = update_frame_encode_reply(reply, request_type, seq, reason);

    return HAL_UART_Transmit(uart, reply, (uint16_t)size, 100u) == HAL_OK;
}
