#ifndef _MKEY_H_
#define _MKEY_H_

void MKey_Init(void);    // 薄膜键盘初始化（3列输出 PA8-10，4行输入 PB12-15）
char MKey_Read(void);    // 扫描一次键盘，返回按下的字符('0'-'9'/'*'/'#')，无按键返回0

#endif
