

#include "knx_tx.h"
#include "knx_rx.h"
#include "config.h"
#include <string.h> // For memset
#include "logger.h"
extern "C" {
  #include "stm32f1xx_hal.h"
}
// handles được init trong knx_hal_conf.cpp
extern TIM_HandleTypeDef htim3;
extern DMA_HandleTypeDef hdma_tim3_ch3;

// buffer DMA (halfword)
static uint16_t dma_buf[269];
static int dma_len = 0;

// ===== Thông số timing (72 MHz) =====
#define BIT_PERIOD   104   // ~104µs
#define T0_HIGH      35  // ~69µs (xung cao cho bit 0, theo ý bạn)
#define T0_TOL       700    // ~10µs dung sai (không dùng)

// ===== Encode bit =====
static void encode_bit(uint8_t bit) {
    if (dma_len >= (int)(sizeof(dma_buf) / sizeof(dma_buf[0]))) return;
    if (bit) {
        dma_buf[dma_len++] = 0;  // bit 1 => luôn Low (~104µs, đảo ngược)
    } else {
        dma_buf[dma_len++] = T0_HIGH;  // bit 0 => xung High ~69µs
    }
}

// ===== Encode byte (Start + 8 data + parity + Stop) =====
static void encode_byte(uint8_t b) {
    uint8_t parity_count = 0;

    encode_bit(0); // start = 0

    for (int i = 0; i < 8; i++) {
        uint8_t bit = (b >> i) & 0x01;
        encode_bit(bit);
        if (bit) parity_count++;
    } 

    // parity even
    encode_bit(parity_count & 1);
  //  encode_bit(1);    // stop = 1
    encode_bit(1);
    encode_bit(1);
    encode_bit(1);
}
// ===== Prepare frame =====
static void prepare_frame(uint8_t *data, int len) {
    memset(dma_buf, 0, sizeof(dma_buf));
    dma_len = 0;
    for (int i = 0; i < len; i++) {
        encode_byte(data[i]);
    }
 //    DEBUG_SERIAL.printf("Prepared frame, dma_len: %d\r\n", dma_len);
}

// ===== Public send function với error handling =====
knx_error_t knx_send_frame(uint8_t *data, int len) {
    // Input validation
    if (data == nullptr) {
        LOG_DEBUG(LOG_CAT_SYSTEM, "KNX TX: Invalid data pointer");
        return KNX_ERROR_INVALID_PARAM;
    }
    
    if (len <= 0 || len > KNX_BUFFER_MAX_SIZE) {
        LOG_DEBUG(LOG_CAT_SYSTEM, "KNX TX: Invalid length %d\n", len);
        return KNX_ERROR_INVALID_LENGTH;
    }
    
    // Kiểm tra DMA state
    if (hdma_tim3_ch3.State != HAL_DMA_STATE_READY) {
        LOG_DEBUG(LOG_CAT_SYSTEM, "KNX TX: DMA busy");
        return KNX_ERROR_BUS_BUSY;
    }
    
    // Final bus collision check - đọc trực tiếp GPIO
    prepare_frame(data, len);
    uint8_t bus_level = get_knx_rx_flag();
    if (bus_level) {
        // Kiểm tra tín hiệu trên chân RX
        uint8_t rx_pin_level = (GPIOB->IDR & (1 << 6)) ? 1 : 0; // Giả sử chân RX là PA9
        if(rx_pin_level){ // Nếu chân RX vẫn cao thì bus vẫn bận
            LOG_DEBUG(LOG_CAT_SYSTEM, "KNX TX: Bus collision detected - aborting");
            return KNX_ERROR_BUS_BUSY;
        }
    }
    // // Đợi Timer dừng hoàn toàn
    // uint32_t timeout = millis() + 10; // 10ms timeout
    // while (htim3.State != HAL_TIM_STATE_READY && millis() < timeout) {
    //     delay(1);
    //     LOG_DEBUG(LOG_CAT_SYSTEM, "Wait for Timer ready...");
    // }
    
    // if (htim3.State != HAL_TIM_STATE_READY) {
    //     LOG_DEBUG(LOG_CAT_SYSTEM, "KNX TX: Timer not ready");
    //     return KNX_ERROR_BUS_BUSY;
    // }
    // Start DMA transmission
    HAL_StatusTypeDef status = HAL_TIM_PWM_Start_DMA(&htim3, TIM_CHANNEL_3, (uint32_t*)dma_buf, dma_len);
    
    if (status != HAL_OK) {
        LOG_DEBUG(LOG_CAT_SYSTEM, "KNX TX: DMA start failed %d\n", status);
        return KNX_ERROR_BUS_BUSY;
    }
    
    return KNX_OK;
}

// ===== Callback khi DMA hoàn tất =====
extern "C" void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *htim) {
    if (htim->Instance == TIM3 && htim->Channel == HAL_TIM_ACTIVE_CHANNEL_3) {
        HAL_TIM_PWM_Stop_DMA(&htim3, TIM_CHANNEL_3);
        //DEBUG_SERIAL.printf("PWM Finished, DMA State: %d\r\n", hdma_tim3_ch3.State);
    }
}


