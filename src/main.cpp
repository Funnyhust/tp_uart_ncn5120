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

#define KNX_BUFFER_MAX_SIZE 23

uint16_t send_done_count=0;

// Forward declarations
uint8_t knx_calc_checksum(const uint8_t *data, uint8_t len);


// void send_test_frame() {
//   knx_send_frame(on_frame, on_frame_len);
//   delay(1000);
//   knx_send_frame(off_frame, off_frame_len);
//   delay(1000);
// }

// Cải thiện random function để tránh integer overflow
static uint64_t seed = 1;
uint8_t random_num(uint8_t a, uint8_t b) {
    // Sử dụng uint64_t để tránh overflow
    seed = (seed * 1664525ULL + 1013904223ULL) & 0xFFFFFFFFULL;
    return (seed % (b-a) + 1) + a;  // [2..10] - sửa lỗi logic
}


//UART
HardwareSerial DEBUG_SERIAL(USART3);
HardwareSerial MCU_SERIAL(USART1);

static uint8_t Uart_length = 0;
static uint8_t uart_rx_buf[KNX_BUFFER_MAX_SIZE];

//===============================================UART=========================
tpuart_state_t tpuart_state = TPUART_STATE_IDLE;

bool read_tpuart_frame() {
    static uint8_t index = 0;
    static uint32_t last_byte_time = 0;
    const uint32_t TIMEOUT_MS = 100; // Timeout 100ms

    while (MCU_SERIAL.available()) {
        uint8_t prefix = MCU_SERIAL.read();
        last_byte_time = millis();

        // Bounds check
        if (index >= KNX_BUFFER_MAX_SIZE - 1) {
            LOG_ERROR(LOG_CAT_UART, "TPUART Buffer overflow - resetting");
            index = 0;
            memset(uart_rx_buf, 0, sizeof(uart_rx_buf));
            return false;
        }

        // Chỉ nhận prefix hợp lệ
        if ((prefix & 0xF0) == 0x80 || (prefix & 0xF0) == 0x40) {
            // Kiểm tra byte data có sẵn
            if (!MCU_SERIAL.available()) {
                // Data chưa đến, chờ lần sau
                break;
            }
            uint8_t data = MCU_SERIAL.read();
            uart_rx_buf[index++] = data;

            // Nếu prefix là DataEnd → frame hoàn chỉnh
            if ((prefix & 0xF0) == 0x40) {
                // Có thể validate checksum ở đây
                frame_validation_result_t validation = validate_knx_frame(uart_rx_buf, index);
                if (validation == FRAME_VALID) {
                    index = 0;
                    return true; // frame hợp lệ
                } else {
                    LOG_ERROR(LOG_CAT_VALIDATION, "TPUART frame validation failed");
                    index = 0;
                    memset(uart_rx_buf, 0, sizeof(uart_rx_buf));
                    return false;
                }
            }
        } else {
            LOG_ERROR(LOG_CAT_UART, "Invalid TPUART prefix: 0x%02X", prefix);
        }
    }

    // Timeout reset
    if (index > 0 && (millis() - last_byte_time) > TIMEOUT_MS) {
        LOG_ERROR(LOG_CAT_UART, "TPUART Frame timeout - resetting");
        index = 0;
        memset(uart_rx_buf, 0, sizeof(uart_rx_buf));
    }

    return false;
}


//===============================================

//KNX-TX với Echo ACK mechanism
  static uint32_t backoff_time = 0;
  static bool waiting_backoff = false;
  
  // Frame tracking cho echo ACK
  struct PendingFrame {
    uint8_t data[KNX_BUFFER_MAX_SIZE];
    uint8_t len;
    uint32_t send_time;
    uint8_t retry_count;
    bool waiting_ack;
  };
  
  static PendingFrame pending_frame;

//KNX-RX ========================================================================================================================================
static uint8_t knx_rx_buf[KNX_BUFFER_MAX_SIZE];
static volatile bool knx_rx_flag=false;
static volatile bool knx_is_receved = false;
static volatile uint8_t knx_rx_length = 0;
static volatile uint8_t byte_idx = 0;

// Thread-safe access functions
bool get_knx_rx_flag_safe(void) {
    return ATOMIC_READ(knx_rx_flag);
}

void set_knx_rx_flag_safe(bool value) {
    ATOMIC_WRITE(knx_rx_flag, value);
}

//===============CheckSum=================
uint8_t knx_calc_checksum(const uint8_t *data, uint8_t len) {
  uint8_t cs = 0;
  for (uint8_t i = 0; i < len - 1; i++) { // không tính byte cuối
    cs ^= data[i];
  }
  return ~cs;
} 

//===============Frame Comparison=================
bool compare_frames(const uint8_t *frame1, uint8_t len1, const uint8_t *frame2, uint8_t len2) {
  if (len1 != len2) return false;
  
  for (uint8_t i = 0; i < len1; i++) {
    if (frame1[i] != frame2[i]) return false;
  }
  return true;
}

