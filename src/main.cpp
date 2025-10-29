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

#include "FreeRTOS.h"
#include "task.h"

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
// =================== KNX RX callback (giữ nguyên) ===================
void handle_knx_frame(const uint8_t byte) {
  ATOMIC_BLOCK_START();
  Rx_byte = byte;
  ATOMIC_BLOCK_END();
  set_knx_rx_flag_safe(true);
}


// =================== TASK HANDLERS ===================
TaskHandle_t taskKnxHandle = NULL;
TaskHandle_t taskUartHandle = NULL;
TaskHandle_t taskSysHandle = NULL;

// =================== TASK 1: KNX BUS ===================
void task_knx(void *pvParameters) {
  static bool pending_uart_send = false;
  static uint32_t checksum_rx_time = 0;
  static bool waiting_backoff = false;
  static uint32_t backoff_time = 0;

  for (;;) {
    // RX frame từ bus
    if (get_knx_rx_flag_safe()) {
      set_knx_rx_flag_safe(false);
      knx_parse_BUS_byte(Rx_byte);
    }

    // Khi nhận đủ frame → xử lý phản hồi
    if (is_rx_checksum_done()) {
      if (!pending_uart_send) {
        checksum_rx_time = micros();
        pending_uart_send = true;
      }

      if (is_get_echo_frame()) {
        if (micros() - checksum_rx_time >= 2600) {
          pending_uart_send = false;
          MCU_SERIAL.write(L_DATA_CON | SUCCESS);
          reset_echo_frame();
          reset_rx_checksum();
        }
      } else {
        if (micros() - checksum_rx_time >= 1300) {
          MCU_SERIAL.write(L_DATA_CON | SUCCESS);
          reset_rx_checksum();
          reset_echo_frame();
        }
      }
    }

    // KNX TX: gửi frame nếu queue có dữ liệu
    if (ATOMIC_QUEUE_READ_COUNT()) {
      if (!get_knx_rx_flag()) {
        if (!waiting_backoff) {
          backoff_time = millis() + random_num(5, 10);
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

    vTaskDelay(pdMS_TO_TICKS(1)); // nhường CPU 1ms
  }
}

// =================== TASK 2: UART ===================
void task_uart(void *pvParameters) {
  static uint32_t last_byte_time = 0;

  for (;;) {
    if (MCU_SERIAL.available()) {
      uint32_t now = micros();
      if (now - last_byte_time > 5)
        reset_tx_state();

      uint8_t b = MCU_SERIAL.read();
      knx_parse_MCU_byte(b);
      last_byte_time = now;
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

// =================== TASK 3: SYSTEM ===================
void task_system(void *pvParameters) {
  for (;;) {
    system_health_check();
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

// =================== SETUP ===================
void setup() {
  system_init();
  logger_init();
  LOG_INFO(LOG_CAT_SYSTEM, "KNX FreeRTOS Gateway started (STM32)");

  // Khởi tạo các task
  xTaskCreate(task_knx, "KNX", 512, NULL, 3, &taskKnxHandle);
  xTaskCreate(task_uart, "UART", 512, NULL, 2, &taskUartHandle);
  xTaskCreate(task_system, "SYS", 512, NULL, 1, &taskSysHandle);

  // Bắt đầu scheduler FreeRTOS
  vTaskStartScheduler();
}

// =================== LOOP ===================
void loop() {
  // Không dùng loop nữa vì FreeRTOS đã quản lý
}
