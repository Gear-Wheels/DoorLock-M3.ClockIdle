#include "stm32f10x.h"                  // Device header
#include "fingerprint.h"
#include "usart2.h"
#include "as608.h"
#include "OLED.h"                       // OLED 显示（调试/提示）
#include "Serial.h"                     // 串口1 打印（FR_DEBUG_PRINT=1 时启用）
#include "Delay.h"                      // Delay_ms 延时函数
#include "Key.h"                        // Key_GetNum 板载三键

/*==============================================================================
 * 指纹模块上层逻辑（STM32F103 版，移植自 F4 的 fingerprint.c）
 *
 * 【指纹功能分了"三层"，这是最上面一层】
 *   usart2.c  —— 最底层：管 USART2 串口收发（怎么把字节发出去/收回来）
 *   as608.c   —— 中间层：管 AS608 协议（怎么组织命令包、解析应答包）
 *   fingerprint.c —— 最上层（本文件）：管业务（录入/验证/删除/查看这些"用户操作"）
 *
 * 【为什么要分三层】
 *   就像快递：usart2 是"货车"（管运输），as608 是"打包/拆包"（管格式），
 *   fingerprint 是"寄件人"（管我要寄什么）。每层只管自己的事，
 *   改一层不影响另外两层。比如换一个指纹模块，只改 as608 协议层就行，
 *   上层 fingerprint 和下层的显示都不用动。
 *
 * 阶段一：FR_Init() —— USART2 初始化 + 握手 + 读参数（上电时调用）。
 * 阶段二：Add_FR()（录入）/ press_FR()（验证解锁）/ Del_FR()（删除）/ View_FR()（查看）。
 *
 * 调试开关 FR_DEBUG_PRINT：
 *   =0（默认，关闭）：提示信息用 OLED 显示，不碰串口1（PA9/PA10 被薄膜键盘占用）
 *   =1（开启）：调试信息额外用串口1 打印，方便连电脑看日志。
 *   后期若把键盘引脚挪开、解放出串口1，把此宏改成 1 并重新编译即可，
 *   无需改动任何函数体。注意开启前 main.c 要调用 Serial_Init()。
 *============================================================================*/

#define FR_DEBUG_PRINT   0              // 调试开关：0=OLED显示 1=串口1打印

/*==============================================================================
 * 初始化 OLED 显示开关 FR_OLED_DISPLAY：
 *   =0（默认，关闭）：FR_Init() 全程静默，不在 OLED 上显示握手/参数等调试信息，
 *                     上电后直接进入时钟待机界面（屏上干净，不刷初始化过程）。
 *   =1（开启）：FR_Init() 在 OLED 上逐步显示 HandShake/Read Para/Ready 等状态。
 *   后期想观察初始化过程，把此宏改成 1 重新编译即可，函数体不用动。
 *============================================================================*/
#define FR_OLED_DISPLAY   0              // 初始化显示开关：0=静默 1=OLED显示

/* 模块参数与有效指纹个数（全局，供上层读取） */
SysPara  AS608Para;
uint16_t ValidN;

/*------------------------------------------------------------------------------
 * 内部封装：初始化过程的 OLED 显示。由 FR_OLED_DISPLAY 控制是否生效。
 * 说    明：默认关闭时，FR_Init 不碰 OLED，屏交给 main 的 IdleClock_Draw。
 *------------------------------------------------------------------------------*/
#if FR_OLED_DISPLAY
#define FR_OLED(...)    do { __VA_ARGS__; } while(0)
#else
#define FR_OLED(...)    do {} while(0)
#endif

/*------------------------------------------------------------------------------
 * 内部封装：输出提示。根据 FR_DEBUG_PRINT 决定走串口1 还是 OLED。
 * 说    明：OLED 显示始终生效（屏上反馈是必须的）；串口1 只在开关打开时
 *           额外输出，方便电脑端看日志。二者不冲突。
 *------------------------------------------------------------------------------*/
