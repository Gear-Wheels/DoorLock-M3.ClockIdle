#include "stm32f10x.h"                  // Device header
#include "Key.h"
#include "OLED.h"
#include "MyRTC.h"

#define MENU_NUM		6		//一级菜单项数,菜单项增减时只要改这一处
#define CONFIG			4		//二级功能菜单项数,菜单项增减时只要改这一处
#define COLCK			2		//二级功能菜单项数,菜单项增减时只要改这一处
uint8_t KeyNum;//用于存储键码值

/**
  * @brief 读取RTC并换算为当天秒数,供菜单无操作超时计时用
  * @param  无
  * @retval 当天0点起的秒数,范围0~86399
  */
static uint32_t NowSec(void)
{
	MyRTC_ReadTime();
	return (uint32_t)MyRTC_Time[3]*3600 + MyRTC_Time[4]*60 + MyRTC_Time[5];
}

/**
  * @brief 一级菜单函数,用于显示一级菜单选项
  * @param  无
  * @retval 返回当前选中的是第几项,范围1~MENU_NUM;0=4秒无操作超时退回
  * @note   OLED为128*64,一行16像素,一屏最多只显示4行;
  *         第5项要靠case 5把显示窗口上移一行才能露出来
  */
int menu1(void)
{
	static uint8_t flag=1;
	uint32_t last;					//上次有按键操作的时间(当天秒数)
	int32_t diff;					//与当前时间的差值
	last = NowSec();
	/*初始显示*/
	OLED_ShowString(0,0, "密码解锁",OLED_8X16);
	OLED_ShowString(0,16,"指纹解锁",OLED_8X16);
	OLED_ShowString(0,32,"手机解锁",OLED_8X16);
	OLED_ShowString(0,48,"RFID解锁",OLED_8X16);
	OLED_Update();
	while(1)
	{
		KeyNum = Key_GetNum();
		if(KeyNum != 0)				//有按键操作,重新计时
		{
			last = NowSec();
		}
		else						//无按键,检查是否超时
		{
			diff = (int32_t)(NowSec() - last);
			if(diff < 0)			//跨午夜回绕
			{
				diff += 86400;
			}
			if(diff >= 4)			//4秒无操作,自动退回时钟待机
			{
				OLED_Clear();
				OLED_Update();
				return 0;			//0=超时无选择,main不做分发
			}
		}
		if(KeyNum == 1)//上一项
		{
			/*这里必须用"比较"判断,不能用"等于"判断*/
			if(flag <= 1)			//已经在第一项,回绕到最后一项
			{
				flag = MENU_NUM;
			}
			else					//否则正常上移
			{
				flag --;
			}
		}
		if(KeyNum == 2)//下一项
		{
			if(flag >= MENU_NUM)	//已经在最后一项,回绕到第一项
			{
				flag = 1;
			}
			else					//否则正常下移
			{
				flag ++;
			}
		}
		if(KeyNum == 3)//确认
		{
			OLED_Clear();
			OLED_Update();
			return flag;
		}
		switch(flag)
		{
			case 1:
			{
				OLED_Clear();
				OLED_ShowString(0,0, "密码解锁",OLED_8X16);
				OLED_ShowString(0,16,"指纹解锁",OLED_8X16);
				OLED_ShowString(0,32,"手机解锁",OLED_8X16);
				OLED_ShowString(0,48,"RFID解锁",OLED_8X16);
				OLED_ReverseArea(0,0,128,16);
				OLED_Update(); 
				break;
			}
			case 2:
			{
				OLED_Clear();
				OLED_ShowString(0,0, "密码解锁",OLED_8X16);
				OLED_ShowString(0,16,"指纹解锁",OLED_8X16);
				OLED_ShowString(0,32,"手机解锁",OLED_8X16);
				OLED_ShowString(0,48,"RFID解锁",OLED_8X16);
				OLED_ReverseArea(0,16,128,16);
				OLED_Update(); 
				break;			
			}	
			case 3:
			{
				OLED_Clear();
				OLED_ShowString(0,0, "密码解锁",OLED_8X16);
				OLED_ShowString(0,16,"指纹解锁",OLED_8X16);
				OLED_ShowString(0,32,"手机解锁",OLED_8X16);
				OLED_ShowString(0,48,"RFID解锁",OLED_8X16);
				OLED_ReverseArea(0,32,128,16);
				OLED_Update(); 
				break;			
			}	
			case 4:
			{
				OLED_Clear();
				OLED_ShowString(0,0, "密码解锁",OLED_8X16);
				OLED_ShowString(0,16,"指纹解锁",OLED_8X16);
				OLED_ShowString(0,32,"手机解锁",OLED_8X16);
				OLED_ShowString(0,48,"RFID解锁",OLED_8X16);
				OLED_ReverseArea(0,48,128,16);
				OLED_Update(); 
				break;			
			}
			case 5:
			{
				/*显示窗口上移一行,把"功能设置"露出来*/
				OLED_Clear();
				OLED_ShowString(0,0, "指纹解锁",OLED_8X16);
				OLED_ShowString(0,16,"手机解锁",OLED_8X16);
				OLED_ShowString(0,32,"RFID解锁",OLED_8X16);
				OLED_ShowString(0,48,"功能设置",OLED_8X16);
				OLED_ReverseArea(0,48,128,16);
				OLED_Update(); 
				break;			
			}
			case 6:
			{
				/*显示窗口上移一行,把"时钟显示"露出来*/
				OLED_Clear();
				OLED_ShowString(0,0, "手机解锁",OLED_8X16);
				OLED_ShowString(0,16,"RFID解锁",OLED_8X16);
				OLED_ShowString(0,32,"功能设置",OLED_8X16);
				OLED_ShowString(0,48,"时钟显示",OLED_8X16);
				OLED_ReverseArea(0,48,128,16);
				OLED_Update(); 
				break;			
			}
		}
	}
}

