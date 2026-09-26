#ifndef __Key_H__
#define __Key_H__

void    Key_Init(void);      // 板载三键初始化（PB11上/PB1下/PA6确认）
uint8_t Key_GetNum(void);    // 读板载键码：1上/2下/3确认，无按键返回0（阻塞+消抖）
uint8_t Event_GetNum(void);  // 事件聚合：把板载键/键盘/指纹统一成事件码(1/2/3/10/11)

#endif
