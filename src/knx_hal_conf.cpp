#include "knx_tx.h"
#include "config.h" // For DEBUG_SERIAL

#if KNX_TX_MODE 

extern "C" {
    #include "stm32f1xx_hal.h"
    #include "stm32f1xx_hal_rcc.h" // Để kiểm tra clock
}

// Forward declaration for custom error handler
void my_Error_Handler(void);

// Define handles here (single definition)
TIM_HandleTypeDef htim3;
DMA_HandleTypeDef hdma_tim3_ch3;

// Forward declarations
extern "C" void DMA1_Channel2_IRQHandler(void);

extern "C" void MX_TIM3_Init(void) {
    // Enable clocks
    __HAL_RCC_TIM3_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    // === GPIO PA10 as AF Push-Pull (TIM3_CH3) ===
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = GPIO_PIN_0;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    // === TIM3 basic init ===
    htim3.Instance = TIM3;
    htim3.Init.Prescaler = (SystemCoreClock / 1000000) - 1;
    htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
    //72MHz
    //     htim3.Init.Period = 7499
    //64MHz
    htim3.Init.Period = 103; // 64MHz / (6666 + 1) = 9600Hz (104.166us)
    htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim3.Init.RepetitionCounter = 0;
    htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    if (HAL_TIM_PWM_Init(&htim3) != HAL_OK) {
        my_Error_Handler();
    }

    // === 3 OC config for CH3 (PWM2) ===
    TIM_OC_InitTypeDef sConfigOC = {0};
  sConfigOC.OCMode = TIM_OCMODE_PWM2;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_LOW;
  sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
  sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
    if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_3) != HAL_OK) {
        my_Error_Handler();
    }

    // === DMA init for TIM3_CH3 ===
    hdma_tim3_ch3.Instance = DMA1_Channel2;
    hdma_tim3_ch3.Init.Direction = DMA_MEMORY_TO_PERIPH;
    hdma_tim3_ch3.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_tim3_ch3.Init.MemInc = DMA_MINC_ENABLE;
    hdma_tim3_ch3.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    hdma_tim3_ch3.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
    hdma_tim3_ch3.Init.Mode = DMA_NORMAL;
    hdma_tim3_ch3.Init.Priority = DMA_PRIORITY_HIGH;
    if (HAL_DMA_Init(&hdma_tim3_ch3) != HAL_OK) {
        my_Error_Handler();
       // DEBUG_SERIAL.printf("DMA_Init_Error ");
    }
    else {
      //  DEBUG_SERIAL.printf("DMA_Init_OK ");
    }

    // Link DMA handle to TIM handle (CC3)
    __HAL_LINKDMA(&htim3, hdma[TIM_DMA_ID_CC3], hdma_tim3_ch3);

    // NVIC for DMA Channel2
    HAL_NVIC_SetPriority(DMA1_Channel2_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(DMA1_Channel2_IRQn);

    // Debug clock
   // DEBUG_SERIAL.printf("System Clock: %lu Hz\r\n", HAL_RCC_GetSysClockFreq());
}

// Wrapper public
void knx_tx_init(void) {
    MX_TIM3_Init();
}

// DMA IRQ handler (must be C linkage)
extern "C" void DMA1_Channel2_IRQHandler(void) {
    HAL_DMA_IRQHandler(&hdma_tim3_ch3);
}

// Custom error handler với recovery mechanism
static uint32_t error_count = 0;
static uint32_t last_error_time = 0;

void my_Error_Handler(void) {
    error_count++;
    last_error_time = millis();
    
    // Log error
    DEBUG_SERIAL.printf("Error #%lu at %lu ms\n", error_count, last_error_time);
    
    // Recovery mechanism - reset peripherals
    if (error_count < 5) { // Giới hạn số lần retry
        DEBUG_SERIAL.println("Attempting recovery...");
        
        // Reset DMA
        HAL_DMA_DeInit(&hdma_tim3_ch3);
        HAL_DMA_Init(&hdma_tim3_ch3);
        
        // Reset Timer
        HAL_TIM_PWM_DeInit(&htim3);
        HAL_TIM_PWM_Init(&htim3);
        
        // Reconfigure channel
        TIM_OC_InitTypeDef sConfigOC = {0};
        sConfigOC.OCMode = TIM_OCMODE_PWM2;
        sConfigOC.Pulse = 0;
        sConfigOC.OCPolarity = TIM_OCPOLARITY_LOW;
        sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
        sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
        sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
        sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
        HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_3);
        
        DEBUG_SERIAL.println("Recovery completed");
    } else {
        // Quá nhiều lỗi, chuyển sang safe mode
        DEBUG_SERIAL.println("Too many errors - entering safe mode");
        while (1) {
            // Blink LED hoặc gửi error code
            delay(1000);
            DEBUG_SERIAL.println("SAFE MODE");
        }
    }
}
#else
#endif