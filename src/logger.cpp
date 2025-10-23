#include "logger.h"
#include "config.h"
#include <stdarg.h>
#include <string.h>

// Logger state
static logger_config_t logger_config = {
    .level = LOGGER_DEFAULT_LEVEL,
    .enable_timestamp = true,
    .enable_category = true,
    .enable_level = true,
    .enable_color = true,
    .max_logs_per_second = 100
};

static bool category_enabled[LOG_CAT_MAX] = {true}; // All enabled by default
static uint32_t log_counters[LOG_LEVEL_MAX] = {0};
static uint32_t category_counters[LOG_CAT_MAX] = {0};
static uint32_t total_logs = 0;
static uint32_t last_log_time = 0;
static uint32_t logs_this_second = 0;

// ANSI color codes
#define ANSI_RESET   "\033[0m"
#define ANSI_RED     "\033[31m"
#define ANSI_YELLOW  "\033[33m"
#define ANSI_GREEN   "\033[32m"
#define ANSI_BLUE    "\033[34m"
#define ANSI_MAGENTA "\033[35m"
#define ANSI_CYAN    "\033[36m"

// Color mapping for log levels
static const char* level_colors[LOG_LEVEL_MAX] = {
    ANSI_RED,     // ERROR
    ANSI_YELLOW,  // WARN
    ANSI_GREEN,   // INFO
    ANSI_BLUE,    // DEBUG
    ANSI_CYAN     // TRACE
};

// Level names
static const char* level_names[LOG_LEVEL_MAX] = {
    "ERROR",
    "WARN ",
    "INFO ",
    "DEBUG",
    "TRACE"
};

// Category names
static const char* category_names[LOG_CAT_MAX] = {
    "SYS  ",
    "UART ",
    "KNX_T",
    "KNX_R",
    "QUEUE",
    "ECHO ",
    "VALID",
    "ERROR"
};

void logger_init(void) {
    // Initialize all categories as enabled
    for (int i = 0; i < LOG_CAT_MAX; i++) {
        category_enabled[i] = true;
    }
    
    // Reset counters
    memset(log_counters, 0, sizeof(log_counters));
    memset(category_counters, 0, sizeof(category_counters));
    total_logs = 0;
    last_log_time = 0;
    logs_this_second = 0;
    
    DEBUG_SERIAL.println("\n=== KNX Gateway Logger Initialized ===");
    LOG_INFO(LOG_CAT_SYSTEM, "Logger initialized - Level: %s", level_names[logger_config.level]);
}

void logger_set_level(log_level_t level) {
    if (level < LOG_LEVEL_MAX) {
        logger_config.level = level;
        LOG_INFO(LOG_CAT_SYSTEM, "Log level changed to: %s", level_names[level]);
    }
}

void logger_set_config(const logger_config_t* config) {
    if (config) {
        logger_config = *config;
        LOG_INFO(LOG_CAT_SYSTEM, "Logger configuration updated");
    }
}

void logger_enable_category(log_category_t category, bool enable) {
    if (category < LOG_CAT_MAX) {
        category_enabled[category] = enable;
        LOG_INFO(LOG_CAT_SYSTEM, "Category %s %s", 
                category_names[category], enable ? "enabled" : "disabled");
    }
}

