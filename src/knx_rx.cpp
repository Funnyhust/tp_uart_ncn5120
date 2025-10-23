#include "config.h"
#if KNX_RX_MODE


#include "knx_rx.h"
#include <Arduino.h>


#define BIT0_MIN_US 25
#define BIT0_MAX_US 55
#define KNX_MAX_FRAME_LEN 23


HardwareTimer timer(TIM2);

static uint8_t bit_idx = 0, byte_idx = 0, cur_byte = 0;
static volatile bool bit0 = false;
static uint8_t pulse_start = 0;
static knx_frame_callback_t callback_fn = nullptr;
static volatile uint8_t parity_bit = 0;
static volatile uint8_t received_frame[KNX_MAX_FRAME_LEN];
static volatile uint32_t last_rx_time = 0;


static volatile bool RX_flag=false;
static uint8_t total = 0;

bool get_knx_rx_flag(){
  // Kiểm tra multiple conditions để đảm bảo bus thực sự rảnh
  uint32_t now = millis();
  
  // 1. Kiểm tra thời gian từ lần cuối có activity
  if ((now - last_rx_time) < KNX_BUS_BUSY_TIMEOUT_MS) {
    return true; // Bus bận
  }
  
  // 2. Kiểm tra RX_flag (đang trong quá trình nhận)
  if (RX_flag) {
    return true; // Bus bận
  }
  
  // 3. Kiểm tra timer có đang chạy không
  if (timer.isRunning()) {
    return true; // Bus bận
  }
  
  return false; // Bus rảnh
}

// Sử dụng cùng hàm checksum với main.cpp để đảm bảo consistency
extern uint8_t knx_calc_checksum(const uint8_t *data, uint8_t len);
void knx_rx_init(knx_frame_callback_t cb) {
  timer.setPrescaleFactor((SystemCoreClock/1000000) -1);   // CK_CNT = 8MHz / (8+1) = 1MHz
  timer.setOverflow(104);       
  timer.attachInterrupt(knx_timer_tick);
 // timer2.setPrescaleFactor(63);   
  //timer2.attachInterrupt(knx_send_tick);
 // timer2.setOverflow(1); 
  attachInterrupt(digitalPinToInterrupt(KNX_TX_PIN), knx_exti_irq, CHANGE);
  pinMode(KNX_TX_PIN, INPUT);
  callback_fn = cb;
  bit_idx = byte_idx = cur_byte = 0;
  bit0 = false;
  pulse_start = 0;
}


void knx_exti_irq(void) {
  // Cập nhật last_rx_time ngay khi có bất kỳ thay đổi nào trên bus
  last_rx_time = millis();
  
  if(!RX_flag){
      RX_flag = true;
      timer.refresh();
      timer.resume(); // Bật lại timer để bắt đầu nhận dữ liệu
  }
  static uint8_t last = 0;

// Đọc mức logic tại PB6
  uint8_t lvl = (GPIOB->IDR & (1 << 6)) ? 1 : 0;
   //bước 1: Nếu là sườn lên -> lưu lại time điểm này bằng bộ đếm timer
  //Bước 2: sườn xuống -> tính khoảng thời gian từ lúc sườn lên đến sườn xuống và kiểm tra khoảng time thỏa mãn ko? Nếu có thì bit 0/1
  //bước 3: 
  uint8_t now = timer.getCount();
  if (lvl && !last) pulse_start = now;
  else if (!lvl && last) {
    uint8_t w = now >= pulse_start ? now - pulse_start :104-pulse_start + now;
    if (w >= BIT0_MIN_US && w <= BIT0_MAX_US){
         bit0 = true;
         // last_rx_time đã được cập nhật ở đầu hàm
    }   
  }
  last = lvl;
}


void reset_knx_receiver() {
  memset((void*)received_frame, 0, sizeof(received_frame));
  bit_idx = 0;
  //byte_idx = 0;
  total = 0;
  cur_byte = 0;
  bit0 = false;
  RX_flag= false;
}
void knx_timer_tick(void) {
  uint8_t bit = bit0 ? 0 : 1;
  bit0 = false;
  bit_idx++;
  if (bit_idx == 1) {
    cur_byte = 0;
    parity_bit = 0;
  } 
  else if (bit_idx >= 2 && bit_idx <= 9) {
    cur_byte >>= 1;
    if (bit) {
      cur_byte |= 0x80;
      parity_bit++;
    }
  } 
    else if (bit_idx == 10) {
    if ((parity_bit & 1) == bit) {
      return;
    }
  } 
  if (bit_idx == 11 && bit == 1) {
    if (callback_fn) callback_fn(cur_byte);
    cur_byte = 0;
    bit_idx = 0;
    byte_idx++;
    RX_flag = false;
    timer.pause();
  }
}
#endif