#if FR_DEBUG_PRINT
#define FR_LOG(...)    Serial_Printf(__VA_ARGS__)
#else
#define FR_LOG(...)    do {} while(0)
#endif

/*------------------------------------------------------------------------------
 * 函    数：指纹模块初始化
 * 返 回 值：0 初始化成功；非 0 为失败（1=握手失败，2=读参数失败，3=读模板数失败）
 * 说    明：依次做三件事——
 *           1) 初始化 USART2 + 触摸感应脚 GPIO
 *           2) 等待模块上电稳定后握手，确认模块在线
 *           3) 读系统参数、读有效模板个数
 * 显示说明：每步结果都在 OLED 屏上显示；失败时停在屏上 2 秒便于观察。
 *------------------------------------------------------------------------------*/
uint8_t FR_Init(void)
{
    uint8_t ret;

    /* 1. 初始化 USART2（与 AS608 通讯）+ 触摸感应脚 GPIO */
    usart2_init(USART2_BAUND);
    PS_StaGPIO_Init();

    /* 1.5 等待模块上电稳定（AS608 上电后需数百 ms 才能应答，复位后立刻发命令会握手失败） */
    Delay_ms(800);

    /* 2. 握手：确认模块在线 */
    FR_OLED(
        OLED_Clear();
        OLED_ShowString(0, 0, "AS608 Init", OLED_8X16);
        OLED_ShowString(0, 16, "HandShake...", OLED_8X16);
        OLED_Update();
    );
    FR_LOG("AS608 Init...\r\n");

    ret = PS_HandShake(&AS608Addr);
    if (ret != 0)
    {
        FR_OLED(
            OLED_ShowString(0, 32, "HandShake Fail!", OLED_8X16);
            /* 调试：显示 USART2 实际收到的字节数，用于区分"完全没收到"和"收到但解析失败" */
            OLED_Printf(0, 48, OLED_8X16, "RX:%d", (int)(USART2_RX_STA & 0x7FFF));
            OLED_Update();
            Delay_ms(2000);             // 失败停留观察，静默模式下不执行
        );
        FR_LOG("HandShake Fail! RX:%d\r\n", (int)(USART2_RX_STA & 0x7FFF));
        return 1;
    }

    /* 3. 读系统参数 */
    FR_OLED(
        OLED_ShowString(0, 32, "HandShake OK", OLED_8X16);
        OLED_ShowString(0, 48, "Read Para...", OLED_8X16);
        OLED_Update();
    );
    FR_LOG("HandShake OK\r\n");

    ret = PS_ReadSysPara(&AS608Para);
    if (ret != 0)
    {
        FR_OLED(
            OLED_Clear();
            OLED_ShowString(0, 0, "Read Para Fail", OLED_8X16);
            OLED_Update();
            Delay_ms(2000);
        );
        FR_LOG("Read Para Fail! err=0x%02X\r\n", ret);
        return 2;
    }

    /* 4. 读有效模板个数 */
    ret = PS_ValidTempleteNum(&ValidN);
    if (ret != 0)
    {
        FR_OLED(
            OLED_Clear();
            OLED_ShowString(0, 0, "Read Num Fail", OLED_8X16);
            OLED_Update();
            Delay_ms(2000);
        );
        FR_LOG("Read Num Fail! err=0x%02X\r\n", ret);
        return 3;
    }

    /* 全部成功：显示初始化完成 + 指纹容量 */
    FR_OLED(
        OLED_Clear();
        OLED_ShowString(0, 0, "AS608 Ready!", OLED_8X16);
        OLED_Printf(0, 16, OLED_8X16, "Cap:%d", AS608Para.PS_max);
        OLED_Printf(0, 32, OLED_8X16, "Valid:%d", ValidN);
        OLED_Update();
        Delay_ms(1500);                 // 显示停留，静默模式下不执行
    );
    FR_LOG("AS608 Ready! Cap:%d Valid:%d\r\n", AS608Para.PS_max, ValidN);
    return 0;
}

/*------------------------------------------------------------------------------
 * 函    数：显示确认码对应的错误信息（调试用）
 *------------------------------------------------------------------------------*/
