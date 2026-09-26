#include "stm32f10x.h"                  // Device header
#include <string.h>
#include <stdio.h>
#include "Delay.h"
#include "usart2.h"
#include "as608.h"

/*==============================================================================
 * AS608 指纹模块协议层（STM32F103 版，移植自 F4 的 as608.c）
 *
 * 改动要点（F4 -> F1）：
 *   1. delay_ms 小写 -> Delay_ms 大写（M3 工程 Delay.h 的接口）
 *   2. u8/u16/u32 -> uint8_t/uint16_t/uint32_t（标准库类型）
 *   3. PAin(8) 位带宏 -> GPIO_ReadInputDataBit()（F1 无 sys.h 位带库）
 *   4. RCC_AHB1PeriphClockCmd -> RCC_APB2PeriphClockCmd（GPIOA 在 APB2）
 *   5. GPIO_Mode_IN / GPIO_PuPd_DOWN -> GPIO_Mode_IPU（触摸脚用上拉输入，最稳）
 *
 * 协议帧格式：
 *   包头(0xEF 0x01) + 地址(4B) + 包标识(0x01) + 长度(2B) + 指令码(1B)
 *   + 数据(N B) + 校验和(2B)
 *   校验和 = 包标识起、到数据结束所有字节之和（不含包头和校验和本身）。
 *
 * 指令码速查表（AS608 常用命令，Sendcmd 里那个数字的含义）：
 *   0x01 采图      —— 探测手指并采集图像到 ImageBuffer
 *   0x02 生成特征  —— 把图像生成特征存入 CharBuffer
 *   0x03 精确比对  —— 比对 CharBuffer1 和 CharBuffer2 两枚特征
 *   0x04 搜索      —— 在指纹库中搜索与 CharBuffer 匹配的模板
 *   0x05 合并特征  —— 合并 CharBuffer1/2 生成模板
 *   0x06 储存模板  —— 把模板存到指纹库指定页
 *   0x0C 删除模板  —— 删除指纹库中指定页开始的 N 个模板
 *   0x0D 清空库    —— 清空整个指纹库
 *   0x0E 写寄存器  —— 写模块内部寄存器（如波特率）
 *   0x0F 读参数    —— 读系统参数（容量、波特率、包大小等）
 *   0x15 设置地址  —— 修改模块的通信地址
 *   0x18 写记事本  —— 写记事本页（32 字节，可存备注）
 *   0x19 读记事本  —— 读记事本页
 *   0x1B 高速搜索  —— 比 0x04 更快的搜索（大数据量用）
 *   0x1D 读模板数  —— 读取有效模板个数
 *   注：握手是特殊空包（只发包头+地址+包标识，无指令码），见 PS_HandShake。
 *============================================================================*/

/* 模块地址，默认 0xFFFFFFFF（广播地址） */
uint32_t AS608Addr = 0xFFFFFFFF;

/*------------------------------------------------------------------------------
 * 函    数：初始化触摸感应脚 GPIO（PA4，上拉输入）
 * 说    明：AS608 的 VTO（触摸输出脚）检测到手指时输出高电平。
 *           这里配成上拉输入，读到高电平 = 有触摸。
 *------------------------------------------------------------------------------*/
void PS_StaGPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);   // 使能 GPIOA 时钟（APB2）

    GPIO_InitStructure.GPIO_Pin   = PS_Sta_PIN;             // PA4
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_IPU;          // 上拉输入，读模块输出
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(PS_Sta_PORT, &GPIO_InitStructure);
}

/*------------------------------------------------------------------------------
 * 函    数：底层发送一个字节（直接操作寄存器，比库函数更快更省）
 *------------------------------------------------------------------------------*/
static void MYUSART_SendData(uint8_t data)
{
    while ((USART2->SR & 0x40) == 0);   // 等待发送数据寄存器空（TXE）
    USART2->DR = data;                  // 写入数据
}

/* 发送包头 0xEF 0x01 */
static void SendHead(void)
{
    MYUSART_SendData(0xEF);
    MYUSART_SendData(0x01);
}

/* 发送 4 字节地址（高字节在前） */
static void SendAddr(void)
{
    MYUSART_SendData(AS608Addr >> 24);
    MYUSART_SendData(AS608Addr >> 16);
    MYUSART_SendData(AS608Addr >> 8);
    MYUSART_SendData(AS608Addr);
}

/* 发送包标识（命令包固定 0x01） */
static void SendFlag(uint8_t flag)
{
    MYUSART_SendData(flag);
}

/* 发送包长度（2 字节，高字节在前） */
static void SendLength(int length)
{
    MYUSART_SendData(length >> 8);
    MYUSART_SendData(length);
}