//===============Echo ACK Handler=================
void handle_echo_ack(const uint8_t *rx_frame, uint8_t rx_len) {
  if (!pending_frame.waiting_ack) {
    return; // Không có frame đang chờ ACK
  }
  
  // So sánh frame nhận được với frame đã gửi
  if (compare_frames(pending_frame.data, pending_frame.len, rx_frame, rx_len)) {
    // Echo ACK thành công!
    //LOG_INFO(LOG_CAT_ECHO_ACK, "Echo ACK received - transmission successful");
    //LOG_HEX_DEBUG(LOG_CAT_ECHO_ACK, "Echo ACK frame", rx_frame, rx_len);
    send_done_count++;
    pending_frame.waiting_ack = false;
  }
}
//============================================


#if KNX_RX_MODE //Gửi cả Frame
static uint32_t last_byte_time = 0;
void handle_knx_frame(const uint8_t byte) {
  uint32_t now = micros();
  
  // Timeout check với atomic access
  if ((now - last_byte_time) > KNX_FRAME_TIMEOUT_US) {
    ATOMIC_BLOCK_START();
    knx_rx_length = 0;
    memset((void*)knx_rx_buf, 0, sizeof(knx_rx_buf));
    byte_idx = 0;
    ATOMIC_BLOCK_END();
  }
  last_byte_time = now;

  // Bounds checking an toàn
  if (byte_idx >= KNX_BUFFER_MAX_SIZE - 1) {
    // LOG_ERROR(LOG_CAT_KNX_RX, "KNX RX buffer overflow - resetting");
    ATOMIC_BLOCK_START();
    knx_rx_length = 0;
    memset((void*)knx_rx_buf, 0, sizeof(knx_rx_buf));
    byte_idx = 0;
    ATOMIC_BLOCK_END();
    return;
  }

  // Lưu byte an toàn
  ATOMIC_BLOCK_START();
  knx_rx_buf[byte_idx++] = byte;
  ATOMIC_BLOCK_END();

  // Tính độ dài frame khi đã có ít nhất 6 byte
  if (byte_idx >= 6) {
    uint8_t data_length = knx_rx_buf[5] & 0x0F;
    uint8_t calculated_length = 6 + data_length + 1 + 1;
    
    // Validation độ dài frame
    if (calculated_length > KNX_BUFFER_MAX_SIZE || data_length > 15) {
      // LOG_ERROR(LOG_CAT_KNX_RX, "Invalid KNX frame length: %d", calculated_length);
      ATOMIC_BLOCK_START();
      knx_rx_length = 0;
      memset((void*)knx_rx_buf, 0, sizeof(knx_rx_buf));
      byte_idx = 0;
      ATOMIC_BLOCK_END();
      return;
    }
    
    knx_rx_length = calculated_length;
    
    if (byte_idx == knx_rx_length) {
      // Comprehensive frame validation
      frame_validation_result_t validation = validate_knx_frame(knx_rx_buf, knx_rx_length);
      if (validation == FRAME_VALID) {
        // Check if this is an echo ACK của frame đã gửi
        handle_echo_ack(knx_rx_buf, knx_rx_length);
        set_knx_rx_flag_safe(true);
      } else {
        // LOG_ERROR(LOG_CAT_VALIDATION, "KNX Frame validation failed: %s", 
        //          frame_validation_error_to_string(validation));
        // LOG_HEX_DEBUG(LOG_CAT_KNX_RX, "Invalid KNX frame", knx_rx_buf, knx_rx_length);
        // Reset buffer sau khi báo lỗi
        ATOMIC_BLOCK_START();
        knx_rx_length = 0;
        memset((void*)knx_rx_buf, 0, sizeof(knx_rx_buf));
        byte_idx = 0;
        ATOMIC_BLOCK_END();
      }
    }
  }
}

#else // Gửi Byte
static uint8_t Rx_byte=0;
static uint32_t last_byte_time = 0;
void handle_knx_frame(const uint8_t byte) {
 uint32_t now = micros();
  
  // Timeout check với atomic access
  if ((now - last_byte_time) > KNX_FRAME_TIMEOUT_US) {
    ATOMIC_BLOCK_START();
    knx_rx_length = 0;
    byte_idx = 0;
    ATOMIC_BLOCK_END();
  }
  last_byte_time = now;
  // Bounds checking an toàn
  if (byte_idx >= KNX_BUFFER_MAX_SIZE - 1) {
    ATOMIC_BLOCK_START();
    knx_rx_length = 0;
    memset((void*)knx_rx_buf, 0, sizeof(knx_rx_buf));
    byte_idx = 0;
    ATOMIC_BLOCK_END();
    return;
  }


  // Lưu byte an toàn
  ATOMIC_BLOCK_START();
  Rx_byte = byte;
  ATOMIC_BLOCK_END();

  // Tính độ dài frame khi đã có ít nhất 6 byte
  if (byte_idx == 5) {
    uint8_t data_length = byte & 0x0F;
    uint8_t calculated_length = 6 + data_length + 1 + 1;
    knx_rx_length = calculated_length;
    }
  if (byte_idx == knx_rx_length) {
  // Comprehensive frame validation
    ATOMIC_BLOCK_START();
    knx_rx_length = -1;
    byte_idx = 0;
    ATOMIC_BLOCK_END();
   }
  set_knx_rx_flag_safe(true);
  }

