#ifndef _RFID_H_
#define _RFID_H_

#include "stm32f4xx.h"

// 函数声明
int RFIDverify(void);						//卡片验证
int CardEnrollment(void);					//卡片录入
void CardDeletion(void);					//卡片删除
void ShowAuthList(void);					//显示当前所有授权卡片
u8 IsCardAuthorized(u8 *card_number);		//检查卡片是否授权
u8 AddCardToAuthList(u8 *card_number);
u8 RemoveCardFromAuthList(u8 *card_number);

#endif