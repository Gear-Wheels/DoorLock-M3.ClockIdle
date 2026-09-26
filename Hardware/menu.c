/*==============================================================================
 * menu.c —— 菜单系统（门锁项目的"界面大脑"）
 *
 * 【这个文件管什么】
 *   所有屏幕界面都由这里负责：一级菜单、二级菜单（密码/指纹/手机/RFID/
 *   功能设置/时钟）、三级菜单（指纹设置）、以及待机时钟界面。
 *
 * 【菜单是怎么设计的】
 *   菜单采用"状态机 + 按键轮询"的方式：
 *   - 每个菜单是一个 while(1) 循环，圈里不断读按键(Key_GetNum)。
 *   - 用一个变量 flag 记录"当前选中第几项"。
 *   - 上键(1)/下键(2) 改变 flag，再根据 flag 反显对应那一行。
 *   - 确认键(3) 根据 flag 决定跳转到哪个功能，然后 return 出去。
 *
 * 【菜单之间怎么跳转/回退】
 *   关键约定：菜单函数通过"返回值"告诉上一层自己该怎么走。
 *   - menu1() 返回 choice(1~6)，main.c 用 if 判断进入对应二级菜单。
 *   - 二级/三级菜单里，选中"←"返回上一级 → return 0。
 *   - 4 秒无操作超时 → return -1，逐层往上抛，最终回待机时钟界面。
 *   - main.c 在菜单返回后无条件调用 IdleClock_Draw() 重画待机界面兜底。
 *
 * 【OLED 屏的局限（重要）】
 *   OLED 是 128x64，每行 16 像素高，所以一屏最多显示 4 行。
 *   一级菜单有 6 项，只能通过"窗口上移"来露出第 5、6 项（见 menu1 的
 *   case 5 / case 6，它们把整屏文字往上挪一行）。
 *============================================================================*/

#include "stm32f10x.h"                  // Device header
#include "Key.h"
#include "OLED.h"
#include "MyRTC.h"
#include "CodedLock.h"
#include "fingerprint.h"

#define MENU_NUM		6		//一级菜单项数(密码/指纹/手机/RFID/功能设置/时钟),增减项只改这里
#define CONFIG			4		//"功能设置"二级菜单项数(←/密码修改/指纹设置/RFID设置)
#define COLCK			2		//"时钟"二级菜单项数(←/设置)
uint8_t KeyNum;					//全局键码值：各菜单函数共用，存 Key_GetNum 的返回值(1上/2下/3确认)

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
  * @brief 一级菜单（主菜单）：显示 6 个开锁方式入口
  * @param  无
  * @retval 返回当前选中的第几项(1~MENU_NUM)；0=4秒无操作超时退回
  * @note   1) OLED 为 128x64、每行 16 像素，一屏最多显示 4 行；6 项菜单靠
  *            case 5/6 把整屏文字往上挪一行，才能露出"功能设置""时钟显示"。
  *         2) 选中项用 OLED_ReverseArea 反色表示（黑底白字）。
  *         3) flag 上下移动采用"回绕"：到第 1 项再按上键跳到第 6 项，反之亦然。
  *         4) 本函数不负责进入二级菜单，只 return flag，由 main.c 决定跳转。
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
  * @brief 二级菜单：密码解锁
  * @param  无
  * @retval CodedLock() 的返回值：1=解锁成功；0=返回/未解锁
  * @note   直接转交给 CodedLock() 处理密码输入比对，本函数只是个"壳"，
  *         目的是让 main.c 的跳转逻辑统一（都通过 menu2_xxx 进入）。
  */
int menu2_password(void)
{
	return CodedLock();
}

/**
  * @brief 二级菜单：指纹解锁
  * @param  无
  * @retval 0（解锁流程结束返回待机）
  * @note   调用 press_FR() 完成"等待手指 → 采集 → 搜索比对"，
  *         成功/失败的结果在 press_FR 内部用 OLED 显示。
  */
int menu2_fingerprint(void)
{	
	press_FR();					// 刷指纹解锁（等待触摸 -> 采集 -> 搜索比对）
	return 0;					// 解锁流程结束，返回待机
}

