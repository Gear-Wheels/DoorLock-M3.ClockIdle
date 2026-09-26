#include "stm32f10x.h"                  // Device header
#include "Delay.h" 

/*==============================================================================
 * MKey.c —— 薄膜键盘（矩阵键盘 3 列 × 4 行）驱动
 *
 * 【这是什么】
 *   一个 12 键的薄膜键盘，按键布局：
 *       1 2 3
 *       4 5 6
 *       7 8 9
 *       * 0 #
 *
 * 【为什么叫"矩阵键盘"】
 *   12 个按键如果每个都接一根线到单片机，需要 12 个引脚，太浪费。
 *   所以改成"矩阵"接法：3 条列线 + 4 条行线 = 7 个引脚就能管 12 个键。
 *   每个按键就跨接在某一行和某一列的交点上。
 *
 * 【扫描原理（核心）】
 *   列线是"输出"（单片机控制），行线是"输入"（单片机读）。
 *   平时所有列都输出高电平。
 *   扫描时：
 *     1) 把第 1 列拉低（输出 0），其余列保持高；
 *     2) 逐个读 4 条行线，如果某行读到 0，说明"这一列和这一行交叉的键被按下"
 *        （因为按键按下会把行列短接，列的低电平传到行上）；
 *     3) 扫完第 1 列，恢复拉高，再拉低第 2 列，重复……直到扫完 3 列。
 *   这样就能判断出"具体是哪个键被按下"了。
 *
 * 【消抖】
 *   按键按下的瞬间，金属触点会抖动几下，可能被误判成"按了很多次"。
 *   所以读到按下后，延时 10ms 再读一次确认，并且等松手才返回，这就是消抖。
 *============================================================================*/

// 用宏把引脚集中声明，以后换引脚只改这里
#define KEY_COL_PORT   GPIOA
#define KEY_COL1_PIN   GPIO_Pin_8
#define KEY_COL2_PIN   GPIO_Pin_9
#define KEY_COL3_PIN   GPIO_Pin_10

#define KEY_ROW_PORT   GPIOB
#define KEY_ROW1_PIN   GPIO_Pin_12
#define KEY_ROW2_PIN   GPIO_Pin_13
#define KEY_ROW3_PIN   GPIO_Pin_14
#define KEY_ROW4_PIN   GPIO_Pin_15

// 列数组和行数组，方便循环
static const uint16_t ColPins[3] = {KEY_COL1_PIN, KEY_COL2_PIN, KEY_COL3_PIN};
static const uint16_t RowPins[4] = {KEY_ROW1_PIN, KEY_ROW2_PIN, KEY_ROW3_PIN, KEY_ROW4_PIN};

// 薄膜键盘按键布局 + 引脚
/* 	
	1 2 3
	4 5 6
	7 8 9
	* 0 #
*/
// 列输出：PA8, PA9, PA10         
// 行输入：PB12, PB13, PB14, PB15  
// KeyMap[行][列] = 该位置按键对应的字符
static const char KeyMap[4][3] = {   
    {'1', '2', '3'},
    {'4', '5', '6'},
    {'7', '8', '9'},
    {'*', '0', '#'},
};

/**
  * @brief 薄膜键盘初始化
  * @param  无
  * @retval 无
  * @note   4 行配成上拉输入（默认高电平），3 列配成推挽输出并初始拉高。
  *         列初始拉高很关键：否则多列同时为低，扫描会错乱。
  */
void MKey_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;

	//打开GPIO端口的时钟
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA|RCC_APB2Periph_GPIOB, ENABLE);

	// 4行：上拉输入
	GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_IPU;     	// 设置为上拉输入内部经电阻接 VCC，默认高电平
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;   // 输出速度50MHz
	GPIO_InitStructure.GPIO_Pin   = KEY_ROW1_PIN | KEY_ROW2_PIN | KEY_ROW3_PIN | KEY_ROW4_PIN;    
	GPIO_Init(KEY_ROW_PORT, &GPIO_InitStructure);  			
	// 3列：推挽输出，初始全高
	GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_Pin   = KEY_COL1_PIN | KEY_COL2_PIN | KEY_COL3_PIN;
	GPIO_Init(KEY_COL_PORT, &GPIO_InitStructure); 
	// 显式把 3 列拉高（复位后输出寄存器默认是低，不拉高会导致多列同时为低、扫描错乱）
	GPIO_SetBits(KEY_COL_PORT, KEY_COL1_PIN | KEY_COL2_PIN | KEY_COL3_PIN);
}

/**
  * @brief 读取薄膜键盘按键值（扫描一次）
  * @param  无
  * @retval 按下键对应的字符（'0'~'9'、'*'、'#'）；没按键返回 0
  * @note   逐列扫描：拉低一列 → 读 4 行 → 哪行是 0 就是哪键按下。
  *         返回前会等按键松手（避免一次长按被重复读取）。
  */
char MKey_Read(void)
{
	uint8_t col, row;
	uint16_t col_pin, row_pin;
	
	for (col = 0; col < 3; col++)
	{
		col_pin = ColPins[col];
		
		// 1. 只把当前列拉低，其余列保持高
        GPIO_ResetBits(KEY_COL_PORT, col_pin);
		
		// 2. 逐行读
        for (row = 0; row < 4; row++)
		{
			row_pin = RowPins[row];
            if (GPIO_ReadInputDataBit(KEY_ROW_PORT, row_pin) == 0)  // 读到0=按下
            {
                Delay_ms(10);   // 消抖
                // 再次确认（消抖后还按着才认）
                if (GPIO_ReadInputDataBit(KEY_ROW_PORT, row_pin) == 0)
                {
                    // 3. 等松手（消抖的"释放"）
                    while (GPIO_ReadInputDataBit(KEY_ROW_PORT, row_pin) == 0);
                    // 4. 恢复当前列拉高
                    GPIO_SetBits(KEY_COL_PORT, col_pin);
					// 5. 查表返回字符
                    return KeyMap[row][col];
                }
            }
		}
		// 6. 这一列扫完，恢复拉高，准备扫下一列
        GPIO_SetBits(KEY_COL_PORT, col_pin);
	}
	return 0;   // 没按键，返回 0
}
