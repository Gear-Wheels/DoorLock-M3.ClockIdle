#include "stm32f10x.h"                  // Device header
#include "LowPower.h"
#include "OLED.h"                       // OLED_DisplayOff/On 熄屏亮屏
#include "usart2.h"                     // usart2_init / TIM4_Int_Init（醒来重建）
#include "Delay.h"                      // Delay_ms

/*==============================================================================
 * 低功耗 Stop 模式驱动（由 CountSensor 移植改造而来）
 *
 * 原 CountSensor 是"对射式红外计数传感器"例程（PB14 外部中断计数），
 * 这里改造成门锁的"事件唤醒源"：三个板载键 + 指纹 WAK 脚走 EXTI。
 *
 * 核心链路（最容易翻车的地方）：
 *   进 Stop 前：熄屏 + 关 USART2/TIM4 + 配好 EXTI，再 PWR_EnterSTOPMode。
 *   醒来后：  SystemInit() 恢复 HSE+PLL=72MHz（否则 Delay 失准、外设时钟错），
 *             然后重建 GPIO 时钟、USART2/TIM4、亮屏。
 *   原因：Stop 会关 HSE 和 PLL，醒来回 HSI 8MHz；SystemInit 复位 RCC 会清掉
 *         之前配置的外设时钟和 GPIO，所以必须重建。
 *============================================================================*/

/* 全局唤醒标志：EXTI 中断里置位，main 主循环轮询后清零 */
volatile uint8_t WakeUpFlag = 0;

/*------------------------------------------------------------------------------
 * 函    数：配置单个 EXTI 外部中断线
 * 参    数：port  GPIO 端口（GPIOA/GPIOB）
 *           pin   GPIO 引脚
 *           src   引脚源编号（GPIO_PinSourceX）
 *           line  EXTI 线（EXTI_LineX）
 *           irqn  NVIC 中断通道
 *           trigger 触发方式（EXTI_Trigger_Falling 下降沿 / Rising 上升沿）
 *------------------------------------------------------------------------------*/
static void LowPower_EXTI_Config(GPIO_TypeDef *port, uint16_t pin,
                                 uint8_t src, uint32_t line, uint8_t irqn,
                                 EXTITrigger_TypeDef trigger)
{
    GPIO_InitTypeDef   GPIO_InitStructure;
    EXTI_InitTypeDef   EXTI_InitStructure;
    NVIC_InitTypeDef   NVIC_InitStructure;

    /* 1. 使能 GPIO 时钟 + AFIO 时钟（EXTI 必须开 AFIO） */
    if (port == GPIOA) RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    if (port == GPIOB) RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);

    /* 2. GPIO 初始化为上拉输入（与 Key.c 里按键配置一致，避免冲突） */
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_IPU;
    GPIO_InitStructure.GPIO_Pin   = pin;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(port, &GPIO_InitStructure);

    /* 3. AFIO 把 EXTI 线映射到指定端口引脚 */
    GPIO_EXTILineConfig((port == GPIOA) ? GPIO_PortSourceGPIOA : GPIO_PortSourceGPIOB, src);

    /* 4. 配置 EXTI 线 */
    EXTI_InitStructure.EXTI_Line    = line;
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_InitStructure.EXTI_Mode    = EXTI_Mode_Interrupt;
    EXTI_InitStructure.EXTI_Trigger = trigger;
    EXTI_Init(&EXTI_InitStructure);

    /* 5. NVIC 分组（只在首次初始化时配一次即可，重复调用无害） */
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    NVIC_InitStructure.NVIC_IRQChannel                   = irqn;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 1;
    NVIC_Init(&NVIC_InitStructure);
}

/*------------------------------------------------------------------------------
 * 函    数：低功耗唤醒源初始化
 * 说    明：把三个板载键 + 指纹 WAK 脚配成 EXTI 外部中断，作为 Stop 唤醒源。
 *           main 上电时调用一次即可。
 *------------------------------------------------------------------------------*/
void LowPower_Init(void)
{
    /* 三个板载键：下降沿（按下拉低） */
    LowPower_EXTI_Config(WAKE_KEY1_PORT, WAKE_KEY1_PIN, GPIO_PinSource11,
                         WAKE_KEY1_EXLINE, WAKE_KEY1_IRQn, EXTI_Trigger_Falling);
    LowPower_EXTI_Config(WAKE_KEY2_PORT, WAKE_KEY2_PIN, GPIO_PinSource1,
                         WAKE_KEY2_EXLINE, WAKE_KEY2_IRQn, EXTI_Trigger_Falling);
    LowPower_EXTI_Config(WAKE_KEY3_PORT, WAKE_KEY3_PIN, GPIO_PinSource6,
                         WAKE_KEY3_EXLINE, WAKE_KEY3_IRQn, EXTI_Trigger_Falling);

    /* 指纹 WAK 脚：上升沿（模块检测到触摸输出高电平） */
    LowPower_EXTI_Config(WAKE_FP_PORT, WAKE_FP_PIN, GPIO_PinSource4,
                         WAKE_FP_EXLINE, WAKE_FP_IRQn, EXTI_Trigger_Rising);
}

