#include "stm32f10x.h"                  // Device header
#include "Delay.h"
#include "OLED.h"
#include "Key.h"
#include "menu.h"
#include "MyRTC.h"

int main(void)
{
	// 标志位"选择"，承接一级菜单的返回值，用来判断进入哪个二级菜单
	int choice;
	// 事件码，承接 Event_GetNum 的返回值
	uint8_t event;

	// 模块初始化
	OLED_Init();
	Key_Init();
	MyRTC_Init();

	// 上电默认进入时钟待机界面
	IdleClock_Draw();

	// 主循环：非阻塞轮询，刷时钟 + 扫事件 + 按需分发
	while (1)
	{
		// 秒变化才重画时钟，无按键时不会刷爆 I2C
		IdleClock_Tick();

		// 聚合事件：1=上 2=下 3=确认 10=键盘(预留) 11=指纹(预留)
		event = Event_GetNum();

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
		}

		if(event == 10)								//密码键盘有键按下(预留,stub暂不触发)
		{
			menu2_password();
			IdleClock_Draw();
		}

		if(event == 11)								//指纹模块检测到手指(预留,stub暂不触发)
		{
			menu2_fingerprint();
			IdleClock_Draw();
		}
	}
}
