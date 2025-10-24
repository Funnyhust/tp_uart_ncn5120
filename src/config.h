#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// Khai báo serial debug dùng chung
extern HardwareSerial DEBUG_SERIAL;
extern HardwareSerial MCU_SERIAL;

// KNX Configuration - Cải thiện với constants rõ ràng
#define KNX_TX_MODE 1 // 1: PWM, 0: OC
#define KNX_RX_MODE 0 // 1: Gửi Frame, 0: Gửi Byte

// Timing constants (microseconds)
#define KNX_BIT_PERIOD_US 104
#define KNX_BIT0_MIN_US 25
#define KNX_BIT0_MAX_US 55
#define KNX_FRAME_TIMEOUT_US 1500
#define KNX_BUS_BUSY_TIMEOUT_MS 4

// Buffer sizes
#define KNX_BUFFER_MAX_SIZE 23
#define KNX_MAX_FRAME_LEN 23
#define KNX_MAX_QUEUE_SIZE 50


// UART Configuration
#define UART_BAUD_RATE 19200
#define UART_TIMEOUT_MS 100

// Watchdog Configuration 500000=500ms
#define WATCHDOG_TIMEOUT_US 5000000

// Error Recovery
#define MAX_ERROR_RETRY_COUNT 5
#define RECOVERY_TIMEOUT_MS 10

// Logger Configuration
#define LOGGER_DEFAULT_LEVEL LOG_LEVEL_INFO
#define LOGGER_ENABLE_TIMESTAMP 1
#define LOGGER_ENABLE_CATEGORY 1
#define LOGGER_ENABLE_LEVEL 1
#define LOGGER_ENABLE_COLOR 1
#define LOGGER_MAX_LOGS_PER_SECOND 100

// Legacy Debug Configuration (deprecated - use logger instead)
#define ENABLE_DEBUG_PRINTS 1
#define ENABLE_ERROR_LOGGING 1

#if 0
#define FRAME_MODE
#else
#define BYTE_MODE
#endif


#endif

