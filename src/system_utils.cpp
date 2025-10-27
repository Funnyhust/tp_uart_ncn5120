#include "config.h"
#include "atomic_utils.h"
#include "knx_rx.h"
#include "knx_tx.h"
#include <IWatchdog.h>
#include "logger.h"

// Forward declarations
void handle_knx_frame(const uint8_t byte);

// NVIC Priority Configuration
void MX_NVIC_Init(void) {
    // Ưu tiên cao hơn cho Timer (để KNX đọc không bị block)
    HAL_NVIC_SetPriority(TIM2_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(TIM2_IRQn);
    // Ưu tiên cao nhất cho EXTI (để KNX đọc không bị block)
    HAL_NVIC_SetPriority(EXTI9_5_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);

    // Ưu tiên trung bình cho USART1
    HAL_NVIC_SetPriority(USART1_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);

    // Ưu tiên thấp nhất cho USART2
    HAL_NVIC_SetPriority(USART3_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(USART3_IRQn);
}

// System initialization function
void system_init(void) {
    // Initialize serial ports
    DEBUG_SERIAL.begin(UART_BAUD_RATE, SERIAL_8E1);
    MCU_SERIAL.begin(UART_BAUD_RATE, SERIAL_8E1);
    
    // Initialize watchdog
    //  IWatchdog.begin(WATCHDOG_TIMEOUT_US);
    
    // Initialize KNX modules
    knx_rx_init(handle_knx_frame);
    knx_tx_init();
    
    // Initialize NVIC priorities
    MX_NVIC_Init();
    
    // System ready message
    LOG_INFO(LOG_CAT_SYSTEM, "System initialized - KNX Gateway Ready");
}

// Error handler with logging
void error_handler(const char* error_msg) {
    if (ENABLE_ERROR_LOGGING) {
        DEBUG_SERIAL.printf("ERROR: %s at %lu ms\r\n", error_msg, millis());
    }
    
    // Log to main serial as well
    LOG_ERROR(LOG_CAT_SYSTEM, "ERR: %s", error_msg);
    
    // Could add LED indication or other error handling here
}

// System health check
bool system_health_check(void) {
    static uint32_t last_check = 0;
    uint32_t now = millis();
    
    // Check every 5 seconds
    if (now - last_check > 200) {
        last_check = now;
        
        // Check if watchdog is working
        IWatchdog.reload();
        
        // Check memory usage (basic check)
        if (ATOMIC_QUEUE_READ_COUNT() > KNX_MAX_QUEUE_SIZE * 0.8) {
            LOG_WARN(LOG_CAT_SYSTEM, "Queue nearly full");
            return false;
        }
        
       // LOG_INFO(LOG_CAT_SYSTEM, "System health OK");
    }
    
    return true;
}
