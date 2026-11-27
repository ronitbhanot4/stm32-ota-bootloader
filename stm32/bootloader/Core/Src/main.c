/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>
#include "crc32.h"
#include "flash_slot_b.h"
#include "update_uart.h"
#include "update_frame.h"
#include "update_session.h"
#include "boot_meta_flash.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define APP_ADDR   0x08010000U
#define APP_B_ADDR 0x08040000U
#define APP_SIZE   (192U * 1024U)
#define RAM_START  0x20000000U
#define RAM_END    0x20020000U
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static void print(const char *s)
{
  HAL_UART_Transmit(&huart2, (uint8_t *)s, strlen(s), 100);
}

static bool app_vectors_valid(uint32_t address, uint32_t size)
{
    uint32_t sp = *(volatile const uint32_t *)address;
    uint32_t reset = *(volatile const uint32_t *)(address + 4u);
    uint32_t reset_address = reset & ~1u;

    return sp > RAM_START
        && sp <= RAM_END
        && (sp & 7u) == 0u
        && (reset & 1u) != 0u
        && reset_address >= address
        && reset_address < address + size;
}

static bool app_a_vectors_valid(void)
{
    return app_vectors_valid(APP_ADDR, APP_SIZE);
}
static bool app_b_image_valid(const SlotMeta *slot)
{
    uint32_t size = slot->payload_size;

    if (size < 8u || size > APP_SIZE || size % 4u != 0u) {
        return false;
    }

    if (!app_vectors_valid(APP_B_ADDR, size)) {
        return false;
    }

    const uint8_t *image =
        (const uint8_t *)(uintptr_t)APP_B_ADDR;

    return crc32_ieee(image, size) == slot->payload_crc32;
}
static void jump_to_app(uint32_t address, uint32_t size)
{
	uint32_t app_sp = *(volatile const uint32_t *)address;
	uint32_t app_reset = *(volatile const uint32_t *)(address + 4u);


	if (!app_vectors_valid(address, size)) {
	    print("BOOT: invalid application vector table\r\n");
	    return;
	}

  print("BOOT: jumping to app\r\n");

  HAL_UART_DeInit(&huart2);
  HAL_RCC_DeInit();
  HAL_DeInit();

  __disable_irq();
  SysTick->CTRL = 0;
  SysTick->LOAD = 0;
  SysTick->VAL  = 0;
  for (int i = 0; i < 8; i++)
  {
    NVIC->ICER[i] = 0xFFFFFFFF;
    NVIC->ICPR[i] = 0xFFFFFFFF;
  }

  /* Clear pending SysTick and PendSV exceptions. */
  SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;

  SCB->VTOR = address;
  __DSB();
  __ISB();

  /* Load the app stack and jump without further C stack accesses. */
  __asm volatile (
      "msr msp, %0\n"
      "cpsie i\n"
      "bx %1\n"
      :
      : "r" (app_sp), "r" (app_reset)
      : "memory"
  );

  __builtin_unreachable();
}
static bool prepare_app_a_boot(void)
{
    BootMeta metadata = {0};
    int copy = boot_meta_flash_load(&metadata);

    if (copy < 0) {
        if (!app_a_vectors_valid()) {
            print("BOOT: cannot initialize metadata; App A invalid\r\n");
            return false;
        }

        metadata.active_slot = 0u;
        metadata.slot[0].state = SLOT_CONFIRMED;

        if (!boot_meta_flash_save(&metadata)) {
            print("BOOT: metadata initialization failed\r\n");
            return false;
        }

        print("BOOT: initialized metadata for App A\r\n");
    }

    if (metadata.active_slot != 0u
        || metadata.slot[0].state != SLOT_CONFIRMED) {
        print("BOOT: metadata requires another boot path\r\n");
        return false;
    }

    print("BOOT: metadata selects confirmed App A\r\n");
    return true;
}

static bool start_trial_watchdog(void)
{
    IWDG->KR = 0xCCCCu;  /* Start watchdog and its LSI clock. */
    IWDG->KR = 0x5555u;  /* Allow configuration writes. */
    IWDG->PR = 4u;       /* Divide clock by 64. */
    IWDG->RLR = 2000u;   /* Approximately four seconds. */

    uint32_t start = HAL_GetTick();

    while (IWDG->SR != 0u) {
        if (HAL_GetTick() - start >= 1000u) {
            return false;
        }
    }

    IWDG->KR = 0xAAAAu;  /* Reload with the configured timeout. */
    return true;
}