/**
  * @brief 解锁菜单二级密码
  * @param 无
  * @retval 
  */
int menu2_password(void)
{
	// 显示密码输入界面
	OLED_Clear();
	OLED_ShowString(24,16, "请输入密码",OLED_8X16);
	OLED_ShowString(20,33, "_ _ _ _ _ _",OLED_8X16);
	OLED_Update();
	while (1)
    {
		KeyNum = Key_GetNum();
    }
}

/**
  * @brief 指纹解锁二级菜单
  * @param
  * @retval 
  */
int menu2_fingerprint(void)
{	
	OLED_Clear();
	OLED_ShowString(32,24, "请按手指",OLED_8X16);
	OLED_Update();
	//printf("请先按下手指\r\n");
	while(1)
	{
			
	}
}

/**
  * @brief 手机解锁二级菜单
  * @param
  * @retval 
  */
int menu2_Phone(void)
{	
	OLED_Clear();
    OLED_ShowString(24,24, "请验证身份",OLED_8X16);
    OLED_Update();
	while(1)
	{
			
	}
}

/**
  * @brief RFID解锁二级菜单
  * @param
  * @retval 
  */
int menu2_RFID(void)
{	
	OLED_Clear();
    OLED_ShowString(24,24, "请验证身份",OLED_8X16);
    OLED_Update();
	while(1)
	{
			
	}
}

/**
  * @brief 功能设置二级菜单
  * @param
  * @retval 
  */
int menu2_config(void)
{	
	// 标志位，记录二级菜单当前选项
	uint8_t flag=1;
	// 功能界面
	OLED_Clear();
	OLED_ShowString(0,0, "←",OLED_8X16);	
	OLED_ShowString(0,16,"密码修改",OLED_8X16);
	OLED_ShowString(0,32,"指纹设置",OLED_8X16);
	OLED_ShowString(0,48,"RFID设置",OLED_8X16);
	OLED_Update(); 
	while(1)
	{
		KeyNum = Key_GetNum();
		if(KeyNum == 1)//上一项
		{			
			if(flag <= 1)				//已经在第一项,回绕到最后一项
				flag = CONFIG;
			else						//否则正常上移
				flag --;
		}
		if(KeyNum == 2)//下一项
		{
			if(flag >= CONFIG)	//已经在最后一项,回绕到第一项
				flag = 1;
			else						//否则正常下移
				flag ++;
		}
		if(KeyNum == 3)//确认
		{
			OLED_Clear();
			OLED_Update();
			
			if(flag == 1){return 0;}; //若选中"←"则返回上一级菜单
			if(flag == 2){};
			if(flag == 3){};
			if(flag == 4){};
		}
				
		switch (flag)
        {
        	case 1:
			{
				OLED_Clear();
				OLED_ShowString(0,0, "←",OLED_8X16);	
				OLED_ShowString(0,16,"密码修改",OLED_8X16);
				OLED_ShowString(0,32,"指纹设置",OLED_8X16);
				OLED_ShowString(0,48,"RFID设置",OLED_8X16);
				OLED_ReverseArea(0,0,128,16);
				OLED_Update();
        	break;
			}

        	case 2:
			{
				OLED_Clear();
				OLED_ShowString(0,0, "←",OLED_8X16);	
				OLED_ShowString(0,16,"密码修改",OLED_8X16);
				OLED_ShowString(0,32,"指纹设置",OLED_8X16);
				OLED_ShowString(0,48,"RFID设置",OLED_8X16);
				OLED_ReverseArea(0,16,128,16);
				OLED_Update(); 
				break;
			}
			case 3:
			{
				OLED_Clear();
				OLED_ShowString(0,0, "←",OLED_8X16);	
				OLED_ShowString(0,16,"密码修改",OLED_8X16);
				OLED_ShowString(0,32,"指纹设置",OLED_8X16);
				OLED_ShowString(0,48,"RFID设置",OLED_8X16);
				OLED_ReverseArea(0,32,128,16);
				OLED_Update(); 
				break;
			}
			case 4:
			{
				OLED_Clear();
				OLED_ShowString(0,0, "←",OLED_8X16);	
				OLED_ShowString(0,16,"密码修改",OLED_8X16);
				OLED_ShowString(0,32,"指纹设置",OLED_8X16);
				OLED_ShowString(0,48,"RFID设置",OLED_8X16);
				OLED_ReverseArea(0,48,128,16);
				OLED_Update(); 
				break;
			}			
        }
	}
}
/**
* @brief 时钟显示二级密码
  * @param 无
  * @retval 
  */