void ShowErrMessage(uint8_t ensure)
{
    FR_LOG("Err[0x%02X]: %s\r\n", ensure, EnsureMessage(ensure));
}

/*------------------------------------------------------------------------------
 * 函    数：录入指纹
 * 说    明：流程 = GetImage -> GenChar(两次) -> Match -> RegModel -> StoreChar。
 *           用 processnum 状态机分 5 步走；某步失败则提示并回退或重试。
 *           录入成功后自动分配 ID = 当前有效数 + 1。
 *           采到第一张图并生成特征后，先搜索库内是否已有相同指纹，
 *           防止重复录入（同一枚手指被录两次）。
 *------------------------------------------------------------------------------*/
void Add_FR(void)
{
    uint8_t ensure;
    uint8_t processnum = 0;
    uint16_t ID;
    SearchResult seach;

    OLED_Clear();
    OLED_ShowString(32, 24, "请按手指", OLED_8X16);
    OLED_Update();

    while (1)
    {
        switch (processnum)
        {
            case 0:                                     // 第一步：采第一张图像
                ensure = PS_GetImage();
                if (ensure == 0x00)
                {
                    ensure = PS_GenChar(CharBuffer1);   // 生成特征到 CharBuffer1
                    if (ensure == 0x00)
                    {
                        /* 防重复录入：用 CharBuffer1 在库内搜索，若已存在相同指纹则拒绝 */
                        if (PS_HighSpeedSearch(CharBuffer1, 0, AS608Para.PS_max, &seach) == 0x00)
                        {
                            OLED_Clear();
                            OLED_ShowString(16, 8,  "该指纹已录入", OLED_8X16);
                            OLED_ShowString(16, 32, "编号:", OLED_8X16);
                            OLED_ShowNum(64, 32, seach.pageID, 3, OLED_8X16);
                            OLED_Update();
                            Delay_ms(2000);
                            return;
                        }
                        processnum = 1;                 // 未重复，进第二步
                    }
                    else ShowErrMessage(ensure);
                }
                else ShowErrMessage(ensure);
                break;

            case 1:                                     // 第二步：采第二张图像
                OLED_Clear();
                OLED_ShowString(32, 24, "请再按手指", OLED_8X16);
                OLED_Update();
                ensure = PS_GetImage();
                if (ensure == 0x00)
                {
                    ensure = PS_GenChar(CharBuffer2);   // 生成特征到 CharBuffer2
                    if (ensure == 0x00)
                        processnum = 2;                 // 成功，进第三步
                    else ShowErrMessage(ensure);
                }
                else ShowErrMessage(ensure);
                break;

            case 2:                                     // 第三步：比对两次特征是否同指
                ensure = PS_Match();
                if (ensure == 0x00)
                    processnum = 3;                     // 两次一致，进第四步
                else
                {
                    ShowErrMessage(ensure);             // 两次不一致，重录
                    processnum = 0;
                }
                Delay_ms(1200);
                break;

            case 3:                                     // 第四步：合并特征生成模板
                ensure = PS_RegModel();
                if (ensure == 0x00)
                    processnum = 4;                     // 成功，进第五步
                else
                {
                    ShowErrMessage(ensure);
                    processnum = 0;
                }
                Delay_ms(1200);
                break;

            case 4:                                     // 第五步：分配 ID 并存储
            {
                uint16_t curN;
                PS_ValidTempleteNum(&curN);             // 读当前有效指纹数

                if (curN < AS608Para.PS_max)            // 库没满，ID = 有效数 + 1
                {
                    ID = curN + 1;
                }
                else                                    // 库满，退出
                {
                    OLED_Clear();
                    OLED_ShowString(32, 24, "指纹库已满", OLED_8X16);
                    OLED_Update();
                    Delay_ms(1500);
                    return;
                }

                ensure = PS_StoreChar(CharBuffer2, ID); // 存储模板
                if (ensure == 0x00)
                {
                    OLED_Clear();
                    OLED_ShowString(32, 8,  "录入成功", OLED_8X16);
                    OLED_ShowString(32, 32, "编号:", OLED_8X16);
                    OLED_ShowNum(80, 32, ID, 3, OLED_8X16);   // 显示本次分配的编号
                    OLED_Update();
                    PS_ValidTempleteNum(&ValidN);       // 刷新有效指纹数
                    Delay_ms(1500);
                    return;
                }
                else
                {
                    OLED_Clear();
                    OLED_ShowString(32, 24, "录入失败", OLED_8X16);
                    OLED_Update();
                    ShowErrMessage(ensure);
                    processnum = 0;                     // 失败重录
                    Delay_ms(1200);
                }
                break;
            }
        }
        Delay_ms(400);
    }
}

