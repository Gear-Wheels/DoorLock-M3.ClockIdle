#include "stm32f10x.h"                  // Device header
#include "Delay.h"
#include "MKey.h"
#include "as608.h"                      // 取 PS_Sta_PORT/PS_Sta_PIN（指纹触摸脚 PA4）

/*==============================================================================
 * Key.c —— 板载三键驱动 + 事件抽象层（门锁的"输入总管"）
 *
 * 【这个文件的两大职责】
 *   1. 板载三键：读上键/下键/确认键三个物理按键（Key_GetNum）。
 *   2. 事件抽象层：把"板载键 + 键盘 + 指纹"三种输入，统一成一个"事件码"
 *      （Event_GetNum），供 main 主循环使用。
 *
 * 【什么是"事件抽象层"（本项目最重要的设计之一）】
 *   想象门锁要响应很多种输入：按板载键、按键盘、摸指纹……
 *   如果 main 主循环里直接写"读这个 GPIO、再读那个 GPIO"，代码会很乱，
 *   而且以后加新输入方式要到处改。
 *
 *   所以这里做了一层"翻译"：不管底层是什么输入，都翻译成统一的事件码：
 *      1 = 上键   2 = 下键   3 = 确认键
 *      10 = 键盘有键按下   11 = 指纹检测到手指
 *   main 只认这些数字，不关心数字背后是哪个硬件。
 *   好处：以后加手机开锁、RFID，只需在 Event_GetNum 里加几行返回新事件码，
 *         main 和所有菜单代码一行都不用改。
 *============================================================================*/

/**
  * @brief 板载三键 GPIO 初始化
  * @param  无
  * @retval 无
  * @note   键1=PB11(上)、键2=PB1(下)、键3=PA6(确认)，全部配成上拉输入。
  *         按下时引脚被拉低（读到 0），松开恢复高（读到 1）。
  */
void Key_Init(void)
{
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB,ENABLE);//打开GPIOB时钟
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA,ENABLE);//打开GPIOA时钟
	
	GPIO_InitTypeDef GPIO_InitStructure;				//定义结构体变量
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;		//上拉输入
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1 | GPIO_Pin_11;//用B1口和B11口
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;//输出速度,但输入用不上,写50没影响
	GPIO_Init(GPIOB,&GPIO_InitStructure);				//GPIOB初始化
	
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;		//上拉输入
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6;//用B1口和B11口
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;//输出速度,但输入用不上,写50没影响
	GPIO_Init(GPIOA,&GPIO_InitStructure);				//GPIOB初始化
}

/**
  * @brief 获取板载按键值（阻塞式，含消抖）
  * @param  无
  * @retval 键码：1=上(PB11) 2=下(PB1) 3=确认(PA6)；无按键返回 0
  * @note   消抖：读到按下后延时 20ms 再确认，然后等松手（while 死等），
  *         松手再延时 20ms，保证一次按键只返回一次、不抖动。
  */
uint8_t Key_GetNum(void)
{
	uint8_t KeyNum = 0;							//默认为0,没有按键按下返回0
	if(GPIO_ReadInputDataBit(GPIOB,GPIO_Pin_11) == 0)//读取B11口的输入,如果按下(0)
	{
		Delay_ms(20);							//按键消抖
		while(GPIO_ReadInputDataBit(GPIOB,GPIO_Pin_11) == 0);//直到松手往下走
		Delay_ms(20);							//按键消抖
		KeyNum = 1;								//返回键码1
	}
	
	if(GPIO_ReadInputDataBit(GPIOB,GPIO_Pin_1) == 0)//读取B1口的输入,如果按下(0)
	{
		Delay_ms(20);							//按键消抖
		while(GPIO_ReadInputDataBit(GPIOB,GPIO_Pin_1) == 0);//直到松手往下走
		Delay_ms(20);							//按键消抖
		KeyNum = 2;								//返回键码2
	}

	if(GPIO_ReadInputDataBit(GPIOA,GPIO_Pin_6) == 0)//读取A6口的输入,如果按下(0)
	{
		Delay_ms(20);							//按键消抖
		while(GPIO_ReadInputDataBit(GPIOA,GPIO_Pin_6) == 0);//直到松手往下走
		Delay_ms(20);							//按键消抖
		KeyNum = 3;								//返回键码3
	}


	return KeyNum;
}

/**
  * @brief 事件聚合接口（待机主循环统一调用）
  * @param  无
  * @retval 事件码：0=无事件 1=上 2=下 3=确认
  *         10=密码键盘有键按下   11=指纹模块检测到手指
  * @note   依次检查三种输入源，谁先有事件就返回谁的事件码。
  *         这就是"事件抽象层"的核心：把硬件细节翻译成统一事件码。
  *         以后键盘/指纹硬件接好，只需在本函数里填检测代码并返回对应事件码，
  *         main 和所有菜单函数都不用改动。
  */
uint8_t Event_GetNum(void)
{
	uint8_t k = Key_GetNum();
	if(k)					//板载三个按键,直接透传键码
	{
		return k;
	}

	/* 密码键盘扫描:检测到有键按下时返回事件码 10 */
	if(MKey_Read() != 0)
	{
		return 10;
	}

	/* 指纹模块手指检测:触摸脚 PA4 读到高电平 = 检测到手指，返回事件码 11 */
	if(GPIO_ReadInputDataBit(PS_Sta_PORT, PS_Sta_PIN) == 1)
	{
		return 11;
	}

	return 0;
}
