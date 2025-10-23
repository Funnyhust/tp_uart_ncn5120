#include "frame_validator.h"
#include "config.h"

frame_validation_result_t validate_knx_frame(const uint8_t *data, uint8_t len) {
    // Basic length check
    if (len < 7 || len > KNX_BUFFER_MAX_SIZE) {
        return FRAME_ERROR_INVALID_LENGTH;
    }
    
    // Extract frame components
    uint8_t control = data[0];
    uint8_t data_length = data[5] & 0x0F;
    uint8_t expected_length = 6 + data_length + 1 + 1 ; // header + data + checksum
    
    // Validate total length
    if (len != expected_length) {
        return FRAME_ERROR_INVALID_LENGTH;
    }
    
    // Validate control field
    // if (!is_valid_knx_control(control)) {
    //     return FRAME_ERROR_INVALID_CONTROL;
    // }
    
    // Validate addresses
    // if (!is_valid_knx_address(&data[1]) || !is_valid_knx_address(&data[3])) {
    //     return FRAME_ERROR_INVALID_ADDRESS;
    // }
    
    // Validate checksum
    extern uint8_t knx_calc_checksum(const uint8_t *data, uint8_t len);
    if (data[len-1] != knx_calc_checksum(data, len)) {
        return FRAME_ERROR_CHECKSUM;
        // DEBUG_SERIAL.printf(" | Expected: %02X, Calculated: %02X
    }
    
    return FRAME_VALID;
}

bool is_valid_knx_address(const uint8_t *address) {
    // KNX addresses: 0.0.0 to 15.15.255
    uint16_t addr = (address[0] << 8) | address[1];
    
    // Check if address is in valid range
    if (addr > 0xFFFF) {
        return false;
    }
    
    // Additional KNX-specific address validation can be added here
    return true;
}

bool is_valid_knx_control(uint8_t control) {
    // KNX control field validation
    // Bit 7: Frame type (0=standard, 1=extended)
    // Bit 6: Repeat flag
    // Bit 5: System broadcast
    // Bit 4: Priority
    // Bit 3-0: Reserved
    
    // Basic validation - can be extended based on KNX spec
    return true;
}

const char* frame_validation_error_to_string(frame_validation_result_t result) {
    switch (result) {
        case FRAME_VALID: return "Valid";
        case FRAME_ERROR_INVALID_LENGTH: return "Invalid length";
        case FRAME_ERROR_INVALID_CONTROL: return "Invalid control field";
        case FRAME_ERROR_INVALID_ADDRESS: return "Invalid address";
        case FRAME_ERROR_CHECKSUM: return "Checksum error";
        case FRAME_ERROR_FORMAT: return "Invalid format";
        default: return "Unknown error";
    }
}
