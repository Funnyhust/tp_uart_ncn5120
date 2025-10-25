#include "tpuart.h"
#include <Arduino.h>
#include "config.h"
//#include "knx_rx.h"
#include "logger.h"
#include "tpuart/tpuart.h"

//Biến, buffer dùng chung TX
static uint8_t tx_buffer[KNX_MAX_FRAME_LEN];
static uint8_t tx_buf_idx = 0;
static bool tx_frame_complete=false;
static bool is_echo_frame = false;

//Biến, buffer dùng chung RX
static uint8_t rx_buf_idx = 0;
static uint8_t rx_buf_len = 0;
static bool rx_checksum_byte=false;


Frame queue[MAX_QUEUE];
volatile uint8_t q_head = 0;
volatile uint8_t q_tail = 0;
volatile uint8_t q_count = 0;

bool enqueue_frame(const uint8_t *data, uint8_t len) {
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


// TX STATE


tpuart_tx_state_t parse_tx_state = TPUART_TX_IDLE;

void set_echo_frame(bool is_echo) {
    is_echo_frame = is_echo;
}
void reset_echo_frame() {
    is_echo_frame = false;
}
bool is_get_echo_frame() {
    return is_echo_frame; 
}


void knx_parse_MCU_byte(uint8_t byte) {
    switch (parse_tx_state) {
        case TPUART_TX_IDLE:
            if (byte & U_L_DATA_START_REQ) { // example: start of frame (high bit set)
                tx_buf_idx = 0;
                parse_tx_state = TPUART_TX_CTRL;
            }
            break;
        case TPUART_TX_CTRL:
            tx_buffer[tx_buf_idx++] = byte;
            parse_tx_state = TPUART_TX_CONT;
            break;
        case TPUART_TX_DATA:
            tx_buffer[tx_buf_idx++] = byte;
            parse_tx_state = TPUART_TX_CONT;
            break;  
        case TPUART_TX_CONT:
            if ((byte ^ U_L_DATA_CONT_REQ)==tx_buf_idx) { // example: byte count matches        
                parse_tx_state = TPUART_TX_DATA;
            } else if((byte^U_L_DATA_END_REQ == tx_buf_idx)){ // example: end of frame (bit7)
                parse_tx_state = TPUART_TX_CHECKSUM;
            }
            break;
        case TPUART_TX_CHECKSUM:
            tx_buffer[tx_buf_idx++] = byte;
            parse_tx_state = TPUART_TX_END;
            break;
        case TPUART_TX_END:
            enqueue_frame(tx_buffer, tx_buf_idx);
            parse_tx_state = TPUART_TX_IDLE;
            break;
    }
}
void reset_tx_state() {
    parse_tx_state = TPUART_TX_IDLE;
    tx_buf_idx = 0;
}




tpuart_rx_state_t parse_rx_state = TPUART_RX_IDLE;
void knx_parse_BUS_byte(uint8_t byte) {
    switch (parse_rx_state) {
        case TPUART_RX_IDLE:
        //Control byte
            rx_buf_idx ++;
            MCU_SERIAL.write(byte);
            parse_rx_state = TPUART_RX_DATA;
            break;

        case TPUART_RX_DATA:
        //Data byte từ byte 1 đến byte checksum
            if(rx_buf_idx == 5) {
                // Lấy độ dài dữ liệu từ byte thứ 5
                uint8_t data_length = byte & 0x0F;
                uint8_t calculated_length = 6 + data_length + 1 + 1;
                rx_buf_len = calculated_length;
            }
            if(rx_buf_idx == rx_buf_len-1) {
                parse_rx_state = TPUART_RX_CHECKSUM;
            } else {
                rx_buf_idx ++;
                MCU_SERIAL.write(byte);
                parse_rx_state = TPUART_RX_DATA;
            }
            break;
        case TPUART_RX_CHECKSUM:
            //Checksum byte
            MCU_SERIAL.write(byte);
            rx_checksum_byte = true;
            parse_rx_state = TPUART_RX_ACK;
            break;
        case TPUART_RX_ACK:
            // Xử lý để sau
            
            parse_rx_state = TPUART_RX_IDLE;
            break;
    }
}
void reset_rx_state() {
    parse_rx_state = TPUART_RX_IDLE;
    rx_buf_idx = 0;
    rx_buf_len = 0;
}

bool is_rx_checksum_byte() {
    return rx_checksum_byte;
}


