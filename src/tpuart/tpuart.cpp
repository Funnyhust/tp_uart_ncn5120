#include "tpuart.h"
#include <Arduino.h>
#include "config.h"
#include "knx_rx.h"
#include "logger.h"
#include <IWatchdog.h>
#include "tpuart/tpuart.h"


static uint8_t rx_buffer[KNX_MAX_FRAME_LEN];
static uint8_t tx_buf_idx = 0;

Frame queue[MAX_QUEUE];
volatile uint8_t q_head = 0;
volatile uint8_t q_tail = 0;
volatile uint8_t q_count = 0;

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
// int parse_TPUART_frame(uint8_t *in, int len_in, uint8_t *buffer_out) {
//     int out_idx = 0;
//     for (int i = 0; i < len_in - 1; i++) {
//         uint8_t prefix = in[i];
//         uint8_t data = in[i + 1];

//         // Mỗi prefix phải là 0x80..0x8F hoặc 0x40..0x4F
//         if ((prefix & 0xF0) == 0x80 || (prefix & 0xF0) == 0x40) {
//             buffer_out[out_idx++] = data;

//             // Nếu prefix = 0x40 -> kết thúc frame
//             if ((prefix & 0xF0) == 0x40)
//                 break;
//             i++; // skip next since it's data
//         }
//     }
//     return out_idx;
// }


// TX STATE
tpuart_state_t parse_tx_state = TPUART_STATE_IDLE;


void knx_tx_parse_tpuart(uint8_t byte) {
    switch (parse_tx_state) {
        case TPUART_STATE_IDLE:
            if (byte & 0x80) { // example: start of frame (high bit set)
                tx_buf_idx = 0;
                parse_tx_state = TPUART_STATE_RX_CTRL;
            }
            break;
        case TPUART_STATE_RX_CTRL:
            rx_buffer[tx_buf_idx++] = byte;
            parse_tx_state = TPUART_STATE_RX_CONT;
            break;
        case TPUART_STATE_RX_DATA:
            rx_buffer[tx_buf_idx++] = byte;
            parse_tx_state = TPUART_STATE_RX_CONT;
            break;  
        case TPUART_STATE_RX_CONT:
            if ((byte ^ 0x80)==tx_buf_idx) { // example: end of frame (bit6)         
                parse_tx_state = TPUART_STATE_RX_DATA;
            } else if((byte^0x40 == tx_buf_idx)){ // example: end of frame (bit7)
                parse_tx_state = TPUART_STATE_RX_CHECKSUM;
            }
            break;
        case TPUART_STATE_RX_END:
            rx_buffer[tx_buf_idx++] = byte; // checksum byte
            enqueue_frame(rx_buffer, tx_buf_idx);
            parse_tx_state = TPUART_STATE_IDLE;
            break;
    }
}




