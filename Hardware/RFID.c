#include "RFID.h"
#include "MFRC522.h"
#include "beep.h"
#include "stdio.h"
#include "uart.h"
#include "OLED.h"
#include "key.h"
#include "Motor.h"
#include "led.h"
// 定义授权卡片列表（示例）
#define MAX_AUTH_CARDS 10
u8 authorized_cards[MAX_AUTH_CARDS][5] = {0}; // 存储卡片序列号
u8 auth_card_count = 0; // 已授权卡片数量

// 检查卡片是否已授权
u8 IsCardAuthorized(u8 *card_number)
{
    u8 i, j;
    for(i = 0; i < auth_card_count; i++)
    {
        u8 match = 1;
        for(j = 0; j < 4; j++) // 比较前4个字节（序列号）
        {
            if(authorized_cards[i][j] != card_number[j])
            {
                match = 0;
                break;
            }
        }
        if(match)
            return 1; // 卡片已授权
    }
    return 0; // 卡片未授权
}

// 添加卡片到授权列表
u8 AddCardToAuthList(u8 *card_number)
{
    if(auth_card_count >= MAX_AUTH_CARDS)
    {
        Send_String(USART1, "授权列表已满!\r\n");
        return 0;
    }
    
    if(IsCardAuthorized(card_number))
    {
        Send_String(USART1, "卡片已存在授权列表中!\r\n");
        return 0;
    }
    
    // 复制卡片序列号到授权列表
    u8 i;
    for(i = 0; i < 5; i++)
    {
        authorized_cards[auth_card_count][i] = card_number[i];
    }
    auth_card_count++;
    
    Send_String(USART1, "卡片录入成功!\r\n");
    return 1;
}

// 从授权列表删除卡片
u8 RemoveCardFromAuthList(u8 *card_number)
{
    u8 i, j, k;
    for(i = 0; i < auth_card_count; i++)
    {
        u8 match = 1;
        for(j = 0; j < 4; j++) // 比较前4个字节（序列号）
        {
            if(authorized_cards[i][j] != card_number[j])
            {
                match = 0;
                break;
            }
        }
        
        if(match)
        {
            // 删除卡片，将后面的卡片前移
            for(k = i; k < auth_card_count - 1; k++)
            {
                for(j = 0; j < 5; j++)
                {
                    authorized_cards[k][j] = authorized_cards[k+1][j];
                }
            }
            auth_card_count--;
            
            // 清空最后一个位置
            for(j = 0; j < 5; j++)
            {
                authorized_cards[auth_card_count][j] = 0;
            }
            
            Send_String(USART1, "卡片删除成功!\r\n");
            return 1;
        }
    }
    
    Send_String(USART1, "未找到该卡片!\r\n");
    return 0;
}

// 显示授权列表
#if 0
void ShowAuthList(void)
{
    char buffer[64];
    u8 i, j;
    	
    sprintf(buffer, "已授权卡片数量: %d\r\n", auth_card_count);
    Send_String(USART1, buffer);
    
    for(i = 0; i < auth_card_count; i++)
    {
        Send_String(USART1, "卡片");
        sprintf(buffer, "%d: ", i+1);
        Send_String(USART1, buffer);
        
        for(j = 0; j < 5; j++)
        {
            sprintf(buffer, "%02X ", authorized_cards[i][j]);
            Send_String(USART1, buffer);
        }
        Send_String(USART1, "\r\n");
    }
}
#endif
void ShowAuthList(void)
{
    char buffer[64];
    u8 i, j;
    
    OLED_Clear();  
    // 将数字转换为字符串显示
    sprintf(buffer, "授权数量: %d", auth_card_count);
    OLED_ShowString(24, 24, buffer, OLED_8X16);
    OLED_Update();
    delay_ms(2000); 
    
    // 清屏后显示详细信息
    OLED_Clear();
    
    sprintf(buffer, "已授权卡片数量: %d\r\n", auth_card_count);
    Send_String(USART1, buffer);
    
    // 在OLED上显示每张卡片信息
    if(auth_card_count == 0)
    {
		OLED_Clear();  
        OLED_ShowString(24, 24, "无授权卡片", OLED_8X16);
		OLED_Update();
    }
    else
    {
        for(i = 0; i < auth_card_count; i++)
        {
            // 在OLED上显示卡片序号
            sprintf(buffer, "卡片%d:", i+1);
            OLED_ShowString(0, i*16, buffer, OLED_8X16);
            
            // 在OLED上显示卡片序列号（前4个字节）
            sprintf(buffer, "%02X%02X%02X%02X", 
                   authorized_cards[i][0], 
                   authorized_cards[i][1], 
                   authorized_cards[i][2], 
                   authorized_cards[i][3]);
            OLED_ShowString(64, i*16, buffer, OLED_8X16);
            
            // 串口输出完整信息
            Send_String(USART1, "卡片");
            sprintf(buffer, "%d: ", i+1);
            Send_String(USART1, buffer);
            
            for(j = 0; j < 5; j++)
            {
                sprintf(buffer, "%02X ", authorized_cards[i][j]);
                Send_String(USART1, buffer);
            }
            Send_String(USART1, "\r\n");
        }
    }
    
    OLED_Update();
    delay_ms(3000); // 给用户足够时间查看
}