/* 发送指令码 */
static void Sendcmd(uint8_t cmd)
{
    MYUSART_SendData(cmd);
}

/* 发送校验和（2 字节，高字节在前） */
static void SendCheck(uint16_t check)
{
    MYUSART_SendData(check >> 8);
    MYUSART_SendData(check);
}

/*------------------------------------------------------------------------------
 * 函    数：等待并查找应答包
 * 参    数：waittime  等待时间（单位 1ms）
 * 返 回 值：应答包数据首地址；超时无应答返回 0
 * 说    明：等待中断接收完成一帧（bit15 置位），在接收缓冲里用 strstr 查找
 *           以"包头+地址+包标识 0x07"为特征的应答包，找到则返回其地址。
 *------------------------------------------------------------------------------*/
static uint8_t *JudgeStr(uint16_t waittime)
{
    char *data;
    uint8_t str[8];

    /* 应答包特征头：0xEF 0x01 + 地址(4B) + 0x07（应答包标识） */
    str[0] = 0xEF;
    str[1] = 0x01;
    str[2] = AS608Addr >> 24;
    str[3] = AS608Addr >> 16;
    str[4] = AS608Addr >> 8;
    str[5] = AS608Addr;
    str[6] = 0x07;
    str[7] = '\0';

    USART2_RX_STA = 0;                  // 清零接收状态，准备接收新一帧
    while (--waittime)
    {
        Delay_ms(1);
        if (USART2_RX_STA & 0x8000)     // 一帧接收完成
        {
            USART2_RX_STA = 0;
            data = strstr((const char *)USART2_RX_BUF, (const char *)str);
            if (data)
                return (uint8_t *)data; // 找到应答包
        }
    }
    return 0;                           // 超时
}

/*------------------------------------------------------------------------------
 * 函    数：录入图像（探测手指）
 * 返 回 值：0x00 成功；其它为错误码（见 EnsureMessage）
 *------------------------------------------------------------------------------*/
uint8_t PS_GetImage(void)
{
    uint16_t temp;
    uint8_t  ensure;
    uint8_t  *data;

    SendHead();
    SendAddr();
    SendFlag(0x01);                     // 命令包标识
    SendLength(0x03);
    Sendcmd(0x01);
    temp = 0x01 + 0x03 + 0x01;          // 校验和 = 包标识 + 长度(2B) + 指令码
    SendCheck(temp);

    data = JudgeStr(2000);
    if (data)
        ensure = data[9];               // 应答包第 10 字节是确认码
    else
        ensure = 0xFF;
    return ensure;
}

/*------------------------------------------------------------------------------
 * 函    数：生成特征（将 ImageBuffer 图像生成特征存 CharBuffer）
 * 参    数：BufferID  CharBuffer1(0x01) 或 CharBuffer2(0x02)
 *------------------------------------------------------------------------------*/
uint8_t PS_GenChar(uint8_t BufferID)
{
    uint16_t temp;
    uint8_t  ensure;
    uint8_t  *data;

    SendHead();
    SendAddr();
    SendFlag(0x01);
    SendLength(0x04);
    Sendcmd(0x02);
    MYUSART_SendData(BufferID);
    temp = 0x01 + 0x04 + 0x02 + BufferID;
    SendCheck(temp);

    data = JudgeStr(2000);
    if (data)
        ensure = data[9];
    else
        ensure = 0xFF;
    return ensure;
}

/*------------------------------------------------------------------------------
 * 函    数：精确比对 CharBuffer1 与 CharBuffer2 两枚特征
 *------------------------------------------------------------------------------*/
uint8_t PS_Match(void)
{
    uint16_t temp;
    uint8_t  ensure;
    uint8_t  *data;

    SendHead();
    SendAddr();
    SendFlag(0x01);
    SendLength(0x03);
    Sendcmd(0x03);
    temp = 0x01 + 0x03 + 0x03;
    SendCheck(temp);

    data = JudgeStr(2000);
    if (data)
        ensure = data[9];
    else
        ensure = 0xFF;
    return ensure;
}

/*------------------------------------------------------------------------------
 * 函    数：搜索指纹库
 * 参    数：BufferID   CharBuffer1/2
 *           StartPage  起始页
 *           PageNum    搜索页数
 *           p          输出搜索结果（页码 + 匹配得分）
 *------------------------------------------------------------------------------*/
