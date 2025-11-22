#include <Arduino.h>
#include "config.h"
#include "knx_tx.h"
#include "knx_rx.h"
#include "atomic_utils.h"
#include "system_utils.h"
#include "frame_validator.h"
#include "logger.h"
#include <IWatchdog.h>
#include "tpuart/tpuart.h"


// =================== UART ===================
HardwareSerial DEBUG_SERIAL(USART3);
HardwareSerial MCU_SERIAL(USART1);

// =================== Buffer & Flags ===================
#define KNX_BUFFER_MAX_SIZE 23
static uint8_t Rx_byte = 0;
static volatile bool knx_rx_flag = false;

bool get_knx_rx_flag_safe(void) { return ATOMIC_READ(knx_rx_flag); }
void set_knx_rx_flag_safe(bool v) { ATOMIC_WRITE(knx_rx_flag, v); }

// =================== Random Function ===================
static uint64_t seed = 1;
uint8_t random_num(uint8_t a, uint8_t b) {
    seed = (seed * 1664525ULL + 1013904223ULL) & 0xFFFFFFFFULL;
    return (seed % (b - a + 1)) + a;  // [a..b]

}
// =================== KNX RX callback ===================
void handle_knx_frame(const uint8_t byte) {
  ATOMIC_BLOCK_START();
  Rx_byte = byte;
  ATOMIC_BLOCK_END();
  set_knx_rx_flag_safe(true);
}

// =================== SETUP ===================
void setup() {
  system_init();
  logger_init();
  LOG_INFO(LOG_CAT_SYSTEM, "KNX Gateway started (STM32) - No FreeRTOS");
}

static uint32_t last_byte_time = 0;
static uint32_t checksum_rx_time = 0;
static bool pending_uart_send = false;
static uint32_t last_rx_time = 0;

// =================== MAIN LOOP - POLLING TẤT CẢ ===================
void loop() {
  // ========== 1. RX từ bus KNX (ISR đã set flag) ==========
  if (get_knx_rx_flag_safe()) {
    if(micros() - last_rx_time > 2800) {
      reset_rx_state();
    }
    set_knx_rx_flag_safe(false);
    knx_parse_BUS_byte(Rx_byte);
    last_rx_time = micros();
  }

//============================================================================================
  // NOTE: ACK Timing - CRITICAL (1298-1500µs window) ==========

  
  if (is_pending_ack()) {
    if (!pending_uart_send) {
      checksum_rx_time = micros();
      pending_uart_send = true;
    }
    if (!is_get_echo_frame()) {
      uint32_t elapsed = micros() - checksum_rx_time;
      // Window chính xác: 1298-1500µs
      if (elapsed >= 104*13 && elapsed < 104*15) {
        
        // Nếu có U_ACK_REQ từ MCU → gửi ACK xuống bus KNX
        knx_send_ack_byte( get_ack_value());
        pending_uart_send = false;
      }
      // Nếu quá 1500µs → timeout, reset
      else if (elapsed >= 104*15) {
        reset_pending_ack();
        pending_uart_send = false;
      }
    }
  }
//============================================================================================




  // ========== 3. UART từ MCU (polling) ==========

  
  if (MCU_SERIAL.available()) {
    // Nếu gap > 5ms → reset TX state (frame mới)
    if (micros() - last_byte_time > 2600) {
      reset_tx_state();
    }

    uint8_t b = MCU_SERIAL.read();
    knx_parse_MCU_byte(b);
    last_byte_time = micros();
  }

  // ========== 4. KNX TX: gửi frame nếu queue có dữ liệu ==========
  static bool waiting_backoff = false;
  static uint32_t backoff_time = 0;
  
  if (ATOMIC_QUEUE_READ_COUNT()) {
    // DEBUG_SERIAL.print("Queue count: ");
    // DEBUG_SERIAL.println(ATOMIC_QUEUE_READ_COUNT());
    if (!get_knx_rx_flag()) {
      if (!waiting_backoff) {
        backoff_time = millis() + random_num(2, 3);
        waiting_backoff = true;
      } else if (millis() >= backoff_time) {
        if (!get_knx_rx_flag()) {
          Frame f;
          if (dequeue_frame(&f)) {
            LOG_HEX_DEBUG(LOG_CAT_KNX_TX, "Sent frame", f.data, f.len);
            knx_send_frame(f.data, f.len);
          }
        }
        waiting_backoff = false;
      }
    } else {
      waiting_backoff = false;
    }
  }

  // ========== 5. System health check (mỗi 200ms) ==========
  static uint32_t last_system_check = 0;
  if (millis() - last_system_check >= 200) {
    system_health_check();
    last_system_check = millis();
  }
}
