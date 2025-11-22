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
static bool is_extended_frame = false; // Lưu loại frame (standard/extended)

// ACK handling: Lưu trạng thái cần gửi ACK và giá trị ACK từ MCU
static bool pending_ack = false; // Có cần gửi ACK xuống bus không, set true khi nhận được U_ACK_REQ từ MCU
static uint8_t ack_value = 0; // Giá trị ACK (U_ACK_REQ | flags)

void set_rx_checksum() {
    rx_checksum_byte = true;
}
void reset_rx_checksum() {
    rx_checksum_byte = false;
}
bool is_rx_checksum_done() {
    return rx_checksum_byte;
}


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

void set_echo_frame() {
    is_echo_frame = true;
}
void reset_echo_frame() {
    is_echo_frame = false;
}
bool is_get_echo_frame() {
    return is_echo_frame; 
}


/*
 * Parse byte từ MCU (tpuart_data_link_layer) gửi xuống
 * 
 * PROTOCOL TPUART (theo tpuart_data_link_layer.cpp):
 * Format: [U_L_DATA_START_REQ|pos] [data] [U_L_DATA_START_REQ|pos+1] [data] ... [U_L_DATA_END_REQ|pos] [checksum]
 * - Nếu frame > 64 byte: [U_L_DATA_OFFSET_REQ|offset] [U_L_DATA_START_REQ|pos] [data] ...
 * 
 * QUY TRÌNH:
 * 1. Nhận U_L_DATA_START_REQ | pos → chờ data byte
 * 2. Nhận data byte → lưu vào buffer
 * 3. Nhận U_L_DATA_START_REQ | pos+1 → chờ data byte tiếp theo
 * 4. Nhận U_L_DATA_END_REQ | pos → chờ checksum byte
 * 5. Nhận checksum byte → hoàn thành frame

 * Note: bản tin request_state F1 FF FF 02 gửi sang định kì để kiểm tra trạng thái connected
 Chip tpuart sẽ phản hồi lại 1 byte để trả lời, mcu sẽ ghi nhận là connected 
 Nếu sau ghép vào 1 module thì không cần làm phần này, tại vì code cùng chạy trong 1 chip
 Tạm thời bỏ qua, không cần đến
 */
void knx_parse_MCU_byte(uint8_t byte) {
    switch (parse_tx_state) {
        case TPUART_TX_IDLE:
            if (byte == U_L_DATA_START_REQ) { // example: start of frame (high bit set)
                tx_buf_idx = 0;
                //DEBUG_SERIAL.println(1);
                parse_tx_state = TPUART_TX_CTRL;
               // DEBUG_SERIAL.print(1);
                break;
            }
            else if ((byte &0xF0) == U_ACK_REQ) {
                ack_value = byte&0x0F;
                if(ack_value) {
                pending_ack = true;
                }
            }
           // DEBUG_SERIAL.print(3);
            break;
        case TPUART_TX_CTRL:
            tx_buffer[tx_buf_idx++] = byte;
            parse_tx_state = TPUART_TX_CONT;
            //DEBUG_SERIAL.print(4);
            break;
        case TPUART_TX_DATA:
          //  DEBUG_SERIAL.print(5);
            tx_buffer[tx_buf_idx++] = byte;
            parse_tx_state = TPUART_TX_CONT;
            break;  
        case TPUART_TX_CONT:
            if((byte & 0xF0) == U_L_DATA_CONT_REQ && ((byte & 0x3F)==tx_buf_idx)){ // example: continue of frame (high bit set)    
                parse_tx_state = TPUART_TX_DATA;
            } else if(((byte & 0xF0) == U_L_DATA_END_REQ) && ((byte & 0x3F)==tx_buf_idx)){ // example: end of frame (bit7)
                parse_tx_state = TPUART_TX_CHECKSUM;
            }
            else {
                // Invalid byte in CONT state, reset
                reset_tx_state();
                parse_tx_state = TPUART_TX_IDLE;
            }
            break;
        case TPUART_TX_CHECKSUM:
            tx_buffer[tx_buf_idx++] = byte;
            enqueue_frame(tx_buffer, tx_buf_idx);
           // DEBUG_SERIAL.print("Enqueued frame: ");
            for(int i=0; i<tx_buf_idx; i++) {
              //DEBUG_SERIAL.print(tx_buffer[i], HEX);
            }
            //DEBUG_SERIAL.println();

            set_echo_frame();
            reset_tx_state();
            parse_tx_state = TPUART_TX_IDLE;
            break;
        case TPUART_TX_END:
            break;
    }
}
//
// ACK handling functions

bool is_pending_ack() {
    return pending_ack;
}

uint8_t get_ack_value() {
    return ack_value;
}

void reset_pending_ack() {
    pending_ack = false;
    ack_value = 0;
}



// TX STATE functions
void reset_tx_state() {
    parse_tx_state = TPUART_TX_IDLE;
    tx_buf_idx = 0;
    memset(tx_buffer, 0, sizeof(tx_buffer));
}




