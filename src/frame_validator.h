#ifndef FRAME_VALIDATOR_H
#define FRAME_VALIDATOR_H

#include <stdint.h>
#include <stdbool.h>

// KNX Frame structure
typedef struct {
    uint8_t control;        // Control field
    uint8_t source[2];      // Source address
    uint8_t dest[2];        // Destination address
    uint8_t data_length;    // Data length
    uint8_t data[15];       // Data payload
    uint8_t checksum;       // Checksum
} knx_frame_t;

// Validation results
typedef enum {
    FRAME_VALID = 0,
    FRAME_ERROR_INVALID_LENGTH,
    FRAME_ERROR_INVALID_CONTROL,
    FRAME_ERROR_INVALID_ADDRESS,
    FRAME_ERROR_CHECKSUM,
    FRAME_ERROR_FORMAT
} frame_validation_result_t;

// Function prototypes
frame_validation_result_t validate_knx_frame(const uint8_t *data, uint8_t len);
bool is_valid_knx_address(const uint8_t *address);
bool is_valid_knx_control(uint8_t control);
const char* frame_validation_error_to_string(frame_validation_result_t result);

#endif // FRAME_VALIDATOR_H
