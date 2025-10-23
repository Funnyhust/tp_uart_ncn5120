#ifndef SYSTEM_UTILS_H
#define SYSTEM_UTILS_H

#include <Arduino.h>
#include <stdint.h>
#include <stdbool.h>

// Function prototypes
void system_init(void);
void error_handler(const char* error_msg);
void debug_print(const char* msg);
bool system_health_check(void);

#endif // SYSTEM_UTILS_H