#endif
//==========================================================================================================================================
// ================== QUEUE =====================


// Hàm thêm frame vào queue - Cải thiện memory safety


void HC_test() {
  MCU_SERIAL.write(0xAB);
  delay(100);
}

void setup() {
   system_init();
   logger_init();
   LOG_INFO(LOG_CAT_SYSTEM, "KNX Gateway started - Logger initialized");

}// Loop-----------------------------------------------------------------
void loop() {
  #if KNX_RX_MODE
  if(get_knx_rx_flag_safe()) {
    set_knx_rx_flag_safe(false);
    //Serial3.printf("Serial OK after KNX receiver\r\n");
    MCU_SERIAL.write(knx_rx_buf, knx_rx_length);
  }
#else
  if(get_knx_rx_flag_safe()) {
    set_knx_rx_flag_safe(false);
    MCU_SERIAL.write(Rx_byte); // Gửi byte nhận được qua Serial để kiểm tra
  }
#endif
  // Nhận frame từ UART → bỏ vào queue
  if (read_tpuart_frame()) {
     uint8_t b = MCU_SERIAL.read();
     //Parse từng byte để thêm vào queue
     knx_tx_parse_tpuart(b);
  }
  // Nếu KNX đang rảnh → lấy frame từ queue ra gửi


if (ATOMIC_QUEUE_READ_COUNT() > 0 && !pending_frame.waiting_ack) {
  // Double-check bus status trước khi gửi
  if (!get_knx_rx_flag()) {
    if (!waiting_backoff) {
      // Bắt đầu backoff ngẫu nhiên
      backoff_time = millis() + random_num(5,10);// 2-10ms
      waiting_backoff = true;
    }
    else if (millis() >= backoff_time) {
      // Final check trước khi gửi
      if (!get_knx_rx_flag()) {
        // Backoff xong, gửi frame
        Frame f;
        LOG_HEX_DEBUG(LOG_CAT_KNX_TX, "Start dequeue", nullptr, 0);

        if (dequeue_frame(&f)) {
          // for(int i=0; i<f.len; i++){
            LOG_HEX_DEBUG(LOG_CAT_KNX_TX, "Sent frame", f.data, f.len);
          // }
          
          knx_error_t result = knx_send_frame(f.data, f.len);
          LOG_DEBUG(LOG_CAT_KNX_TX, "knx_send_frame done, %d", result);

          // if (result == KNX_OK) {
            // Gửi thành công - lưu frame để chờ echo ACK
            memcpy(pending_frame.data, f.data, f.len);
            pending_frame.len = f.len;
            pending_frame.send_time = millis();
            pending_frame.retry_count = 0;
            pending_frame.waiting_ack = true;
        }
      } else {
        LOG_DEBUG(LOG_CAT_KNX_TX, "Bus became busy during backoff - retrying");
      }
      waiting_backoff = false;
    }
  } else {
    // Nếu bus bận trong lúc chờ → reset backoff
    waiting_backoff = false;
  }
  }
  
  // Check echo ACK timeout và retry
  if (pending_frame.waiting_ack) {
    uint32_t now = millis();
    uint32_t timeout = 7; // 100ms timeout cho echo ACK
    
    if (now - pending_frame.send_time > timeout) {
      // Echo ACK timeout - cần retry
      
      if (pending_frame.retry_count < 3) {
        if (!get_knx_rx_flag()) {
           if (!waiting_backoff) {
      // Bắt đầu backoff ngẫu nhiên
            backoff_time = millis() + random_num(5,10);// 2-10ms
            waiting_backoff = true;
          }
          else if (millis() >= backoff_time) {
            // Final check trước khi gửi
            if (!get_knx_rx_flag()) {
                pending_frame.retry_count++;
                knx_send_frame(pending_frame.data, pending_frame.len);
            } else {
              LOG_DEBUG(LOG_CAT_KNX_TX, "Bus became busy during backoff - retrying");
            }
            waiting_backoff = false;
          }
        } else {
          // Nếu bus bận trong lúc chờ → reset backoff
          waiting_backoff = false;
        }
      } else {
        // Max retries reached - drop frame
        LOG_ERROR(LOG_CAT_ECHO_ACK, "Max echo ACK retries reached - dropping frame");
        LOG_HEX_DEBUG(LOG_CAT_ECHO_ACK, "Dropped frame", pending_frame.data, pending_frame.len);
        pending_frame.waiting_ack = false;
      }
    }
  }
  // System health check and watchdog reload
  // Debug send done count mỗi 2 giây
  // static uint32_t last_debug_time = 0;
  // if (millis() - last_debug_time >= 500) {
  //   last_debug_time = millis();
  //   LOG_INFO(LOG_CAT_KNX_TX, "%u", send_done_count);
  // }
  system_health_check();
}