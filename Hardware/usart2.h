#ifndef __USART2_H
#define __USART2_H

#include "stm32f10x.h"                  // Device header

/*==============================================================================
 * 说明：USART2 驱动（STM32F103 版）
 *
 * 用途：与 AS608 指纹模块通讯（你的 Serial.c/USART1 负责 printf 调试，
 *       这里 USART2 专门给指纹模块，两者互不干扰）。
 *
 * 接线：TX -> PA2（USART2_TX），RX -> PA3（USART2_RX）
 *       注意 USART2 挂在 APB1 总线（不是 APB2）。
 *
 * 接收机制：中断接收 + TIM4 定时器做"100ms 无新字节 = 一帧结束"的超时判帧，
 *           这是 AS608 应答包常用的判帧方式。
 *           注意用 TIM4 而非 TIM7：STM32F103C8（中容量）没有 TIM6/TIM7。
 *============================================================================*/

/* 收发缓冲大小 */
#define USART2_MAX_RECV_LEN   400     // 最大接收缓冲字节数
#define USART2_MAX_SEND_LEN   400     // 最大发送缓冲字节数

/* 接收状态寄存器 USART2_RX_STA 的位定义：
 *   bit15      ：1 = 已接收到一整批数据（等待上层处理）
 *   bit14 ~ 0  ：本次已接收到的字节数
 * 处理方读完数据后，把 USART2_RX_STA 清零即可重新接收。 */
extern uint8_t  USART2_RX_BUF[USART2_MAX_RECV_LEN];   // 接收缓冲
extern uint8_t  USART2_TX_BUF[USART2_MAX_SEND_LEN];   // 发送缓冲
extern uint16_t USART2_RX_STA;                        // 接收状态

/* 函数声明 */
void usart2_init(uint32_t bound);      // 初始化 USART2，bound 为波特率
void TIM4_Int_Init(uint16_t arr, uint16_t psc);   // TIM4 定时器初始化（超时判帧用）
void u2_printf(char *fmt, ...);        // 串口2 格式化输出（调试用，可选）

#endif