// 卡片录入函数
int CardEnrollment(void)
{
    u8 card_pydebuf[2];
    u8 card_numberbuf[5];
    u8 status;
	
    char buffer[64];
    uint8_t key_num;
	key_num = Key_Scan();
	if(key_num == 4){return 0;}//按下按键4返回0值	
	
	OLED_Clear();
	OLED_ShowString(24,24, "请放置卡片",OLED_8X16);
	OLED_Update();
	
    Send_String(USART1, "请放置要录入的卡片...\r\n");
    
    // 等待卡片
    do {
        status = MFRC522_Request(0x52, card_pydebuf);
        delay_ms(100);
    } while(status != MI_OK);
    
    // 获取卡片序列号
    status = MFRC522_Anticoll(card_numberbuf);
    if(status == MI_OK)
    {
        Send_String(USART1, "检测到卡片: ");
        u8 i;
        for(i = 0; i < 5; i++)
        {
            sprintf(buffer, "%02X ", card_numberbuf[i]);
            Send_String(USART1, buffer);
        }
        Send_String(USART1, "\r\n");
        
        // 添加到授权列表
        if(AddCardToAuthList(card_numberbuf))
        {
			OLED_Clear();
			OLED_ShowString(32,24, "录入成功",OLED_8X16);
			OLED_Update();
			Beep_once(100); // 成功提示音
			// 延时2秒
			delay_ms(2000); 
			return 0;
        }
        else
        {
			OLED_Clear();
			OLED_ShowString(32,24, "录入失败",OLED_8X16);
			OLED_Update();
			Beep_once(800); // 失败提示音
			// 延时2秒
			delay_ms(2000);
			return 1;
        }
        
        MFRC522_Halt();
    }
    else
    {
        Send_String(USART1, "读取卡片失败!\r\n");
    }
}

// 卡片删除函数
void CardDeletion(void)
{

    u8 card_pydebuf[2];
    u8 card_numberbuf[5];
    u8 status;
    char buffer[64];
 
	OLED_Clear();
	OLED_ShowString(24,24, "请放置卡片",OLED_8X16);
	OLED_Update();
	
    Send_String(USART1, "请放置要删除的卡片...\r\n");
    
    // 等待卡片
    do {
        status = MFRC522_Request(0x52, card_pydebuf);
        delay_ms(100);
    } while(status != MI_OK);
    
    // 获取卡片序列号
    status = MFRC522_Anticoll(card_numberbuf);
    if(status == MI_OK)
    {
        Send_String(USART1, "检测到卡片: ");
        u8 i;
        for(i = 0; i < 5; i++)
        {
            sprintf(buffer, "%02X ", card_numberbuf[i]);
            Send_String(USART1, buffer);
        }
        Send_String(USART1, "\r\n");
        
        // 从授权列表删除
        if(RemoveCardFromAuthList(card_numberbuf))
        {
			OLED_Clear();
			OLED_ShowString(32,24, "删除成功",OLED_8X16);
			OLED_Update();
			Beep_once(100); // 成功提示音
			// 延时2秒
			delay_ms(2000); 
        }
        else
        {
			OLED_Clear();
			OLED_ShowString(32,24, "删除失败",OLED_8X16);
			OLED_Update();
			Beep_once(100); // 成功提示音
			// 延时2秒
			delay_ms(2000);
        }
        
        MFRC522_Halt();
    }
    else
    {
        Send_String(USART1, "读取卡片失败!\r\n");
    }
}

// 卡片验证函数
int RFIDverify(void)
{
    uint8_t key_num;
    key_num = Key_Scan();
    if(key_num == 4){return 0;}//按下按键4返回0值	
    
    u8  card_pydebuf[2];
    u8  card_numberbuf[5];
    u8  status;
    char buffer[64];
    
    OLED_Clear();
    OLED_ShowString(24,24, "请验证身份",OLED_8X16);
    OLED_Update();
    
    status = MFRC522_Request(0x52, card_pydebuf); // 寻卡
    
    if(status == MI_OK) // 如果读到卡
    {
        status = MFRC522_Anticoll(card_numberbuf); // 防冲撞处理
            
        if(status == MI_OK)
        {
            // 检查卡片是否授权
            if(IsCardAuthorized(card_numberbuf))
            {
                OLED_Clear();
                OLED_ShowString(32,24, "验证通过",OLED_8X16);
                OLED_Update();
				//开门指示灯亮起
				lock_open();				
                Beep_once(50); // 蜂鸣器响一声 - 授权通过                
                // 显示卡片信息
                Send_String(USART1, "授权卡片 - 允许访问\r\n");
                Send_String(USART1, "卡片序列号: ");
                u8 i;
                for(i = 0; i < 5; i++)
                {
                    sprintf(buffer, "%02X ", card_numberbuf[i]);
                    Send_String(USART1, buffer);
                }
                Send_String(USART1, "\r\n");
                // 控制电机正转开锁
				Walkmotor_ON();
                MFRC522_Halt(); // 使卡进入休眠状态
                //开门操作结束关闭指示灯（关门）
				lock_close();
                return 0;
            }
            else
            {
                OLED_Clear();
                OLED_ShowString(32,24, "身份非法",OLED_8X16);
                OLED_Update();
				lock_error();// LED错误提示
                Beep_once(1000); // 警报长鸣 - 未授权
                
                Send_String(USART1, "未授权卡片 - 拒绝访问\r\n");
                Send_String(USART1, "检测到未授权卡片: ");
                u8 i;
                for(i = 0; i < 5; i++)
                {
                    sprintf(buffer, "%02X ", card_numberbuf[i]);
                    Send_String(USART1, buffer);
                }
                Send_String(USART1, "\r\n");
                
                MFRC522_Halt(); // 使卡进入休眠状态
                delay_ms(2000);
                return 2;
            }
        }
    }
    
    // 如果没有读到卡或防冲撞失败
    return 1;
}