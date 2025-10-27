#ifndef TPUART_H
#define TPUART_H
#include <Arduino.h>
#include "config.h"

// internal commands, device specific
#define U_RESET_REQ 0x01
#define U_STATE_REQ 0x02
#define U_SET_BUSY_REQ 0x03
#define U_QUIT_BUSY_REQ 0x04
#define U_BUSMON_REQ 0x05
#define U_SET_ADDRESS_REQ 0xF1   // different on TP-UART
#define U_L_DATA_OFFSET_REQ 0x08 //-0x0C
#define U_SYSTEM_MODE 0x0D
#define U_STOP_MODE_REQ 0x0E
#define U_EXIT_STOP_MODE_REQ 0x0F
#define U_ACK_REQ 0x10 //-0x17
#define U_ACK_REQ_NACK 0x04
#define U_ACK_REQ_BUSY 0x02
#define U_ACK_REQ_ADRESSED 0x01
#define U_POLLING_STATE_REQ 0xE0

// Only on NCN51xx available
#ifdef NCN5120
    #define U_CONFIGURE_REQ 0x18
    #define U_CONFIGURE_MARKER_REQ 0x1
    #define U_CONFIGURE_CRC_CCITT_REQ 0x2
    #define U_CONFIGURE_AUTO_POLLING_REQ 0x4
    #define U_SET_REPETITION_REQ 0xF2
#else
    #define U_MXRSTCNT 0x24
#endif

// knx transmit data commands
#define U_L_DATA_START_REQ 0x80
#define U_L_DATA_CONT_REQ 0x80 //-0xBF
#define U_L_DATA_END_REQ 0x40  //-0x7F

// serices to host controller

// DLL services (device is transparent)
#define L_DATA_STANDARD_IND 0x90   //RX Start
#define L_DATA_EXTENDED_IND 0x10
#define L_DATA_MASK 0xD3
#define L_POLL_DATA_IND 0xF0

// acknowledge services (device is transparent in bus monitor mode)
#define L_ACKN_IND 0x00
#define L_ACKN_MASK 0x33
#define L_ACKN_BUSY_MASK 0x0C
#define L_ACKN_NACK_MASK 0xC0
#define L_DATA_CON 0x0B   // TX done
#define L_DATA_CON_MASK 0x7F
#define SUCCESS 0x80

// control services, device specific
#define U_RESET_IND 0x03
#define U_STATE_MASK 0x07
#define U_STATE_IND 0x07
#define SLAVE_COLLISION 0x80
#define RECEIVE_ERROR 0x40
#define TRANSMIT_ERROR 0x20
#define PROTOCOL_ERROR 0x10
#define TEMPERATURE_WARNING 0x08
#define U_FRAME_STATE_IND 0x13      //RX done
#define U_FRAME_STATE_MASK 0x17
#define PARITY_BIT_ERROR 0x80
#define CHECKSUM_LENGTH_ERROR 0x40
#define TIMING_ERROR 0x20
#define U_CONFIGURE_IND 0x0100000000000000000000000000
#define U_CONFIGURE_MASK 0x83
#define AUTO_ACKNOWLEDGE 0x20
#define AUTO_POLLING 0x10
#define CRC_CCITT 0x80
#define FRAME_END_WITH_MARKER 0x40
#define U_FRAME_END_IND 0xCB
#define U_STOP_MODE_IND 0x2B
#define U_SYSTEM_STAT_IND 0x4B

/*
 * NCN51xx Register handling
 */
// write internal registers
#define U_INT_REG_WR_REQ_WD 0x28
#define U_INT_REG_WR_REQ_ACR0 0x29
#define U_INT_REG_WR_REQ_ACR1 0x2A
#define U_INT_REG_WR_REQ_ASR0 0x2B
// read internal registers
#define U_INT_REG_RD_REQ_WD 0x38
#define U_INT_REG_RD_REQ_ACR0 0x39
#define U_INT_REG_RD_REQ_ACR1 0x3A
#define U_INT_REG_RD_REQ_ASR0 0x3B
// Analog Control Register 0 - Bit values
#define ACR0_FLAG_V20VEN 0x40
#define ACR0_FLAG_DC2EN 0x20
#define ACR0_FLAG_XCLKEN 0x10
#define ACR0_FLAG_TRIGEN 0x08
#define ACR0_FLAG_V20VCLIMIT 0x04

// TX State
typedef enum{
    TPUART_TX_IDLE,
    TPUART_TX_START,
    TPUART_TX_CTRL,
    TPUART_TX_CONT,
    TPUART_TX_LENGTH,
    TPUART_TX_DATA,
    TPUART_TX_CHECKSUM,
    TPUART_TX_END,
} tpuart_tx_state_t;

//RX State
typedef enum{
    TPUART_RX_IDLE,
    TPUART_RX_DATA,
    TPUART_RX_LENGTH,
    TPUART_RX_CHECKSUM,
    TPUART_RX_ACK,
    TPUART_RX_END,
    TPUART_RX_END_ECHO,


} tpuart_rx_state_t;


#define MAX_QUEUE 50  // tối đa 5 frame chờ gửi
struct Frame {
  uint8_t data[KNX_BUFFER_MAX_SIZE];
  uint8_t len;
};

extern Frame queue[MAX_QUEUE];
extern volatile uint8_t q_head;
extern volatile uint8_t q_tail;
extern volatile uint8_t q_count;


int parse_TPUART_frame(uint8_t *in, int len_in, uint8_t *buffer_out);

// TX STATE
void knx_parse_MCU_byte(uint8_t byte);

// Hàm thêm frame vào queue
bool enqueue_frame(const uint8_t *data, uint8_t len);
// Hàm lấy frame ra khỏi queue
bool dequeue_frame(Frame *f);

void reset_tx_state();
void set_tx_complete();
void reset_rx_state();
bool is_tx_complete();

// Echo frame handling
void set_echo_frame(bool is_echo);
bool is_get_echo_frame();
void reset_echo_frame();
// RX STATE
void knx_parse_BUS_byte(uint8_t byte);

void set_rx_checksum();
void reset_rx_checksum();
bool is_rx_checksum_done();

void reset_rx_state();
bool is_rx_idle();
//RX
#endif

