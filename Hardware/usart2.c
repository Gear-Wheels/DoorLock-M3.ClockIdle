#include "stm32f10x.h"                  // Device header
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include "usart2.h"

/*==============================================================================
 * USART2 驱动（STM32F103 版，移植自正点原子 F4 的 usart2.c）
 *
 * 接收方式：中断接收 RXNE + TIM4 定时器 100ms 超时判帧。
 *   - 每收到一个字节，清空 TIM4 计数器并开启 TIM4；
 *   - 若 100ms 内没有新字节到达，TIM4 溢出中断，标记 USART2_RX_STA 的 bit15=1，
 *     表示"一帧接收完成"，供上层（as608.c）读取。
 * 注意：用的是 TIM4 而非 TIM7——STM32F103C8（中容量）没有 TIM6/TIM7，
 *       只有 TIM1~TIM4 通用定时器，故选 TIM4（APB1 总线）。
 *============================================================================*/

/* 接收缓冲（中断里写入） */
uint8_t  USART2_RX_BUF[USART2_MAX_RECV_LEN];
/* 发送缓冲（u2_printf 格式化输出用） */
uint8_t  USART2_TX_BUF[USART2_MAX_SEND_LEN];
/* 接收状态：bit15=一帧完成标志，bit14~0=已接收字节数 */
uint16_t USART2_RX_STA = 0;

/*------------------------------------------------------------------------------
 * 函    数：USART2 接收中断服务函数
 * 说    明：每收到一个字节进入一次。字节写入缓冲，同时重启 TIM4 计时。
 *           若缓冲已满（超出 MAX_RECV_LEN），强制标记 bit15=1 让上层尽快处理。
 * 注意事项：函数名必须与启动文件里的弱定义一致，否则中断进不来。
 *------------------------------------------------------------------------------*/
void USART2_IRQHandler(void)
{
    uint8_t res;

    /* 判断是否为"接收数据寄存器非空"触发的中断 */
    if (USART_GetITStatus(USART2, USART_IT_RXNE) != RESET)
    {
        res = USART_ReceiveData(USART2);        // 读出数据（读操作会自动清 RXNE）

        /* 只有上一批数据还没被处理完（bit15==0）时才继续收，避免覆盖 */
        if ((USART2_RX_STA & 0x8000) == 0)
        {
            if (USART2_RX_STA < USART2_MAX_RECV_LEN)   // 缓冲还没满
            {
                TIM_SetCounter(TIM4, 0);        // 清空超时定时器计数器
                if (USART2_RX_STA == 0)
                {
                    TIM_Cmd(TIM4, ENABLE);      // 收到第一个字节时启动超时定时器
                }
                USART2_RX_BUF[USART2_RX_STA++] = res;  // 存入缓冲，长度+1
            }
            else
            {
                USART2_RX_STA |= 0x8000;        // 缓冲满，强制标记"一帧完成"
            }
        }
    }
}

/*------------------------------------------------------------------------------
 * 函    数：TIM4 超时中断服务函数
 * 说    明：100ms 内无新字节到达，说明一帧结束，标记 bit15=1。
 *           随后关闭 TIM4，直到下一次收到字节再启动。
 *------------------------------------------------------------------------------*/
void TIM4_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM4, TIM_IT_Update) != RESET)   // 判断是否更新（溢出）中断
    {
        USART2_RX_STA |= 0x8000;            // 标记一帧接收完成
        TIM_ClearITPendingBit(TIM4, TIM_IT_Update);      // 清中断标志
        TIM_Cmd(TIM4, DISABLE);             // 关闭定时器，等待下次接收重启
    }
}

/*------------------------------------------------------------------------------
 * 函    数：初始化 TIM4，作为串口2接收超时判帧的定时器
 * 参    数：arr  自动重装载值（决定溢出周期）
 * 参    数：psc  预分频值
 * 说    明：溢出周期 = (arr+1)*(psc+1) / 72MHz。
 *           例如 arr=999、psc=7199 -> 100ms。
 *------------------------------------------------------------------------------*/
