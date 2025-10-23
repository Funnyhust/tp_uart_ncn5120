#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>
#include <stdint.h>
#include <stdbool.h>

// Log levels
typedef enum {
    LOG_LEVEL_ERROR = 0,
    LOG_LEVEL_WARN,
    LOG_LEVEL_INFO,
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_TRACE,
    LOG_LEVEL_MAX
} log_level_t;

// Log categories
typedef enum {
    LOG_CAT_SYSTEM = 0,
    LOG_CAT_UART,
    LOG_CAT_KNX_TX,
    LOG_CAT_KNX_RX,
    LOG_CAT_QUEUE,
    LOG_CAT_ECHO_ACK,
    LOG_CAT_VALIDATION,
    LOG_CAT_ERROR,
    LOG_CAT_MAX
} log_category_t;

// Logger configuration
typedef struct {
    log_level_t level;
    bool enable_timestamp;
    bool enable_category;
    bool enable_level;
    bool enable_color;
    uint32_t max_logs_per_second;
} logger_config_t;

// Function prototypes
void logger_init(void);
void logger_set_level(log_level_t level);
void logger_set_config(const logger_config_t* config);
void logger_enable_category(log_category_t category, bool enable);

// Main logging functions
void logger_log(log_level_t level, log_category_t category, const char* format, ...);
void logger_log_hex(log_level_t level, log_category_t category, const char* prefix, const uint8_t* data, uint8_t len);

// Convenience macros
#define LOG_ERROR(cat, ...)   logger_log(LOG_LEVEL_ERROR, cat, __VA_ARGS__)
#define LOG_WARN(cat, ...)    logger_log(LOG_LEVEL_WARN, cat, __VA_ARGS__)
#define LOG_INFO(cat, ...)    logger_log(LOG_LEVEL_INFO, cat, __VA_ARGS__)
#define LOG_DEBUG(cat, ...)   logger_log(LOG_LEVEL_DEBUG, cat, __VA_ARGS__)
#define LOG_TRACE(cat, ...)   logger_log(LOG_LEVEL_TRACE, cat, __VA_ARGS__)

#define LOG_HEX_ERROR(cat, prefix, data, len) logger_log_hex(LOG_LEVEL_ERROR, cat, prefix, data, len)
#define LOG_HEX_WARN(cat, prefix, data, len)  logger_log_hex(LOG_LEVEL_WARN, cat, prefix, data, len)
#define LOG_HEX_INFO(cat, prefix, data, len)  logger_log_hex(LOG_LEVEL_INFO, cat, prefix, data, len)
#define LOG_HEX_DEBUG(cat, prefix, data, len) logger_log_hex(LOG_LEVEL_DEBUG, cat, prefix, data, len)
#define LOG_HEX_TRACE(cat, prefix, data, len) logger_log_hex(LOG_LEVEL_TRACE, cat, prefix, data, len)

// Utility functions
const char* logger_level_to_string(log_level_t level);
const char* logger_category_to_string(log_category_t category);
void logger_get_stats(uint32_t* total_logs, uint32_t* logs_per_level, uint32_t* logs_per_category);

#endif // LOGGER_H
