#ifndef __LOWPOWER_H
#define __LOWPOWER_H

#include "stm32f10x.h"                  // Device header

/*==============================================================================
 * 低功耗 Stop 模式驱动（由 CountSensor 移植改造而来）
 *
 * 用途：门锁电池供电，待机无操作一段时间后进入 Stop 停机模式省电，
 *       靠外部中断（EXTI）唤醒。
 *
 * 唤醒源（下降沿 = 按键按下拉低）：
 *   - 板载键1 PB11 -> EXTI11
 *   - 板载键2 PB1  -> EXTI1
 *   - 板载键3 PA6  -> EXTI6
 *   - 指纹 WAK PA4 -> EXTI4（上升沿，模块输出高电平=检测到触摸）
 *
 * 设计要点：
 *   - EXTI 只负责"把 CPU 从 Stop 叫醒"，不负责按键识别；
 *     醒来后按键识别仍走原来的 Key_GetNum() 轮询（自带消抖）。
 *   - 进 Stop 前熄屏（OLED_DisplayOff）+ 关 USART2/TIM4；
 *     醒来后 SystemInit 恢复 72MHz + 重建外设 + 亮屏。
 *============================================================================*/

/* 唤醒源 GPIO 定义 */
#define WAKE_KEY1_PIN       GPIO_Pin_11     // 板载键1，PB11
#define WAKE_KEY1_PORT      GPIOB
#define WAKE_KEY1_EXLINE    EXTI_Line11
#define WAKE_KEY1_IRQn      EXTI15_10_IRQn

#define WAKE_KEY2_PIN       GPIO_Pin_1      // 板载键2，PB1
#define WAKE_KEY2_PORT      GPIOB
#define WAKE_KEY2_EXLINE    EXTI_Line1
#define WAKE_KEY2_IRQn      EXTI1_IRQn

#define WAKE_KEY3_PIN       GPIO_Pin_6      // 板载键3，PA6
#define WAKE_KEY3_PORT      GPIOA
#define WAKE_KEY3_EXLINE    EXTI_Line6
#define WAKE_KEY3_IRQn      EXTI9_5_IRQn

#define WAKE_FP_PIN         GPIO_Pin_4      // 指纹 WAK，PA4
#define WAKE_FP_PORT        GPIOA
#define WAKE_FP_EXLINE      EXTI_Line4
#define WAKE_FP_IRQn        EXTI4_IRQn

/* 函数声明 */
void LowPower_Init(void);   // 初始化唤醒源（EXTI 外部中断）
void EnterStopMode(void);   // 熄屏 + 关外设 + 进 Stop 停机模式
void ExitStopMode(void);    // 恢复时钟 + 重建外设 + 亮屏

#endif