/*
 * Parse byte từ bus KNX nhận được và forward lên MCU
 * 
 * PROTOCOL TPUART (forward lên MCU):
 * - L_DATA_STANDARD_IND (0x90) hoặc L_DATA_EXTENDED_IND (0x10): Control byte đầu
 * - Data bytes: Tất cả byte từ frame (bao gồm control byte đầu)
 * - L_ACKN_IND, L_DATA_CON, etc.: Các control byte khác
 * 
 * QUY TRÌNH:
 * 1. Phát hiện L_DATA_STANDARD_IND/L_DATA_EXTENDED_IND → bắt đầu frame
 * 2. Forward tất cả byte lên MCU
 * 3. Tính toán độ dài frame từ byte thứ 5 (standard) hoặc byte thứ 6 (extended)
 * 4. Khi nhận đủ frame → set checksum done
 * 5. Nếu là echo frame → gửi L_DATA_CON | SUCCESS
 */
tpuart_rx_state_t parse_rx_state = TPUART_RX_IDLE;
void knx_parse_BUS_byte(uint8_t byte) {
    switch (parse_rx_state) {
        case TPUART_RX_IDLE:
            // Kiểm tra control byte đầu: L_DATA_STANDARD_IND hoặc L_DATA_EXTENDED_IND
            if ((byte & L_DATA_MASK) == L_DATA_STANDARD_IND) {
                rx_buf_idx = 1; // Bắt đầu từ byte 1 (đã có control byte)
                rx_buf_len = 0; // Chưa biết độ dài
                is_extended_frame = false; // Standard frame
                MCU_SERIAL.write(byte); // Forward control byte đầu
                parse_rx_state = TPUART_RX_DATA;
               // DEBUG_SERIAL.write(0XAA);
            } else if ((byte & L_DATA_MASK) == L_DATA_EXTENDED_IND) {
                rx_buf_idx = 1;
                rx_buf_len = 0;
                is_extended_frame = true; // Extended frame
                MCU_SERIAL.write(byte); // Forward control byte đầu
                parse_rx_state = TPUART_RX_DATA;
              //  DEBUG_SERIAL.write(0XBB);
            } else {
                // Các control byte khác (L_ACKN_IND, L_DATA_CON, etc.) → forward ngay
                MCU_SERIAL.write(byte);

            }
            break;

        case TPUART_RX_DATA:
            // Forward data byte lên MCU
            MCU_SERIAL.write(byte);
            rx_buf_idx++;
            
            // Tính toán độ dài frame khi đã có đủ thông tin
            // Format TP frame: header (8/9) + payload + checksum (1)
            // rx_buf_idx bắt đầu từ 1 (sau control byte đầu), nên cần nhận thêm header + payload byte
            if (rx_buf_len == 0) {
                if (!is_extended_frame && rx_buf_idx == 6) {
                    // Standard frame: byte 5 (index 5, tức byte thứ 6 của frame) chứa payload length (4 bit thấp)
                    // Header = 8 byte, payload từ byte này
                    uint8_t payload_length = byte & 0x0F;
                    rx_buf_len = 8 + payload_length; // Header (8) + payload (không tính checksum)
                } else if (is_extended_frame && rx_buf_idx == 7) {
                    // Extended frame: byte 6 (index 6, tức byte thứ 7 của frame) chứa payload length
                    // Header = 9 byte, payload từ byte này
                    uint8_t payload_length = byte;
                    rx_buf_len = 9 + payload_length; // Header (9) + payload (không tính checksum)
                }
            }
            
            // Kiểm tra đã nhận đủ frame chưa (trừ checksum)
            // rx_buf_len = header + payload, cần thêm 1 byte checksum

            //Note: cần check lại length xem chuyển state ở đoạn nào
            if (rx_buf_len > 0 && rx_buf_idx >= rx_buf_len - 1) {
                // Đã nhận đủ data, byte tiếp theo là checksum
                parse_rx_state = TPUART_RX_CHECKSUM;
            }
            break;
            
        case TPUART_RX_CHECKSUM:
            // Checksum byte - forward lên MCU
            MCU_SERIAL.write(byte);
            set_rx_checksum();
            rx_checksum_byte = true;
            
            // Kiểm tra xem có phải echo frame không
            if (is_get_echo_frame()&& pending_ack) {
                parse_rx_state = TPUART_RX_END_ECHO;
            } else {
                parse_rx_state = TPUART_RX_ACK;
            }
            break;
            
        case TPUART_RX_ACK:
            // Frame hoàn thành (không phải echo)
            reset_pending_ack();
            reset_rx_state();
            parse_rx_state = TPUART_RX_IDLE;
            break;
            
        case TPUART_RX_END_ECHO:
            // Frame echo hoàn thành → gửi L_DATA_CON | SUCCESS lên MCU
            reset_rx_state();
            reset_echo_frame(); // Reset echo flag
            MCU_SERIAL.write(L_DATA_CON | SUCCESS);
            parse_rx_state = TPUART_RX_IDLE;
            break;
    }
}
void reset_rx_state() {
    parse_rx_state = TPUART_RX_IDLE;
    rx_buf_idx = 0;
    rx_buf_len = 0;
    is_extended_frame = false;
}


