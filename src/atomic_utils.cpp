#include "atomic_utils.h"

const char* knx_error_to_string(knx_error_t error) {
    switch (error) {
        case KNX_OK: return "OK";
        case KNX_ERROR_BUFFER_FULL: return "Buffer full";
        case KNX_ERROR_INVALID_LENGTH: return "Invalid length";
        case KNX_ERROR_CHECKSUM: return "Checksum error";
        case KNX_ERROR_TIMEOUT: return "Timeout";
        case KNX_ERROR_BUS_BUSY: return "Bus busy";
        case KNX_ERROR_INVALID_PARAM: return "Invalid parameter";
        default: return "Unknown error";
    }
}