/**
  * @brief 二级菜单：手机解锁（预留，未实现）
  * @param  无
  * @retval 无（当前是死循环空壳，需后续接入手机开锁模块）
  * @note   TODO：目前只显示"请验证身份"后进入空 while(1)，
  *         后续接入蓝牙/WiFi 手机开锁时在此实现。
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
  * @brief 二级菜单：RFID 解锁（预留，未实现）
  * @param  无
  * @retval 无（当前是死循环空壳，需后续接入 RFID 模块）
  * @note   TODO：RFID.c 里已有卡片录入/删除/验证函数，后续接入时在此调用。
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
  * @brief 二级菜单：功能设置（密码修改 / 指纹设置 / RFID设置）
  * @param  无
  * @retval 0 = 正常返回上一级；-1 = 超时或子操作完成需退回待机时钟
  * @note   1) flag=1"←"返回、flag=2密码修改、flag=3指纹设置、flag=4 RFID设置(预留)
  *         2) 密码修改成功后 return -1（改完直接回时钟待机）；
  *         3) 指纹设置进入三级菜单，若三级超时也 return -1 逐层退回。
  *         4) 本菜单有 4 秒无操作超时，避免一直停在设置界面。
  */
int menu2_config(void)
{	
	// 标志位，记录二级菜单当前选项
	uint8_t flag=1;
	uint32_t last;				//上次有按键操作的时间(当天秒数)
	int32_t diff;				//与当前时间的差值
	last = NowSec();
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
		if(KeyNum != 0)			//有按键操作,重新计时
		{
			last = NowSec();
		}
		else					//无按键,检查是否超时
		{
			diff = (int32_t)(NowSec() - last);
			if(diff < 0)		//跨午夜回绕
			{
				diff += 86400;
			}
			if(diff >= 4)		//4秒无操作,自动退回时钟待机
			{
				OLED_Clear();
				OLED_Update();
				return -1;
			}
		}
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
			if(flag == 2){if(ChangePassword() == 1) return -1;}; //密码修改 -> 成功后返回-1退回待机(时钟)
			if(flag == 3){if(menu3_fpconfig() == -1) return -1;}; //指纹设置 -> 进入三级菜单,三级超时则本层也退回
			if(flag == 4){}; //RFID设置(预留)
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
  * @brief 二级菜单：时钟显示（查看日期时间 / 进入时间设置）
  * @param  无
  * @retval 0 = 选中"←"返回上一级
  * @note   1) 上半屏实时显示 RTC 的日期(Data)和时间(Time)。
  *         2) while 循环里每圈都 MyRTC_ReadTime() 刷新时间。
  *         3) flag=1"←"、flag=2"设置"（设置项目前是空壳，预留改时间用）。
  *         4) 注意：本菜单暂无超时逻辑（历史遗留），后续可参照 menu2_config 补。
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
  * @brief 指纹设置三级菜单（查看/录入/删除）
  * @param  无
  * @retval 0 = 正常返回上一级（功能设置）；-1 = 4秒超时退回待机
  * @note   1) 从"功能设置 -> 指纹设置"进入，串联查看/录入/删除三个操作。
  *         2) 确认进入某个操作后，操作结束会重画本菜单（不清屏退出）。
  *         3) 超时返回 -1，让上层 menu2_config 感知后也 -1 逐层退回待机。
  */