static int prepare_boot(bool *trial_boot)
{
    BootMeta metadata = {0};
    *trial_boot = false;

    if (boot_meta_flash_load(&metadata) < 0) {
        return prepare_app_a_boot() ? 0 : -1;
    }

    SlotMeta *candidate = &metadata.slot[1];

    if (candidate->state == SLOT_PENDING
        || candidate->state == SLOT_TRIAL) {

        bool exhausted = candidate->state == SLOT_TRIAL
                      && candidate->attempts_left == 0u;

        if (exhausted) {
            bool fallback = boot_meta_rollback(&metadata, 1u);

            if (!boot_meta_flash_save(&metadata)) {
                print("BOOT: rollback metadata save failed\r\n");
                return -1;
            }

            if (!fallback) {
                print("BOOT: no confirmed fallback\r\n");
                return -1;
            }

            print("BOOT: rolling back to App A\r\n");
        } else if (!app_b_image_valid(candidate)
                   || candidate->attempts_left == 0u
                   || candidate->attempts_left > BOOT_META_MAX_ATTEMPTS) {

            candidate->state = SLOT_BAD;
            candidate->attempts_left = 0u;

            if (!boot_meta_flash_save(&metadata)) {
                print("BOOT: invalid image metadata save failed\r\n");
                return -1;
            }

            print("BOOT: rejected invalid App B candidate\r\n");
        } else {
            if (!boot_meta_begin_trial(&metadata, 1u)
                || !boot_meta_flash_save(&metadata)) {
                print("BOOT: trial metadata save failed\r\n");
                return -1;
            }

            *trial_boot = true;
            print("BOOT: trial App B\r\n");
            return 1;
        }
    }

    unsigned selected = metadata.active_slot;

    if (metadata.slot[selected].state != SLOT_CONFIRMED) {
        selected = 1u - selected;

        if (metadata.slot[selected].state != SLOT_CONFIRMED) {
            print("BOOT: no confirmed application\r\n");
            return -1;
        }

        metadata.active_slot = (uint8_t)selected;

        if (!boot_meta_flash_save(&metadata)) {
            print("BOOT: fallback selection save failed\r\n");
            return -1;
        }
    }

    bool valid = selected == 0u
               ? app_a_vectors_valid()
               : app_b_image_valid(&metadata.slot[1]);

    if (!valid) {
        print("BOOT: confirmed application invalid\r\n");
        return -1;
    }

    print(selected == 0u
          ? "BOOT: selected confirmed App A\r\n"
          : "BOOT: selected confirmed App B\r\n");

    return (int)selected;
}


static void run_update_mode(void)
{
    UpdateSession session = {0};
    uint8_t buffer[UPDATE_UART_FRAME_CAPACITY];

    print("BOOT: update mode\r\n");

    for (;;) {
        size_t size;
        UpdateFrame frame;

        if (!update_uart_read_frame(&huart2, buffer, &size)) {
            continue;
        }

        UpdateFrameStatus status = update_frame_decode(buffer, size, &frame);
        uint8_t reason;

        if (status == UPDATE_FRAME_BAD_CRC) {
            reason = 1u;  // FRAME_CRC
        } else if (status != UPDATE_FRAME_OK) {
            continue;
        } else {
            switch (frame.type) {
            case 0x01u:  // START
                reason = update_session_start(&session, &frame);
                break;
            case 0x02u:  // DATA
                reason = update_session_data(&session, &frame);
                break;
            case 0x03u:  // END
                reason = update_session_end(&session, &frame);
                break;
            default:
                continue;
            }
        }

        update_uart_send_reply(&huart2, frame.type, frame.seq, reason);
    }
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */


  if (HAL_GPIO_ReadPin(B1_GPIO_Port, B1_Pin) == GPIO_PIN_RESET) {
      run_update_mode();
  }


  print("\r\nBOOT: bootloader running\r\n");

  bool trial_boot = false;
  int selected = prepare_boot(&trial_boot);

  if (selected >= 0) {
      uint32_t address = selected == 0 ? APP_ADDR : APP_B_ADDR;

      if (trial_boot && !start_trial_watchdog()) {
          print("BOOT: watchdog initialization failed\r\n");
          Error_Handler();
      }

      jump_to_app(address, APP_SIZE);
  }

  print("BOOT: staying in bootloader\r\n");
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    HAL_GPIO_TogglePin(LD2_GPIO_Port, LD2_Pin);
    HAL_Delay(100);
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 16;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : B1_Pin */
  GPIO_InitStruct.Pin = B1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : LD2_Pin */
  GPIO_InitStruct.Pin = LD2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LD2_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