int menu2_Clock(void)
{
	// 标志位，记录二级菜单当前选项
	uint8_t flag=1;
	// 功能界面
	OLED_Clear();
	OLED_ShowString(0,0, "←",OLED_8X16);	
	OLED_Printf(0,16,OLED_8X16,"Data:%d-%d-%d",MyRTC_Time[0],MyRTC_Time[1],MyRTC_Time[2]);
	OLED_Printf(0,32,OLED_8X16,"Time:%d:%d:%d",MyRTC_Time[3],MyRTC_Time[4],MyRTC_Time[5]);
	OLED_ShowString(0,48,"            设置",OLED_8X16);
	OLED_Update(); 
	while (1)
    {
		MyRTC_ReadTime();
 		KeyNum = Key_GetNum();
		if(KeyNum == 1)//上一项
		{			
			if(flag <= 1)				//已经在第一项,回绕到最后一项
				flag = COLCK;
			else						//否则正常上移
				flag --;
		}
		if(KeyNum == 2)//下一项
		{
			if(flag >= COLCK)	//已经在最后一项,回绕到第一项
				flag = 1;
			else						//否则正常下移
				flag ++;
		}
		if(KeyNum == 3)//确认
		{
			OLED_Clear();
			OLED_Update();
			
			if(flag == 1){return 0;}; //若选中"←"则返回上一级菜单
			if(flag == 2){};
			if(flag == 3){};
			if(flag == 4){};
		}
		switch(flag)
		{
			case 1:
			{
				OLED_Clear();
				OLED_ShowString(0,0, "←",OLED_8X16);	
				OLED_Printf(0,16,OLED_8X16,"Data:%d-%d-%d",MyRTC_Time[0],MyRTC_Time[1],MyRTC_Time[2]);
				OLED_Printf(0,32,OLED_8X16,"Time:%d:%d:%d",MyRTC_Time[3],MyRTC_Time[4],MyRTC_Time[5]);
				OLED_ShowString(0,48,"            设置",OLED_8X16);
				OLED_ReverseArea(0,0,16,16);
				OLED_Update();
			break;
		}
		case 2:
		{
			OLED_Clear();
			OLED_ShowString(0,0, "←",OLED_8X16);
			OLED_Printf(0,16,OLED_8X16,"Date:%d-%d-%d",MyRTC_Time[0],MyRTC_Time[1],MyRTC_Time[2]);
			OLED_Printf(0,32,OLED_8X16,"Time:%d:%d:%d",MyRTC_Time[3],MyRTC_Time[4],MyRTC_Time[5]);
			OLED_ShowString(0,48,"            设置",OLED_8X16);
			OLED_ReverseArea(96,48,32,16);
			OLED_Update();
			break;
		}

	}
}
}

/**
  * @brief 时钟待机界面-内部绘制函数,每次调用都整屏重画
  * @param  无
  * @retval 无
  */
static void IdleClock_Show(void)
{
	MyRTC_ReadTime();
	OLED_Clear();
	OLED_Printf(8,8, OLED_8X16,"Date:%d-%d-%d",MyRTC_Time[0],MyRTC_Time[1],MyRTC_Time[2]);
	OLED_Printf(8,32,OLED_8X16,"Time:%d:%d:%d",MyRTC_Time[3],MyRTC_Time[4],MyRTC_Time[5]);
	OLED_Update();
}

/**
  * @brief 时钟待机界面-完整绘制,上电或从菜单返回待机时调用一次
  * @param  无
  * @retval 无
  */
void IdleClock_Draw(void)
{
	IdleClock_Show();
}

/**
  * @brief 时钟待机界面-非阻塞刷新,main主循环每圈调用
  * @param  无
  * @retval 无
  * @note   只在秒数变化时才重画,避免I2C满速刷屏
  */
void IdleClock_Tick(void)
{
	static uint16_t last_sec = 0xFFFF;	//初值取不可能值,保证第一次必刷新
	MyRTC_ReadTime();
	if(MyRTC_Time[5] != last_sec)
	{
		last_sec = MyRTC_Time[5];
		IdleClock_Show();
	}
}
