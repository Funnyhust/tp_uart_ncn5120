#include <Arduino.h>
#include "config.h"
#include "knx_tx.h"
#include "knx_rx.h"
#include "atomic_utils.h"
#include "system_utils.h"
#include "frame_validator.h"
#include "logger.h"
#include <IWatchdog.h>

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
HardwareSerial HC_SERIAL(USART1);

static uint8_t Uart_length = 0;
static uint8_t uart_rx_buf[KNX_BUFFER_MAX_SIZE];

//===============================================
bool read_uart_frame() {
  static uint8_t index = 0;
  static uint8_t total = 0;
  static uint32_t last_byte_time = 0;
  const uint32_t TIMEOUT_MS = 100; // Timeout 100ms
  
  while (HC_SERIAL.available()) {
    uint8_t byte = HC_SERIAL.read();
    last_byte_time = millis();
    
    // Bounds checking nghiêm ngặt
    if (index >= KNX_BUFFER_MAX_SIZE - 1) {
      // Buffer sắp đầy, reset an toàn
      LOG_ERROR(LOG_CAT_UART, "UART Buffer overflow - resetting");
      index = 0;
      total = 0;
      memset(uart_rx_buf, 0, sizeof(uart_rx_buf));
      return false;
    }
    
    uart_rx_buf[index++] = byte;
    
    // Kiểm tra độ dài frame khi có đủ 6 byte
    if (index >= 6 && total == 0) {
      uint8_t data_length = uart_rx_buf[5] & 0x0F;
      total = 6 + data_length + 1 + 1; // header + data + checksum
      
      // Validation độ dài frame
      if (total > KNX_BUFFER_MAX_SIZE || data_length > 15) {
        LOG_ERROR(LOG_CAT_UART, "Invalid frame length: %d", total);
        index = 0;
        total = 0;
        memset(uart_rx_buf, 0, sizeof(uart_rx_buf));
        return false;
      }
      Uart_length = total;
    }
    
    // Frame hoàn chỉnh
    if (total > 0 && index >= total) {
      // Comprehensive frame validation
      frame_validation_result_t validation = validate_knx_frame(uart_rx_buf, total);
      if (validation == FRAME_VALID) {
        index = 0;
        total = 0;
        return true;
      } else {
        LOG_ERROR(LOG_CAT_VALIDATION, "UART Frame validation failed: %s", 
                 frame_validation_error_to_string(validation));
          for (uint8_t i = 0; i < 23; i++) {
            LOG_INFO(LOG_CAT_UART, "%02X ", uart_rx_buf[i]);
        }
        LOG_HEX_DEBUG(LOG_CAT_UART, "Invalid UART frame", uart_rx_buf, total);
        
        index = 0;
        total = 0;
        memset(uart_rx_buf, 0, sizeof(uart_rx_buf));
        return false;
      }
    }
  }
  
  // Timeout check
  if (index > 0 && (millis() - last_byte_time) > TIMEOUT_MS) {
    LOG_ERROR(LOG_CAT_UART, "UART Frame timeout - resetting");
    index = 0;
    total = 0;
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
  static uint32_t now = micros();
  knx_is_receved =true;
  if(now -last_byte_time>180){
    knx_is_receved = false;
  }
  DEBUG_SERIAL.write(byte);
  last_byte_time=now;
//  Rx_byte = byte;
//  knx_rx_flag = true;
}
#endif
//==========================================================================================================================================
// ================== QUEUE =====================
#define MAX_QUEUE 50  // tối đa 5 frame chờ gửi
struct Frame {
  uint8_t data[KNX_BUFFER_MAX_SIZE];
  uint8_t len;
};

Frame queue[MAX_QUEUE];
volatile uint8_t q_head = 0, q_tail = 0;
volatile uint8_t q_count = 0;

// Hàm thêm frame vào queue - Cải thiện memory safety
bool enqueue_frame(uint8_t *data, uint8_t len) {
  // Validation đầu vào với flow control
  if (q_count >= MAX_QUEUE) {
    LOG_ERROR(LOG_CAT_QUEUE, "Queue full - frame dropped");
    return false;
  }
  
  // Send flow control signal: GO (if queue was nearly full)
  if (q_count >= MAX_QUEUE * 0.8) {
    LOG_WARN(LOG_CAT_QUEUE, "Queue nearly full - sending flow control signal");
  }
  
  if (data == nullptr) {
    LOG_ERROR(LOG_CAT_QUEUE, "Invalid data pointer");
    return false;
  }
  
  if (len == 0 || len > KNX_BUFFER_MAX_SIZE) {
    LOG_ERROR(LOG_CAT_QUEUE, "Invalid frame length: %d", len);
    return false;
  }
  
  // Copy an toàn với bounds checking
  memcpy(queue[q_tail].data, data, len);
  queue[q_tail].len = len;
  
  // Clear phần còn lại của buffer để tránh data leakage
  if (len < KNX_BUFFER_MAX_SIZE) {
    memset(queue[q_tail].data + len, 0, KNX_BUFFER_MAX_SIZE - len);
  }
  
  q_tail = (q_tail + 1) % MAX_QUEUE;
  q_count++;
  return true;
}

// Hàm lấy frame ra khỏi queue
bool dequeue_frame(Frame *f) {
  if (q_count == 0) return false;
  memcpy(f->data, queue[q_head].data, queue[q_head].len);
  f->len = queue[q_head].len;
  q_head = (q_head + 1) % MAX_QUEUE;
  q_count--;
  return true;
}

void HC_test() {
  HC_SERIAL.write(0xAB);
  delay(100);
}

void setup() {
   system_init();
   logger_init();
   LOG_INFO(LOG_CAT_SYSTEM, "KNX Gateway started - Logger initialized");
}

// Loop-----------------------------------------------------------------
void loop() {
  #if KNX_RX_MODE
  if(get_knx_rx_flag_safe()) {
    set_knx_rx_flag_safe(false);
    //Serial3.printf("Serial OK after KNX receiver\r\n");
    HC_SERIAL.write(knx_rx_buf, knx_rx_length);
  }
#else
  if(get_knx_rx_flag_safe()) {
    set_knx_rx_flag_safe(false);
    HC_SERIAL.write(Rx_byte); // Gửi byte nhận được qua Serial để kiểm tra
  }
#endif
  // Nhận frame từ UART → bỏ vào queue
  if (read_uart_frame()) {
    //Serial3.printf("Serial OK after UART receiver\r\n");
    enqueue_frame(uart_rx_buf, Uart_length);
    Uart_length = 0;
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
  static uint32_t last_debug_time = 0;
  if (millis() - last_debug_time >= 500) {
    last_debug_time = millis();
    LOG_INFO(LOG_CAT_KNX_TX, "%u", send_done_count);
  }
  
//   system_health_check();
//   //HC_test();
//  // IWatchdog.reload();
//  //send_test_frame();
//   if (!get_knx_rx_flag()) {
//         if (!waiting_backoff) {
//       // Bắt đầu backoff ngẫu nhiên
//             backoff_time = millis() + random_num(5,10);// 2-10ms
//             waiting_backoff = true;
//           }
//           else if (millis() >= backoff_time) {
//             // Final check trước khi gửi
//             if (!get_knx_rx_flag()) {
//                 knx_send_frame(test_frame, test_frame_len);
//             } else {
//               LOG_DEBUG(LOG_CAT_KNX_TX, "Bus became busy during backoff - retrying");
//             }
//             waiting_backoff = false;
//           }
//         } else {
//           // Nếu bus bận trong lúc chờ → reset backoff
//           waiting_backoff = false;
//        }
//        delay(90);
  system_health_check();
}