uint8_t PS_Search(uint8_t BufferID, uint16_t StartPage, uint16_t PageNum, SearchResult *p)
{
    uint16_t temp;
    uint8_t  ensure;
    uint8_t  *data;

    SendHead();
    SendAddr();
    SendFlag(0x01);
    SendLength(0x08);
    Sendcmd(0x04);
    MYUSART_SendData(BufferID);
    MYUSART_SendData(StartPage >> 8);
    MYUSART_SendData(StartPage);
    MYUSART_SendData(PageNum >> 8);
    MYUSART_SendData(PageNum);
    temp = 0x01 + 0x08 + 0x04 + BufferID
         + (StartPage >> 8) + (uint8_t)StartPage
         + (PageNum >> 8) + (uint8_t)PageNum;
    SendCheck(temp);

    data = JudgeStr(2000);
    if (data)
    {
        ensure = data[9];
        p->pageID    = (data[10] << 8) + data[11];
        p->mathscore = (data[12] << 8) + data[13];
    }
    else
        ensure = 0xFF;
    return ensure;
}

/*------------------------------------------------------------------------------
 * 函    数：合并特征（生成模板），结果存回 CharBuffer1/2
 *------------------------------------------------------------------------------*/
uint8_t PS_RegModel(void)
{
    uint16_t temp;
    uint8_t  ensure;
    uint8_t  *data;

    SendHead();
    SendAddr();
    SendFlag(0x01);
    SendLength(0x03);
    Sendcmd(0x05);
    temp = 0x01 + 0x03 + 0x05;
    SendCheck(temp);

    data = JudgeStr(2000);
    if (data)
        ensure = data[9];
    else
        ensure = 0xFF;
    return ensure;
}

/*------------------------------------------------------------------------------
 * 函    数：储存模板到指纹库指定页
 * 参    数：BufferID  CharBuffer1/2
 *           PageID    指纹库位置号
 *------------------------------------------------------------------------------*/
uint8_t PS_StoreChar(uint8_t BufferID, uint16_t PageID)
{
    uint16_t temp;
    uint8_t  ensure;
    uint8_t  *data;

    SendHead();
    SendAddr();
    SendFlag(0x01);
    SendLength(0x06);
    Sendcmd(0x06);
    MYUSART_SendData(BufferID);
    MYUSART_SendData(PageID >> 8);
    MYUSART_SendData(PageID);
    temp = 0x01 + 0x06 + 0x06 + BufferID
         + (PageID >> 8) + (uint8_t)PageID;
    SendCheck(temp);

    data = JudgeStr(2000);
    if (data)
        ensure = data[9];
    else
        ensure = 0xFF;
    return ensure;
}

/*------------------------------------------------------------------------------
 * 函    数：删除指纹库中从 PageID 开始的 N 个模板
 *------------------------------------------------------------------------------*/
uint8_t PS_DeletChar(uint16_t PageID, uint16_t N)
{
    uint16_t temp;
    uint8_t  ensure;
    uint8_t  *data;

    SendHead();
    SendAddr();
    SendFlag(0x01);
    SendLength(0x07);
    Sendcmd(0x0C);
    MYUSART_SendData(PageID >> 8);
    MYUSART_SendData(PageID);
    MYUSART_SendData(N >> 8);
    MYUSART_SendData(N);
    temp = 0x01 + 0x07 + 0x0C
         + (PageID >> 8) + (uint8_t)PageID
         + (N >> 8) + (uint8_t)N;
    SendCheck(temp);

    data = JudgeStr(2000);
    if (data)
        ensure = data[9];
    else
        ensure = 0xFF;
    return ensure;
}

/*------------------------------------------------------------------------------
 * 函    数：清空整个指纹库
 *------------------------------------------------------------------------------*/
uint8_t PS_Empty(void)
{
    uint16_t temp;
    uint8_t  ensure;
    uint8_t  *data;

    SendHead();
    SendAddr();
    SendFlag(0x01);
    SendLength(0x03);
    Sendcmd(0x0D);
    temp = 0x01 + 0x03 + 0x0D;
    SendCheck(temp);

    data = JudgeStr(2000);
    if (data)
        ensure = data[9];
    else
        ensure = 0xFF;
    return ensure;
}

/*------------------------------------------------------------------------------
 * 函    数：写系统寄存器
 * 参    数：RegNum  寄存器序号（4/5/6）
 *           DATA    写入值
 *------------------------------------------------------------------------------*/
uint8_t PS_WriteReg(uint8_t RegNum, uint8_t DATA)
{
    uint16_t temp;
    uint8_t  ensure;
    uint8_t  *data;

    SendHead();
    SendAddr();
    SendFlag(0x01);
    SendLength(0x05);
    Sendcmd(0x0E);
    MYUSART_SendData(RegNum);
    MYUSART_SendData(DATA);
    temp = RegNum + DATA + 0x01 + 0x05 + 0x0E;
    SendCheck(temp);

    data = JudgeStr(2000);
    if (data)
        ensure = data[9];
    else
        ensure = 0xFF;
    return ensure;
}

