#include "stm32f10x.h"                  // Device header
#include "MyRTC.h"
#include <time.h>

/*==============================================================================
 * MyRTC.c —— RTC 实时时钟驱动（让门锁能显示日期和时间）
 *
 * 【RTC 是什么】
 *   RTC = Real Time Clock（实时时钟），是 STM32 内置的一个"电子表"，
 *   只要给它接一个时钟源（这里用 LSE 外部 32.768kHz 晶振），
 *   它就能一直走秒、分、时，哪怕主程序在睡觉，它也在计数。
 *
 * 【LSE 时钟】
 *   32.768kHz 晶振是 RTC 的"标配"频率——因为 32768 = 2^15，
 *   把它除以 32768 正好得到 1Hz（每秒 1 个脉冲），计时最方便。
 *
 * 【时区处理】
 *   本文件里 MyRTC_Time 存的是"东八区（北京时间）"。
 *   因为 RTC 计数器存的是"从 1970-01-01 00:00:00 起的秒数"（UTC 时间），
 *   而中国是 UTC+8，所以读取时要 +8*3600 秒换算成本地时间。
 *
 * 【BKP 备份寄存器的作用】
 *   BKP_DR1 里写 0xA5A5 作为"已经初始化过 RTC"的标记。
 *   因为 RTC 在断电（用电池供电）后仍会继续走，不需要每次上电都重新设置时间，
 *   否则一上电就把时间重置了。所以只有第一次（没标记时）才设置时间。
 *============================================================================*/

// 时间数组：{年, 月, 日, 时, 分, 秒}，初始值 2023-04-01 12:59:55
uint16_t MyRTC_Time[] = {2023, 4, 1, 12, 59, 55};

/**
  * @brief RTC 初始化（设置时钟源、预分频、初始时间）
  * @param  无
  * @retval 无
  * @note   第一次上电（BKP_DR1 无 0xA5A5 标记）才完整初始化并设置时间；
  *         之后上电只做同步，不重置时间，保证断电（有电池）时间不丢。
  */
void MyRTC_Init(void)
{
	// 使能PWR（电源控制）和BKP（备份寄存器）时钟
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR, ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_BKP, ENABLE);
	
	// 使能对备份寄存器（BKP）和RTC寄存器的写保护。不执行此操作将无法修改RTC配置
	PWR_BackupAccessCmd(ENABLE);
	
	if (BKP_ReadBackupRegister(BKP_DR1) != 0xA5A5)
	{	
		// 开启LSE, 循环等待LSE就绪
		RCC_LSEConfig(RCC_LSE_ON);
		while (RCC_GetFlagStatus(RCC_FLAG_LSERDY) != SET);

		// 选择RTC时钟源并启用RTC
		RCC_RTCCLKConfig(RCC_RTCCLKSource_LSE);
		RCC_RTCCLKCmd(ENABLE);

		// 等待RTC同步和上一次操作完成
		RTC_WaitForSynchro();
		RTC_WaitForLastTask();

		// 设置预分频器, 等待上次RTC操作（如配置、写入）完成
		RTC_SetPrescaler(32768-1);
		RTC_WaitForLastTask();

		// 设置初始计数值, 等待上次RTC操作（如配置、写入）完成
		//	RTC_SetCounter(1672588795);
		//	RTC_WaitForLastTask();

		MyRTC_SetTime();	
		
		BKP_WriteBackupRegister(BKP_DR1, 0xA5A5);
	}
	else
	{
		// 等待RTC同步和上一次操作完成
		RTC_WaitForSynchro();
		RTC_WaitForLastTask();	
	}
}

/**
  * @brief 把 MyRTC_Time[] 里的时间写入 RTC 计数器
  * @param  无
  * @retval 无
  * @note   用 mktime 把"年月日时分秒"转成"1970 起的总秒数"，再减 8 小时
  *         （因为要写入的是 UTC 时间，而 MyRTC_Time 存的是北京时间）。
  */
void MyRTC_SetTime(void)
{
	time_t time_cnt;
	struct tm time_date;
	
	time_date.tm_year = MyRTC_Time[0] - 1900;
	time_date.tm_mon = MyRTC_Time[1] - 1;
	time_date.tm_mday = MyRTC_Time[2];
	time_date.tm_hour = MyRTC_Time[3];
	time_date.tm_min = MyRTC_Time[4];
	time_date.tm_sec = MyRTC_Time[5];
	
	time_cnt = mktime(&time_date) - 8 * 60 * 60;
	
	RTC_SetCounter(time_cnt);
	RTC_WaitForLastTask();
}

/**
  * @brief 从 RTC 计数器读取当前时间，更新到 MyRTC_Time[] 数组
  * @param  无
  * @retval 无
  * @note   读出的 UTC 秒数 +8 小时转成北京时间，再用 localtime 拆成年月日时分秒。
  *         调用后 MyRTC_Time[0~5] = 年/月/日/时/分/秒。
  */
void MyRTC_ReadTime(void)
{
	time_t time_cnt;
	struct tm time_date;

	time_cnt = RTC_GetCounter() + 8 * 60 * 60;
	
	time_date = *localtime(&time_cnt);
	
	MyRTC_Time[0] = time_date.tm_year + 1900;
	MyRTC_Time[1] = time_date.tm_mon + 1;
	MyRTC_Time[2] = time_date.tm_mday;
	MyRTC_Time[3] = time_date.tm_hour;
	MyRTC_Time[4] = time_date.tm_min;
	MyRTC_Time[5] = time_date.tm_sec;	
}
