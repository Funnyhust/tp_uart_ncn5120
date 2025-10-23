#ifndef ATOMIC_UTILS_H
#define ATOMIC_UTILS_H

#include <Arduino.h>
#include <stdint.h>
#include <stdbool.h>

// Atomic operations cho STM32
// Disable interrupts để đảm bảo atomicity
#define ATOMIC_BLOCK_START() __disable_irq()
#define ATOMIC_BLOCK_END() __enable_irq()

// Safe access cho volatile variables
#define ATOMIC_READ(var) ({ \
    ATOMIC_BLOCK_START(); \
    typeof(var) temp = (var); \
    ATOMIC_BLOCK_END(); \
    temp; \
})

#define ATOMIC_WRITE(var, value) ({ \
    ATOMIC_BLOCK_START(); \
    (var) = (value); \
    ATOMIC_BLOCK_END(); \
})

// Thread-safe queue operations (q_count must be declared as extern volatile uint8_t)
extern volatile uint8_t q_count;
#define ATOMIC_QUEUE_READ_COUNT() ATOMIC_READ(q_count)
#define ATOMIC_QUEUE_WRITE_COUNT(value) ATOMIC_WRITE(q_count, value)

// KNX timing constants (moved to config.h to avoid duplication)
// #define KNX_BIT_PERIOD_US 104
// #define KNX_BIT0_MIN_US 25
// #define KNX_BIT0_MAX_US 55
// #define KNX_FRAME_TIMEOUT_US 1500
// #define KNX_BUS_BUSY_TIMEOUT_MS 4

// Buffer sizes (moved to config.h to avoid duplication)
// #define KNX_BUFFER_MAX_SIZE 23
// #define KNX_MAX_QUEUE_SIZE 50
// #define KNX_MAX_FRAME_LEN 23

// Error codes
typedef enum {
    KNX_OK = 0,
    KNX_ERROR_BUFFER_FULL,
    KNX_ERROR_INVALID_LENGTH,
    KNX_ERROR_CHECKSUM,
    KNX_ERROR_TIMEOUT,
    KNX_ERROR_BUS_BUSY,
    KNX_ERROR_INVALID_PARAM
} knx_error_t;

// Function prototypes
const char* knx_error_to_string(knx_error_t error);

#endif // ATOMIC_UTILS_H