/*------------------------------------------------------------------------------
 * 函    数：进入 Stop 停机模式
 * 说    明：熄屏 + 关 USART2/TIM4 + 清挂起标志，然后 PWR_EnterSTOPMode 进 Stop。
 *           CPU 停在这里，直到任一 EXTI 唤醒，然后从本函数返回。
 *------------------------------------------------------------------------------*/
void EnterStopMode(void)
{
    /* 1. 熄屏省电（OLED 是耗电大头） */
    OLED_DisplayOff();

    /* 2. 关掉指纹模块相关外设（USART2/TIM4），省电 + 避免 Stop 后状态异常 */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, DISABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM4, DISABLE);

    /* 3. 清掉可能残留的 EXTI 挂起标志，避免一进 Stop 就被误唤醒 */
    EXTI_ClearITPendingBit(WAKE_KEY1_EXLINE);
    EXTI_ClearITPendingBit(WAKE_KEY2_EXLINE);
    EXTI_ClearITPendingBit(WAKE_KEY3_EXLINE);
    EXTI_ClearITPendingBit(WAKE_FP_EXLINE);

    WakeUpFlag = 0;                     // 清唤醒标志

    /* 4. 进入 Stop 模式：低功耗稳压器 + WFI 等待中断 */
    PWR_EnterSTOPMode(PWR_Regulator_LowPower, PWR_STOPEntry_WFI);

    /* 5. 被 EXTI 唤醒后，从这里继续执行（不是复位，变量都还在） */
}

/*------------------------------------------------------------------------------
 * 函    数：退出 Stop 模式（唤醒后恢复）
 * 说    明：Stop 关掉了 HSE/PLL，醒来回 HSI 8MHz，必须恢复时钟树；
 *           SystemInit 会复位 RCC，所以还要重建外设时钟和 GPIO、亮屏。
 *------------------------------------------------------------------------------*/
void ExitStopMode(void)
{
    /* 1. 恢复系统时钟：HSE 8MHz + PLL×9 = 72MHz（最关键，否则 Delay 失准） */
    SystemInit();

    /* 2. 重新使能各 GPIO 时钟（SystemInit 复位了 RCC，之前开的时钟没了） */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);

    /* 3. 重新初始化受影响的外设：OLED 的 GPIO 和屏（先重建 GPIO 再亮屏） */
    OLED_Init();                        // 重建 OLED GPIO + 初始化屏（含开显示）
    OLED_DisplayOn();                   // 确保亮屏

    /* 4. 重建指纹模块通讯：USART2（内部会连带重建 TIM4 判帧 + 开时钟） */
    usart2_init(57600);                 // 与 fingerprint.h 的 USART2_BAUND 一致

    /* 5. 清 EXTI 挂起标志，准备下一次唤醒 */
    EXTI_ClearITPendingBit(WAKE_KEY1_EXLINE);
    EXTI_ClearITPendingBit(WAKE_KEY2_EXLINE);
    EXTI_ClearITPendingBit(WAKE_KEY3_EXLINE);
    EXTI_ClearITPendingBit(WAKE_FP_EXLINE);

    WakeUpFlag = 0;                     // 清唤醒标志
}

/*==============================================================================
 * 中断处理函数（EXTI 唤醒）
 * 说    明：中断里只置唤醒标志，不做重活；main 主循环检测到标志后再处理。
 *==============================================================================*/

/* EXTI1：板载键2（PB1） */
void EXTI1_IRQHandler(void)
{
    if (EXTI_GetITStatus(WAKE_KEY2_EXLINE) == SET)
    {
        if (GPIO_ReadInputDataBit(WAKE_KEY2_PORT, WAKE_KEY2_PIN) == 0)
        {
            WakeUpFlag = 1;
        }
        EXTI_ClearITPendingBit(WAKE_KEY2_EXLINE);
    }
}

/* EXTI4：指纹 WAK（PA4） */
void EXTI4_IRQHandler(void)
{
    if (EXTI_GetITStatus(WAKE_FP_EXLINE) == SET)
    {
        WakeUpFlag = 1;
        EXTI_ClearITPendingBit(WAKE_FP_EXLINE);
    }
}

/* EXTI9_5：板载键3（PA6） */
void EXTI9_5_IRQHandler(void)
{
    if (EXTI_GetITStatus(WAKE_KEY3_EXLINE) == SET)
    {
        if (GPIO_ReadInputDataBit(WAKE_KEY3_PORT, WAKE_KEY3_PIN) == 0)
        {
            WakeUpFlag = 1;
        }
        EXTI_ClearITPendingBit(WAKE_KEY3_EXLINE);
    }
}

/* EXTI15_10：板载键1（PB11） */
void EXTI15_10_IRQHandler(void)
{
    if (EXTI_GetITStatus(WAKE_KEY1_EXLINE) == SET)
    {
        if (GPIO_ReadInputDataBit(WAKE_KEY1_PORT, WAKE_KEY1_PIN) == 0)
        {
            WakeUpFlag = 1;
        }
        EXTI_ClearITPendingBit(WAKE_KEY1_EXLINE);
    }
}