/*------------------------------------------------------------------------------
 * 函    数：验证指纹（刷指纹解锁）
 * 返 回 值：1 解锁成功；0 用户返回；-1 验证失败/无手指
 * 说    明：流程 = GetImage -> GenChar -> HighSpeedSearch -> 匹配判断。
 *           先等待手指按下（期间轮询板载键2 退出）；采集到指纹后搜索比对。
 *------------------------------------------------------------------------------*/
int press_FR(void)
{
    SearchResult seach;
    uint8_t ensure;
    uint16_t i;

    /* 等待手指按下：轮询 PA4 触摸脚 + 板载键2 返回 */
    OLED_Clear();
    OLED_ShowString(32, 24, "请按手指", OLED_8X16);
    OLED_Update();

    for (i = 0; i < 100; i++)                           // 约 10 秒超时
    {
        if (GPIO_ReadInputDataBit(PS_Sta_PORT, PS_Sta_PIN) == 1)   // 检测到触摸
            break;
        if (Key_GetNum() == 2)                          // 板载键2 返回
        {
            OLED_Clear();
            OLED_Update();
            return 0;
        }
        Delay_ms(100);
    }

    if (i >= 100)                                       // 超时无触摸
    {
        OLED_Clear();
        OLED_ShowString(32, 24, "验证超时", OLED_8X16);
        OLED_Update();
        Delay_ms(1500);
        return -1;
    }

    /* 采集图像 -> 生成特征 -> 高速搜索 */
    ensure = PS_GetImage();
    if (ensure != 0x00)                                 // 采图失败（可能手指没按好）
    {
        OLED_Clear();
        OLED_ShowString(32, 24, "请重按", OLED_8X16);
        OLED_Update();
        ShowErrMessage(ensure);
        Delay_ms(1500);
        return -1;
    }

    ensure = PS_GenChar(CharBuffer1);                   // 生成特征
    if (ensure != 0x00)
    {
        OLED_Clear();
        OLED_ShowString(32, 24, "请重按", OLED_8X16);
        OLED_Update();
        ShowErrMessage(ensure);
        Delay_ms(1500);
        return -1;
    }

    ensure = PS_HighSpeedSearch(CharBuffer1, 0, AS608Para.PS_max, &seach);   // 搜索
    if (ensure == 0x00)                                 // 搜索到匹配模板
    {
        OLED_Clear();
        OLED_ShowString(32, 24, "解锁成功", OLED_8X16);
        OLED_Update();
        FR_LOG("Unlock OK! ID=%d score=%d\r\n", seach.pageID, seach.mathscore);
        /* TODO: 这里以后接开锁动作（LED/蜂鸣器/电机） */
        Delay_ms(1500);
        return 1;
    }
    else                                                // 未匹配
    {
        OLED_Clear();
        OLED_ShowString(32, 24, "验证失败", OLED_8X16);
        OLED_Update();
        ShowErrMessage(ensure);
        Delay_ms(2000);
        return -1;
    }
}

/*------------------------------------------------------------------------------
 * 函    数：查看已录入指纹编号
 * 说    明：第一行显示总数 N；下面用键1(上)/键2(下) 在 1~N 之间翻看编号，
 *           键3 或键1 翻到边界时返回。
 *------------------------------------------------------------------------------*/