/*------------------------------------------------------------------------------
 * 函    数：读系统基本参数（波特率、包大小、容量等）
 * 参    数：p  输出参数结构体
 *------------------------------------------------------------------------------*/
uint8_t PS_ReadSysPara(SysPara *p)
{
    uint16_t temp;
    uint8_t  ensure;
    uint8_t  *data;

    SendHead();
    SendAddr();
    SendFlag(0x01);
    SendLength(0x03);
    Sendcmd(0x0F);
    temp = 0x01 + 0x03 + 0x0F;
    SendCheck(temp);

    data = JudgeStr(1000);
    if (data)
    {
        ensure = data[9];
        p->PS_max   = (data[14] << 8) + data[15];
        p->PS_level = data[17];
        p->PS_addr  = (data[18] << 24) + (data[19] << 16) + (data[20] << 8) + data[21];
        p->PS_size  = data[23];
        p->PS_N     = data[25];
    }
    else
        ensure = 0xFF;
    return ensure;
}

/*------------------------------------------------------------------------------
 * 函    数：设置模块地址
 *------------------------------------------------------------------------------*/
uint8_t PS_SetAddr(uint32_t PS_addr)
{
    uint16_t temp;
    uint8_t  ensure;
    uint8_t  *data;

    SendHead();
    SendAddr();
    SendFlag(0x01);
    SendLength(0x07);
    Sendcmd(0x15);
    MYUSART_SendData(PS_addr >> 24);
    MYUSART_SendData(PS_addr >> 16);
    MYUSART_SendData(PS_addr >> 8);
    MYUSART_SendData(PS_addr);
    temp = 0x01 + 0x07 + 0x15
         + (uint8_t)(PS_addr >> 24) + (uint8_t)(PS_addr >> 16)
         + (uint8_t)(PS_addr >> 8) + (uint8_t)PS_addr;
    SendCheck(temp);

    AS608Addr = PS_addr;                // 发送完指令后更换地址
    data = JudgeStr(2000);
    if (data)
        ensure = data[9];
    else
        ensure = 0xFF;
    AS608Addr = PS_addr;
    return ensure;
}

/*------------------------------------------------------------------------------
 * 函    数：写记事本（模块内 256B Flash，分 16 页，每页 32B）
 *------------------------------------------------------------------------------*/
uint8_t PS_WriteNotepad(uint8_t NotePageNum, uint8_t *Byte32)
{
    uint16_t temp = 0;
    uint8_t  ensure, i;
    uint8_t  *data;

    SendHead();
    SendAddr();
    SendFlag(0x01);
    SendLength(36);
    Sendcmd(0x18);
    MYUSART_SendData(NotePageNum);
    for (i = 0; i < 32; i++)
    {
        MYUSART_SendData(Byte32[i]);
        temp += Byte32[i];
    }
    temp = 0x01 + 36 + 0x18 + NotePageNum + temp;
    SendCheck(temp);

    data = JudgeStr(2000);
    if (data)
        ensure = data[9];
    else
        ensure = 0xFF;
    return ensure;
}

/*------------------------------------------------------------------------------
 * 函    数：读记事本
 *------------------------------------------------------------------------------*/
uint8_t PS_ReadNotepad(uint8_t NotePageNum, uint8_t *Byte32)
{
    uint16_t temp;
    uint8_t  ensure, i;
    uint8_t  *data;

    SendHead();
    SendAddr();
    SendFlag(0x01);
    SendLength(0x04);
    Sendcmd(0x19);
    MYUSART_SendData(NotePageNum);
    temp = 0x01 + 0x04 + 0x19 + NotePageNum;
    SendCheck(temp);

    data = JudgeStr(2000);
    if (data)
    {
        ensure = data[9];
        for (i = 0; i < 32; i++)
            Byte32[i] = data[10 + i];
    }
    else
        ensure = 0xFF;
    return ensure;
}

/*------------------------------------------------------------------------------
 * 函    数：高速搜索指纹库（比 PS_Search 更快，适合大库）
 *------------------------------------------------------------------------------*/
