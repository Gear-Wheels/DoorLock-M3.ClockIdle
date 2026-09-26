#include "stm32f10x.h"                  // Device header
#include "Delay.h"
#include "OLED.h"
#include "Key.h"
#include "menu.h"
#include "MyRTC.h"
#include "MKey.h"
#include "fingerprint.h"                // 指纹模块初始化（FR_Init，结果用 OLED 显示）
#include "LowPower.h"                   // 低功耗 Stop 模式 + 事件唤醒

/*==============================================================================
 * main.c —— 门锁项目的"总入口"和"大管家"
 *
 * 【这个文件干了三件事】
 *   1. 上电初始化：把 OLED、按键、键盘、RTC、指纹、低功耗全部初始化好。
 *   2. 主循环：一个 while(1) 死循环，不停地"刷时间 → 扫事件 → 处理事件"。
 *   3. 待机状态机：在"亮屏(活跃态)"和"熄屏省电(Stop态)"之间切换。
 *
 * 【整个项目的运行逻辑（一句话看懂）】
 *   上电 → 初始化所有模块 → 显示时钟待机界面 →
 *   然后循环：有按键/触摸等"事件"就进入对应功能（菜单/开锁），
 *   没事件超过 12 秒就熄屏进低功耗睡觉，等按键再唤醒。
 *
 * 【和 menu.c 的分工】
 *   main.c 只负责"顶层调度"：初始化、循环、状态切换、把事件分发到菜单。
 *   具体菜单怎么画、怎么跳转，全在 menu.c 里。main 不关心菜单细节。
 *============================================================================*/

/* 待机状态机：ACTIVE=亮屏正常轮询，STOP=熄屏进低功耗 */
typedef enum
{
	STATE_ACTIVE = 0,                   // 活跃态：亮屏显示时间
	STATE_STOP   = 1                    // 熄屏态：进 Stop 低功耗等唤醒
} IdleState_t;

#define IDLE_TIMEOUT    12              // 无操作 12 秒后熄屏进低功耗

/*------------------------------------------------------------------------------
 * 内部辅助：读取当前"当天秒数"，用于无操作计时。
 * 与 menu.c 的 NowSec 同理：读 RTC 计数器（LSE 独立于主时钟，Stop 后仍准）。
 *------------------------------------------------------------------------------*/
static uint32_t GetNowSec(void)
{
	MyRTC_ReadTime();
	return (uint32_t)MyRTC_Time[3] * 3600 + MyRTC_Time[4] * 60 + MyRTC_Time[5];
}

/**
  * @brief 主函数：初始化所有模块，然后进入主循环（状态机 + 事件分发）
  * @param  无
  * @retval 无（死循环，永不返回）
  * @note   主循环每圈做：刷时间(活跃态) → 采事件 → 有事件则分发到对应菜单，
  *         无事件 12 秒则熄屏进低功耗 Stop，等 EXTI 唤醒后恢复。
  */
int main(void)
{
	// 标志位"选择"，承接一级菜单的返回值，用来判断进入哪个二级菜单
	int choice;
	// 事件码，承接 Event_GetNum 的返回值
	uint8_t event;

	// 低功耗状态机
	IdleState_t state = STATE_ACTIVE;   // 上电默认活跃态（亮屏）
	uint32_t last_active;               // 上次有事件的时间（当天秒数）
	int32_t  diff;                      // 与当前时间的差值

	// 模块初始化
	OLED_Init();
	Key_Init();
	MKey_Init();
	MyRTC_Init();
	FR_Init();                          // 指纹模块初始化（握手结果在 OLED 上显示）
	LowPower_Init();                    // 低功耗唤醒源初始化（EXTI）

	// 上电默认进入时钟待机界面
	IdleClock_Draw();
	last_active = GetNowSec();          // 初始化活跃时间戳

	// 主循环：非阻塞轮询，刷时钟 + 扫事件 + 按需分发 + 低功耗状态切换
	while (1)
	{
		if (state == STATE_ACTIVE)      // ========== 活跃态：亮屏正常轮询 ==========
		{
			// 秒变化才重画时钟，无按键时不会刷爆 I2C
			IdleClock_Tick();

			// 聚合事件：1=上 2=下 3=确认 10=键盘(预留) 11=指纹(预留)
			event = Event_GetNum();

			if (event != 0)             // 有事件：刷新活跃时间戳，并处理事件
			{
				last_active = GetNowSec();

				if(event == 3)								//确认键：进入一级菜单
				{
					choice = menu1();
					if(choice == 1){menu2_password();}		//返回1时进入密码解锁
					if(choice == 2){menu2_fingerprint();}	//返回2时进入指纹解锁
					if(choice == 3){menu2_Phone();}			//返回3时进入手机解锁
					if(choice == 4){menu2_RFID();}			//返回4时进入RFID解锁
					if(choice == 5){menu2_config();}		//返回5时进入功能设置
					//choice==6(时钟显示)或0(4秒超时)：不做任何调用，直接落回待机
					IdleClock_Draw();						//菜单返回后重画待机界面
					last_active = GetNowSec();				//菜单操作完重新计时
				}

				if(event == 10)								//密码键盘有键按下(预留,stub暂不触发)
				{
					menu2_password();
					IdleClock_Draw();
					last_active = GetNowSec();
				}

				if(event == 11)								//指纹模块检测到手指(预留,stub暂不触发)
				{
					menu2_fingerprint();
					IdleClock_Draw();
					last_active = GetNowSec();
				}
			}
			else                        // 无事件：检查是否超时熄屏
			{
				diff = (int32_t)(GetNowSec() - last_active);
				if (diff < 0) diff += 86400;        // 跨午夜回绕
				if (diff >= IDLE_TIMEOUT)           // 12 秒无操作
				{
					state = STATE_STOP;             // 切到熄屏态
				}
			}
		}
		else                            // ========== 熄屏态：进 Stop 等唤醒 ==========
		{
			EnterStopMode();            // 熄屏 + 关外设 + 进 Stop（停在这里等 EXTI）

			// 被 EXTI 唤醒后落到这里：恢复时钟 + 重建外设 + 亮屏
			ExitStopMode();

			// 回到活跃态，重画待机界面
			state = STATE_ACTIVE;
			last_active = GetNowSec();
			IdleClock_Draw();
		}
	}
}