void TIM4_Int_Init(uint16_t arr, uint16_t psc)
{
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
    NVIC_InitTypeDef NVIC_InitStructure;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM4, ENABLE);   // 使能 TIM4 时钟（APB1）

    TIM_TimeBaseStructure.TIM_Period = arr;                 // 自动重装载值
    TIM_TimeBaseStructure.TIM_Prescaler = psc;              // 预分频值
    TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1; // 时钟不分频
    TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;  // 向上计数
    TIM_TimeBaseInit(TIM4, &TIM_TimeBaseStructure);

    TIM_ITConfig(TIM4, TIM_IT_Update, ENABLE);             // 使能更新（溢出）中断

    NVIC_InitStructure.NVIC_IRQChannel = TIM4_IRQn;         // 配置 TIM4 中断线
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    TIM_Cmd(TIM4, DISABLE);                                 // 初始不启动，等收到字节再开
}

/*------------------------------------------------------------------------------
 * 函    数：初始化 USART2
 * 参    数：bound  波特率（AS608 默认 57600）
 * 说    明：PA2=TX(复用推挽输出)，PA3=RX(浮空输入)，8N1，无硬件流控，
 *           开启 RXNE 接收中断，并初始化 TIM4 超时判帧。
 *------------------------------------------------------------------------------*/
void usart2_init(uint32_t bound)
{
    GPIO_InitTypeDef  GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;
    NVIC_InitTypeDef  NVIC_InitStructure;

    /* 1. 使能时钟：GPIOA(APB2) + USART2(APB1，注意总线！) */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);

    /* 2. GPIO 初始化：PA2=TX 复用推挽输出，PA3=RX 浮空输入 */
    /*    F103 的串口引脚直接配 AF_PP / IN_FLOATING 即可，无需 F4 的 PinAFConfig */
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;         // TX：复用推挽输出
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_2;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_IN_FLOATING;   // RX：浮空输入
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_3;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* 3. USART 参数初始化：8 数据位、1 停止位、无校验、无流控、收发模式 */
    USART_InitStructure.USART_BaudRate            = bound;
    USART_InitStructure.USART_WordLength          = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits            = USART_StopBits_1;
    USART_InitStructure.USART_Parity              = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART2, &USART_InitStructure);

    /* 4. 使能串口 + 开启接收中断 */
    USART_Cmd(USART2, ENABLE);
    USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);

    /* 5. 配置 USART2 中断优先级（抢占1、子0） */
    NVIC_InitStructure.NVIC_IRQChannel                   = USART2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    /* 6. 初始化 TIM4 超时判帧（100ms 无新字节 = 一帧结束），并清零接收状态 */
    TIM4_Int_Init(999, 7199);   // 72MHz / (7199+1) / (999+1) = 10kHz / 1000 = 100ms
    USART2_RX_STA = 0;
}

/*------------------------------------------------------------------------------
 * 函    数：串口2 格式化输出（类 printf，调试用）
 * 说    明：实际与 AS608 通讯用的是 as608.c 里的底层发送函数，本函数仅供调试打印。
 *------------------------------------------------------------------------------*/
void u2_printf(char *fmt, ...)
{
    uint16_t i, j;
    va_list ap;

    va_start(ap, fmt);
    vsprintf((char *)USART2_TX_BUF, fmt, ap);   // 把格式化结果写入发送缓冲
    va_end(ap);

    i = strlen((const char *)USART2_TX_BUF);    // 本次要发送的长度
    for (j = 0; j < i; j++)
    {
        while (USART_GetFlagStatus(USART2, USART_FLAG_TC) == RESET);  // 等上次发送完成
        USART_SendData(USART2, USART2_TX_BUF[j]);                      // 逐个字节发送
    }
}
