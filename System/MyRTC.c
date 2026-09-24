#include "stm32f10x.h"                  // Device header
#include "MyRTC.h"
#include <time.h>

uint16_t MyRTC_Time[] = {2023, 4, 1, 12, 59, 55};

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