void View_FR(void)
{
    uint16_t num = 1;                       // 当前翻看的编号，从 1 开始

    /* 实时读一次有效模板数 */
    PS_ValidTempleteNum(&ValidN);

    if (ValidN == 0)                        // 没有指纹，直接提示返回
    {
        OLED_Clear();
        OLED_ShowString(0, 16, "暂无指纹", OLED_8X16);
        OLED_ShowString(0, 48, "任意键返回", OLED_8X16);
        OLED_Update();
        while (Key_GetNum() == 0);
        OLED_Clear();
        OLED_Update();
        return;
    }

    while (1)
    {
        uint8_t k;
        OLED_Clear();
        OLED_Printf(0, 0, OLED_8X16, "总数:%d", ValidN);    // 第一行：总数
        OLED_ShowString(0, 16, "编号:", OLED_8X16);
        OLED_ShowNum(48, 16, num, 3, OLED_8X16);            // 当前翻看的编号
        OLED_ShowString(0, 32, "上下:翻看", OLED_8X16);
        OLED_ShowString(0, 48, "确认:返回", OLED_8X16);
        OLED_Update();

        k = Key_GetNum();
        if (k == 1)                         // 上翻（编号递减）
        {
            if (num > 1) num--;
        }
        else if (k == 2)                    // 下翻（编号递增）
        {
            if (num < ValidN) num++;
        }
        else if (k == 3)                    // 确认返回
        {
            break;
        }
        Delay_ms(100);
    }

    OLED_Clear();
    OLED_Update();
}

/*------------------------------------------------------------------------------
 * 函    数：删除指纹（按编号精确删除）
 * 说    明：用键1(上)/键2(下) 在 1~N 之间翻选编号，键3 确认删除该编号。
 *           编号是录入时 1~N 连续分配的，删除中间编号后编号会留空洞，
 *           但翻选范围仍为 1~N（总数 N 由模块实时读出）。
 *------------------------------------------------------------------------------*/
void Del_FR(void)
{
    uint8_t  ensure;
    uint16_t num = 1;                       // 当前选中的编号，从 1 开始

    /* 实时读一次有效模板数 */
    PS_ValidTempleteNum(&ValidN);

    if (ValidN == 0)                        // 没有指纹可删
    {
        OLED_Clear();
        OLED_ShowString(0, 16, "暂无指纹", OLED_8X16);
        OLED_ShowString(0, 48, "任意键返回", OLED_8X16);
        OLED_Update();
        while (Key_GetNum() == 0);
        OLED_Clear();
        OLED_Update();
        return;
    }

    /* 主循环：翻选编号 */
    while (1)
    {
        uint8_t k;
        OLED_Clear();
        OLED_Printf(0, 0, OLED_8X16, "总数:%d", ValidN);
        OLED_ShowString(0, 16, "删除编号:", OLED_8X16);
        OLED_ShowNum(80, 16, num, 3, OLED_8X16);
        OLED_ShowString(0, 32, "上下:翻选", OLED_8X16);
        OLED_ShowString(0, 48, "确认:删除", OLED_8X16);
        OLED_Update();

        k = Key_GetNum();
        if (k == 1)                         // 上翻
        {
            if (num > 1) num--;
        }
        else if (k == 2)                    // 下翻
        {
            if (num < ValidN) num++;
        }
        else if (k == 3)                    // 确认删除当前编号
        {
            break;
        }
        Delay_ms(100);
    }

    /* 执行删除：删除选中的编号 num */
    ensure = PS_DeletChar(num, 1);

    if (ensure == 0x00)
    {
        OLED_Clear();
        OLED_ShowString(32, 24, "删除成功", OLED_8X16);
        OLED_Update();
    }
    else
    {
        OLED_Clear();
        OLED_ShowString(32, 24, "删除失败", OLED_8X16);
        OLED_Update();
        ShowErrMessage(ensure);
    }

    PS_ValidTempleteNum(&ValidN);           // 更新有效指纹数
    Delay_ms(1200);
}