int menu3_fpconfig(void)
{
	uint8_t flag = 1;			// 1=返回 2=指纹查看 3=指纹录入 4=指纹删除
	uint32_t last;				//上次有按键操作的时间(当天秒数)
	int32_t diff;				//与当前时间的差值
	last = NowSec();
	OLED_Clear();
	OLED_ShowString(0,0, "←",OLED_8X16);
	OLED_ShowString(0,16,"指纹查看",OLED_8X16);
	OLED_ShowString(0,32,"指纹录入",OLED_8X16);
	OLED_ShowString(0,48,"指纹删除",OLED_8X16);
	OLED_Update();
	while(1)
	{
		KeyNum = Key_GetNum();
		if(KeyNum != 0)			//有按键操作,重新计时
		{
			last = NowSec();
		}
		else					//无按键,检查是否超时
		{
			diff = (int32_t)(NowSec() - last);
			if(diff < 0)		//跨午夜回绕
			{
				diff += 86400;
			}
			if(diff >= 4)		//4秒无操作,自动退回时钟待机
			{
				OLED_Clear();
				OLED_Update();
				return -1;
			}
		}
		if(KeyNum == 1)//上一项
		{
			if(flag <= 1) flag = 4;		//第一项回绕到最后
			else flag --;
		}
		if(KeyNum == 2)//下一项
		{
			if(flag >= 4) flag = 1;		//最后一项回绕到第一
			else flag ++;
		}
		if(KeyNum == 3)//确认
		{
			OLED_Clear();
			OLED_Update();
			if(flag == 1){ return 0; }			//返回上一级(功能设置)
			if(flag == 2){ View_FR(); }			//指纹查看
			if(flag == 3){ Add_FR(); }			//指纹录入
			if(flag == 4){ Del_FR(); }			//指纹删除
			/* 操作结束后重画本菜单 */
			OLED_Clear();
			OLED_ShowString(0,0, "←",OLED_8X16);
			OLED_ShowString(0,16,"指纹查看",OLED_8X16);
			OLED_ShowString(0,32,"指纹录入",OLED_8X16);
			OLED_ShowString(0,48,"指纹删除",OLED_8X16);
			OLED_Update();
		}
		switch(flag)
		{
			case 1:
			{
				OLED_Clear();
				OLED_ShowString(0,0, "←",OLED_8X16);
				OLED_ShowString(0,16,"指纹查看",OLED_8X16);
				OLED_ShowString(0,32,"指纹录入",OLED_8X16);
				OLED_ShowString(0,48,"指纹删除",OLED_8X16);
				OLED_ReverseArea(0,0,128,16);
				OLED_Update();
				break;
			}
			case 2:
			{
				OLED_Clear();
				OLED_ShowString(0,0, "←",OLED_8X16);
				OLED_ShowString(0,16,"指纹查看",OLED_8X16);
				OLED_ShowString(0,32,"指纹录入",OLED_8X16);
				OLED_ShowString(0,48,"指纹删除",OLED_8X16);
				OLED_ReverseArea(0,16,128,16);
				OLED_Update();
				break;
			}
			case 3:
			{
				OLED_Clear();
				OLED_ShowString(0,0, "←",OLED_8X16);
				OLED_ShowString(0,16,"指纹查看",OLED_8X16);
				OLED_ShowString(0,32,"指纹录入",OLED_8X16);
				OLED_ShowString(0,48,"指纹删除",OLED_8X16);
				OLED_ReverseArea(0,32,128,16);
				OLED_Update();
				break;
			}
			case 4:
			{
				OLED_Clear();
				OLED_ShowString(0,0, "←",OLED_8X16);
				OLED_ShowString(0,16,"指纹查看",OLED_8X16);
				OLED_ShowString(0,32,"指纹录入",OLED_8X16);
				OLED_ShowString(0,48,"指纹删除",OLED_8X16);
				OLED_ReverseArea(0,48,128,16);
				OLED_Update();
				break;
			}
		}
	}
	return 0;
}

/**
  * @brief 时钟待机界面 - 内部绘制函数（每次调用都整屏重画）
  * @param  无
  * @retval 无
  * @note   读取 RTC 时间，在屏上画两行：Date(日期) + Time(时间)。
  *         是 IdleClock_Draw 和 IdleClock_Tick 共用的底层绘制。
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
  * @brief 时钟待机界面 - 完整绘制（上电或从菜单返回待机时调用一次）
  * @param  无
  * @retval 无
  * @note   和 IdleClock_Show 的区别：Draw 是"对外完整入口"，main 和菜单
  *         返回时调它；Show 是内部实现。封装一层是为了语义清晰——
  *         上层只关心"我要重画待机界面"，不关心内部怎么画。
  */
void IdleClock_Draw(void)
{
	IdleClock_Show();
}

/**
  * @brief 时钟待机界面 - 非阻塞刷新（main 主循环每圈调用）
  * @param  无
  * @retval 无
  * @note   关键优化：只在"秒数变化"时才重画，避免 I2C 满速刷屏浪费 CPU。
  *         用静态变量 last_sec 记住上次秒数，初值取 0xFFFF 保证首次必刷新。
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
