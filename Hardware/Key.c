#include "stm32f10x.h"                  // Device header
#include "Delay.h"

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
  * @brief 获取按键值
  * @param  无
  * @retval KeyNum,要读取的按键值
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
  * @brief 事件聚合接口,待机主循环统一调用
  * @param  无
  * @retval 事件码:0=无事件 1=上 2=下 3=确认
  *         10=密码键盘有键按下(预留,stub暂未实现)
  *         11=指纹模块检测到手指(预留,stub暂未实现)
  * @note   以后键盘/指纹硬件接好,只需在本函数里填检测代码并返回对应事件码,
  *         main 和所有菜单函数都不用改动
  */
uint8_t Event_GetNum(void)
{
	uint8_t k = Key_GetNum();
	if(k)					//板载三个按键,直接透传键码
	{
		return k;
	}

	/* stub:密码键盘扫描,检测到有键按下时 return 10 */

	/* stub:指纹模块手指检测,检测到有手指时 return 11 */

	return 0;
}
