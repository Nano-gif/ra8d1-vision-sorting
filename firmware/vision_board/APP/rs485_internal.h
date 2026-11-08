/*
 * 软件 RS485（GPIO 模拟 UART）模块内部头文件。
 *
 * 仅供 rs485_app.c（对外接口、接收环形缓冲、后台线程）、
 * rs485_phy.c（DWT 定时、GPIO、逐位收发与中断/定时器接收）
 * 和 rs485_cmd.c（msh 调试命令）之间共享，其他模块请只包含 rs485_app.h。
 */
#ifndef RS485_INTERNAL_H
#define RS485_INTERNAL_H

#include <rtthread.h>
#include <stdint.h>

#include "rs485_app.h"

#define RS485_APP_TX_PIN                  BSP_IO_PORT_08_PIN_01
#define RS485_APP_RX_IRQ_PIN              BSP_IO_PORT_00_PIN_09
#define RS485_APP_DE_PIN                  BSP_IO_PORT_00_PIN_07
#define RS485_APP_TX_MASK                 (1U << 1)
#define RS485_APP_RX_IRQ_MASK             (1U << 9)
#define RS485_APP_DE_MASK                 (1U << 7)
/*
 * 维持 57600：本软件模拟 UART 的接收采样是按 57600 专门调过的（见下方
 * IRQ_RX_BURST_BAUD_MID=57600 分支）。实测降到 19200 会落入 BURST_LOW 那条
 * 未调好的接收路径，反而更不稳、停车偶发超级慢。故波特率保持 57600 不动，
 * 稳定性改由“连发幂等停车”（zdt_motor_stop_fast）解决。
 */
#define RS485_APP_DEFAULT_BAUD            57600U
#define RS485_APP_MIN_BAUD                1200U
#define RS485_APP_MAX_BAUD                115200U
#define RS485_APP_READ_CHUNK_SIZE         64U
#define RS485_APP_DEFAULT_TIMEOUT_MS      500U
#define RS485_APP_DE_ASSERT_DELAY_US      10U
#define RS485_APP_DE_RELEASE_DELAY_US     20U
#define RS485_APP_RX_RING_SIZE            512U
#define RS485_APP_SERVICE_STACK_SIZE      2048U
#define RS485_APP_SERVICE_PRIORITY        24U
#define RS485_APP_SERVICE_TIMESLICE       5U
#define RS485_APP_SERVICE_DEFAULT_POLL_MS 50U
#define RS485_APP_SERVICE_DEFAULT_SLEEP_MS 100U
#define RS485_APP_FORWARD_STACK_SIZE      2048U
#define RS485_APP_FORWARD_PRIORITY        RS485_APP_SERVICE_PRIORITY
#define RS485_APP_FORWARD_TIMESLICE       5U
#define RS485_APP_FORWARD_INTERVAL_MS     50U
#define RS485_APP_TIMER_RX_DEFAULT_MS     50U
#define RS485_APP_TIMER_RX_SCAN_US        2U
#define RS485_APP_TIMER_RX_MAX_MS         60000U
#define RS485_APP_IRQ_RX_BURST_BAUD_HIGH  115200U
#define RS485_APP_IRQ_RX_BURST_BAUD_MID   57600U
#define RS485_APP_IRQ_RX_BURST_HIGH       16U
#define RS485_APP_IRQ_RX_BURST_MID        16U
#define RS485_APP_IRQ_RX_BURST_LOW        1U
#define RS485_APP_IRQ_RX_NEXT_START_BITS  2U

typedef struct
{
    uint32_t rx_bytes;
    uint32_t rx_errors;
    uint32_t rx_overflows;
    uint32_t rx_poll_rounds;
    uint32_t rx_timer_windows;
    uint32_t rx_timer_timeouts;
    uint32_t rx_timer_edges;
    uint32_t rx_irq_edges;
    uint32_t rx_irq_bytes;
    uint32_t rx_irq_ignored;
    uint32_t rx_irq_bursts;
    uint32_t rx_irq_max_burst;
    uint32_t tx_bytes;
    uint32_t tx_frames;
} rs485_app_stats_t;

/* 模块共享状态，定义见 rs485_app.c */
extern uint32_t g_rs485_baud;
extern uint32_t g_rs485_bit_cycles;
extern uint32_t g_rs485_irq_rx_max_burst;
extern uint32_t g_rs485_gpt_scan_counts;
extern uint16_t g_rs485_timer_bit_ticks_q8;
extern uint16_t g_rs485_timer_first_sample_ticks;
extern volatile rt_bool_t g_rs485_tx_busy;
extern volatile rt_bool_t g_rs485_service_running;
extern uint32_t g_rs485_service_poll_ms;
extern uint32_t g_rs485_service_sleep_ms;
extern volatile rt_bool_t g_rs485_forward_running;
extern volatile rt_uint16_t g_rs485_rx_count;
extern volatile rt_bool_t g_rs485_timer_opened;
extern volatile rt_bool_t g_rs485_timer_rx_running;
extern volatile rt_bool_t g_rs485_timer_rx_in_byte;
extern volatile uint32_t g_rs485_timer_rx_remaining_scans;
extern volatile uint32_t g_rs485_timer_irq_rounds;
extern volatile rt_bool_t g_rs485_irq_opened;
extern volatile rt_bool_t g_rs485_irq_enabled;
extern volatile rt_bool_t g_rs485_irq_rx_enabled;
extern volatile rt_bool_t g_rs485_irq_rx_busy;
extern rs485_app_stats_t g_rs485_stats;

/* rs485_app.c */
uint32_t rs485_app_ticks_to_ms(rt_tick_t ticks);
uint32_t rs485_app_parse_u32(const char *text, uint32_t default_value);
void rs485_app_stats_clear(void);
rt_size_t rs485_app_ring_write_byte_from_isr(rt_uint8_t byte);
rt_uint16_t rs485_app_ring_count(void);
int rs485_app_ensure_open(void);
int rs485_app_service_start(uint32_t poll_ms, uint32_t sleep_ms);
void rs485_app_service_stop(void);
int rs485_app_forward_start(void);
void rs485_app_forward_stop(void);

/* rs485_phy.c */
void rs485_app_cycle_counter_init(void);
void rs485_app_cycle_delay_us(uint32_t us);
void rs485_app_gpio_config(void);
void rs485_app_tx_set(rt_uint8_t level);
void rs485_app_de_set(rt_uint8_t level);
void rs485_app_softuart_tx_byte(rt_uint8_t value);
int rs485_app_softuart_rx_byte(rt_uint8_t *out_byte, uint32_t timeout_ms);
int rs485_app_timer_rx_start(uint32_t timeout_ms);
void rs485_app_timer_rx_stop(void);
int rs485_app_irq_rx_start(void);
void rs485_app_irq_rx_stop(void);

#endif /* RS485_INTERNAL_H */