uint8_t PS_HighSpeedSearch(uint8_t BufferID, uint16_t StartPage, uint16_t PageNum, SearchResult *p)
{
    uint16_t temp;
    uint8_t  ensure;
    uint8_t  *data;

    SendHead();
    SendAddr();
    SendFlag(0x01);
    SendLength(0x08);
    Sendcmd(0x1B);
    MYUSART_SendData(BufferID);
    MYUSART_SendData(StartPage >> 8);
    MYUSART_SendData(StartPage);
    MYUSART_SendData(PageNum >> 8);
    MYUSART_SendData(PageNum);
    temp = 0x01 + 0x08 + 0x1B + BufferID
         + (StartPage >> 8) + (uint8_t)StartPage
         + (PageNum >> 8) + (uint8_t)PageNum;
    SendCheck(temp);

    data = JudgeStr(2000);
    if (data)
    {
        ensure = data[9];
        p->pageID    = (data[10] << 8) + data[11];
        p->mathscore = (data[12] << 8) + data[13];
    }
    else
        ensure = 0xFF;
    return ensure;
}

/*------------------------------------------------------------------------------
 * 函    数：读有效模板个数
 *------------------------------------------------------------------------------*/
uint8_t PS_ValidTempleteNum(uint16_t *ValidN)
{
    uint16_t temp;
    uint8_t  ensure;
    uint8_t  *data;

    SendHead();
    SendAddr();
    SendFlag(0x01);
    SendLength(0x03);
    Sendcmd(0x1D);
    temp = 0x01 + 0x03 + 0x1D;
    SendCheck(temp);

    data = JudgeStr(2000);
    if (data)
    {
        ensure = data[9];
        *ValidN = (data[10] << 8) + data[11];
    }
    else
        ensure = 0xFF;
    return ensure;
}

/*------------------------------------------------------------------------------
 * 函    数：与 AS608 握手
 * 参    数：PS_Addr  输出握手得到的模块地址
 * 返 回 值：0 成功；1 失败
 *------------------------------------------------------------------------------*/
uint8_t PS_HandShake(uint32_t *PS_Addr)
{
    uint8_t i;

    /* 握手可能因上电时序失败，这里重试 5 次，提高容错 */
    for (i = 0; i < 5; i++)
    {
        USART2_RX_STA = 0;              // 清接收状态，准备接收应答

        SendHead();
        SendAddr();
        MYUSART_SendData(0x01);     // 包标识
        MYUSART_SendData(0x00);     // 长度高字节
        MYUSART_SendData(0x00);     // 长度低字节
        Delay_ms(200);

        if (USART2_RX_STA & 0x8000) // 收到数据
        {
            if (USART2_RX_BUF[0] == 0xEF &&
                USART2_RX_BUF[1] == 0x01 &&
                USART2_RX_BUF[6] == 0x07)   // 判断是否为模块应答包
            {
                *PS_Addr = (USART2_RX_BUF[2] << 24) + (USART2_RX_BUF[3] << 16)
                         + (USART2_RX_BUF[4] << 8)  +  USART2_RX_BUF[5];
                USART2_RX_STA = 0;
                return 0;
            }
            USART2_RX_STA = 0;
        }
    }
    return 1;   // 重试 5 次仍失败
}

/*------------------------------------------------------------------------------
 * 函    数：解析确认码，返回对应的中文错误信息
 *------------------------------------------------------------------------------*/
const char *EnsureMessage(uint8_t ensure)
{
    const char *p;
    switch (ensure)
    {
        case 0x00: p = "OK"; break;
        case 0x01: p = "数据包接收错误"; break;
        case 0x02: p = "传感器上没有手指"; break;
        case 0x03: p = "录入指纹图像失败"; break;
        case 0x04: p = "指纹图像太干、太淡而生不成特征"; break;
        case 0x05: p = "指纹图像太湿、太糊而生不成特征"; break;
        case 0x06: p = "指纹图像太乱而生不成特征"; break;
        case 0x07: p = "指纹图像正常，但特征点太少而生不成特征"; break;
        case 0x08: p = "指纹不匹配"; break;
        case 0x09: p = "指纹不匹配"; break;
        case 0x0A: p = "特征合并失败"; break;
        case 0x0B: p = "访问指纹库时地址序号超出范围"; break;
        case 0x10: p = "删除模板失败"; break;
        case 0x11: p = "清空指纹库失败"; break;
        case 0x15: p = "缓冲区内没有有效原始图而生不成图像"; break;
        case 0x18: p = "读写 FLASH 出错"; break;
        case 0x19: p = "未定义错误"; break;
        case 0x1A: p = "无效寄存器号"; break;
        case 0x1B: p = "寄存器设定内容错误"; break;
        case 0x1C: p = "记事本页码指定错误"; break;
        case 0x1F: p = "指纹库满"; break;
        case 0x20: p = "地址错误"; break;
        default:   p = "模块返回确认码有误"; break;
    }
    return p;
}
