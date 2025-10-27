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

//===============================================

//KNX-TX với Echo ACK mechanism
  static uint32_t backoff_time = 0;
  static bool waiting_backoff = false;

//KNX-RX ===============================================================================================================================
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




//============================================


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

//==========================================================================================================================================

void setup() {
   system_init();
   logger_init();
   LOG_INFO(LOG_CAT_SYSTEM, "KNX Gateway started - Logger initialized");
}// Loop-----------------------------------------------------------------


void loop() {

//KNX RX from bus===================================================
  #if KNX_RX_MODE
  if(get_knx_rx_flag_safe()) {
    set_knx_rx_flag_safe(false);
    //Serial3.printf("Serial OK after KNX receiver\r\n");
    MCU_SERIAL.write(knx_rx_buf, knx_rx_length);
  }
#else
  if(get_knx_rx_flag_safe()) {
    set_knx_rx_flag_safe(false);
    knx_parse_BUS_byte(Rx_byte);
  }
#endif


static bool pending_uart_send = false;
static uint32_t checksum_rx_time = 0;
// Checksumdone xử lý tiếp
  if(is_rx_checksum_done()) {
    if(!pending_uart_send) {
      checksum_rx_time = micros();
      pending_uart_send = true;
    }
    if(micros() - checksum_rx_time >= 2600) {
      pending_uart_send = false;
      //-------------------------
    if(is_get_echo_frame()){
      // Đây là echo frame
      MCU_SERIAL.write(L_DATA_CON);
      reset_echo_frame();
      reset_rx_checksum();
    } else {
      // Đây là frame thực sự từ bus
      reset_rx_checksum();
      Frame f;
      if(dequeue_frame(&f)) {
        LOG_HEX_DEBUG(LOG_CAT_KNX_RX, "Received frame from bus", f.data, f.len);
        knx_parse_BUS_byte(f.data[0]); // Gọi hàm xử lý frame
      } else {
        LOG_DEBUG(LOG_CAT_KNX_RX, "No frame in queue to process after checksum done");
      }
    }
    //-------------------------
  }

  }


//KNX TX to bus============================================
  // Nhận frame từ UART → bỏ vào queue
  if (MCU_SERIAL.available()) {
     uint8_t b = MCU_SERIAL.read();
     //Parse từng byte để thêm vào queue
     knx_parse_MCU_byte(b);
  }
  // Nếu KNX đang rảnh → lấy frame từ queue ra gửi


if (ATOMIC_QUEUE_READ_COUNT()) {
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


  system_health_check();
}