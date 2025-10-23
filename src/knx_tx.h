#pragma once
#include <Arduino.h>
#include "config.h"
#include "atomic_utils.h"
#include <stdint.h>


#if KNX_TX_MODE

#ifdef __cplusplus
extern "C" {
#endif

void knx_tx_init(void);                 // init TIM1 CH3 + DMA
knx_error_t knx_send_frame(uint8_t *data, int len);

#ifdef __cplusplus
}
#endif

#else

#ifdef __cplusplus
extern "C" {
#endif

void knx_tx_init(void);
knx_error_t knx_send_frame(uint8_t *data, int len);
void debug_dma_status(void);

#ifdef __cplusplus
}
#endif

#endif