void logger_log(log_level_t level, log_category_t category, const char* format, ...) {
    // Check if logging is enabled
    if (level > logger_config.level || !category_enabled[category]) {
        return;
    }
    
    // Rate limiting
    uint32_t now = millis();
    if (now - last_log_time >= 1000) {
        logs_this_second = 0;
        last_log_time = now;
    }
    
    if (logs_this_second >= logger_config.max_logs_per_second) {
        return; // Rate limit exceeded
    }
    logs_this_second++;
    
    // Update counters
    log_counters[level]++;
    category_counters[category]++;
    total_logs++;
    
    // Build log message
    char buffer[256];
    int pos = 0;
    
    // Timestamp
    if (logger_config.enable_timestamp) {
        pos += snprintf(buffer + pos, sizeof(buffer) - pos, "[%8lu] ", now);
    }
    
    // Level with color
    if (logger_config.enable_level) {
        if (logger_config.enable_color) {
            pos += snprintf(buffer + pos, sizeof(buffer) - pos, "%s%s%s ", 
                          level_colors[level], level_names[level], ANSI_RESET);
        } else {
            pos += snprintf(buffer + pos, sizeof(buffer) - pos, "%s ", level_names[level]);
        }
    }
    
    // Category
    if (logger_config.enable_category) {
        pos += snprintf(buffer + pos, sizeof(buffer) - pos, "[%s] ", category_names[category]);
    }
    
    // Message
    va_list args;
    va_start(args, format);
    pos += vsnprintf(buffer + pos, sizeof(buffer) - pos, format, args);
    va_end(args);
    
    // Ensure null termination
    buffer[sizeof(buffer) - 1] = '\0';
    
    // Output to serial
    DEBUG_SERIAL.println(buffer);
}

void logger_log_hex(log_level_t level, log_category_t category, const char* prefix, const uint8_t* data, uint8_t len) {
    if (level > logger_config.level || !category_enabled[category] || !data || len == 0) {
        return;
    }
    
    // Rate limiting
    uint32_t now = millis();
    if (now - last_log_time >= 1000) {
        logs_this_second = 0;
        last_log_time = now;
    }
    
    if (logs_this_second >= logger_config.max_logs_per_second) {
        return;
    }
    logs_this_second++;
    
    // Update counters
    log_counters[level]++;
    category_counters[category]++;
    total_logs++;
    
    // Build hex message
    char buffer[512];
    int pos = 0;
    
    // Timestamp
    if (logger_config.enable_timestamp) {
        pos += snprintf(buffer + pos, sizeof(buffer) - pos, "[%8lu] ", now);
    }
    
    // Level with color
    if (logger_config.enable_level) {
        if (logger_config.enable_color) {
            pos += snprintf(buffer + pos, sizeof(buffer) - pos, "%s%s%s ", 
                          level_colors[level], level_names[level], ANSI_RESET);
        } else {
            pos += snprintf(buffer + pos, sizeof(buffer) - pos, "%s ", level_names[level]);
        }
    }
    
    // Category
    if (logger_config.enable_category) {
        pos += snprintf(buffer + pos, sizeof(buffer) - pos, "[%s] ", category_names[category]);
    }
    
    // Prefix
    if (prefix) {
        pos += snprintf(buffer + pos, sizeof(buffer) - pos, "%s: ", prefix);
    }
    
    // Hex data
    for (uint8_t i = 0; i < len && pos < sizeof(buffer) - 4; i++) {
        pos += snprintf(buffer + pos, sizeof(buffer) - pos, "%02X ", data[i]);
    }
    
    // Ensure null termination
    buffer[sizeof(buffer) - 1] = '\0';
    
    // Output to serial
    DEBUG_SERIAL.println(buffer);
}

const char* logger_level_to_string(log_level_t level) {
    if (level < LOG_LEVEL_MAX) {
        return level_names[level];
    }
    return "UNKNOWN";
}

const char* logger_category_to_string(log_category_t category) {
    if (category < LOG_CAT_MAX) {
        return category_names[category];
    }
    return "UNKNOWN";
}

void logger_get_stats(uint32_t* total_logs_out, uint32_t* logs_per_level, uint32_t* logs_per_category) {
    if (total_logs_out) {
        *total_logs_out = total_logs;
    }
    
    if (logs_per_level) {
        memcpy(logs_per_level, log_counters, sizeof(log_counters));
    }
    
    if (logs_per_category) {
        memcpy(logs_per_category, category_counters, sizeof(category_counters));
    }
}
