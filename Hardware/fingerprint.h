#ifndef __FINGERPRINT_H
#define __FINGERPRINT_H

#include "stm32f10x.h"                  // Device header
#include "as608.h"

/*==============================================================================
 * 指纹模块上层逻辑（STM32F103 版，移植自 F4 的 fingerprint.c/h）
 *
 * 阶段一（本轮）：只实现 FR_Init()，跑通 USART2 + 握手 + 读参数。
 * 阶段二（后续）：再补 Add_FR / press_FR / Del_FR 的录入、验证、删除。
 *============================================================================*/

/* 串口2 波特率（AS608 默认 57600） */
#define USART2_BAUND   57600

/* 全局变量 */
extern SysPara  AS608Para;      // 模块参数
extern uint16_t ValidN;         // 模块内有效指纹个数

/* 函数声明 */
uint8_t FR_Init(void);              // 指纹模块初始化（握手 + 读参数）
void ShowErrMessage(uint8_t ensure);// 打印确认码错误信息

/* 以下为阶段二预留（本轮未实现，先注释声明留作接口） */
void Add_FR(void);                  // 录入指纹
int  press_FR(void);                // 验证指纹
void Del_FR(void);                  // 删除指纹
void View_FR(void);                 // 查看已录入指纹数量

#endif
