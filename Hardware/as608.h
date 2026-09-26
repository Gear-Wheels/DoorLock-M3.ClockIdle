#ifndef __AS608_H
#define __AS608_H

#include "stm32f10x.h"                  // Device header

/*==============================================================================
 * AS608 指纹模块协议层（STM32F103 版，移植自 F4 的 as608.c/h）
 *
 * 协议：模块采用"命令包/应答包"格式，包头 0xEF 0x01，后接地址(4B)、包标识、
 *       包长度、指令码、数据、校验和。所有命令帧结构一致，仅数据不同。
 *
 * 本文件只做协议收发，不涉及 OLED/菜单，方便以后复用。
 *============================================================================*/

/* 触摸感应状态脚（WAK/VTO）：接 PA4，上拉输入，检测到触摸时模块输出高电平 */
#define PS_Sta_PIN      GPIO_Pin_4
#define PS_Sta_PORT     GPIOA

/* 特征缓冲区编号（模块内部有 2 个 CharBuffer） */
#define CharBuffer1     0x01
#define CharBuffer2     0x02

/* 模块地址（外部变量，握手后可更新） */
extern uint32_t AS608Addr;

/* 搜索结果：匹配到的页码 + 匹配得分 */
typedef struct
{
    uint16_t pageID;        // 指纹模板 ID（页码）
    uint16_t mathscore;     // 匹配得分
} SearchResult;

/* 系统基本参数 */
typedef struct
{
    uint16_t PS_max;        // 指纹最大容量
    uint8_t  PS_level;      // 安全等级
    uint32_t PS_addr;       // 模块地址
    uint8_t  PS_size;       // 通讯数据包大小
    uint8_t  PS_N;          // 波特率基数 N（实际波特率 = N * 9600）
} SysPara;

/* 函数声明 */
void PS_StaGPIO_Init(void);                                    // 初始化触摸感应脚 GPIO

uint8_t PS_GetImage(void);                                     // 录入图像
uint8_t PS_GenChar(uint8_t BufferID);                          // 生成特征
uint8_t PS_Match(void);                                        // 精确比对两枚特征
uint8_t PS_Search(uint8_t BufferID, uint16_t StartPage, uint16_t PageNum, SearchResult *p);  // 搜索指纹
uint8_t PS_RegModel(void);                                     // 合并特征（生成模板）
uint8_t PS_StoreChar(uint8_t BufferID, uint16_t PageID);       // 储存模板
uint8_t PS_DeletChar(uint16_t PageID, uint16_t N);             // 删除模板
uint8_t PS_Empty(void);                                        // 清空指纹库
uint8_t PS_WriteReg(uint8_t RegNum, uint8_t DATA);             // 写系统寄存器
uint8_t PS_ReadSysPara(SysPara *p);                            // 读系统基本参数
uint8_t PS_SetAddr(uint32_t addr);                             // 设置模块地址
uint8_t PS_WriteNotepad(uint8_t NotePageNum, uint8_t *content);// 写记事本
uint8_t PS_ReadNotepad(uint8_t NotePageNum, uint8_t *note);    // 读记事
uint8_t PS_HighSpeedSearch(uint8_t BufferID, uint16_t StartPage, uint16_t PageNum, SearchResult *p);  // 高速搜索
uint8_t PS_ValidTempleteNum(uint16_t *ValidN);                 // 读有效模板个数
uint8_t PS_HandShake(uint32_t *PS_Addr);                       // 与模块握手

const char *EnsureMessage(uint8_t ensure);                     // 确认码错误信息解析

#endif
