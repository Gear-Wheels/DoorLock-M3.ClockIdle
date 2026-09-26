#ifndef __MENU_H__
#define __MENU_H__

/*==============================================================================
 * menu.h —— 菜单系统对外接口
 * 说明：把 menu.c 里给 main.c 调用的函数在这里声明，
 *       让 main.c 不用看 menu.c 的实现细节就能调用菜单。
 *============================================================================*/

int  menu1(void);                // 一级菜单，返回选中项(1~6)，0=超时
int  menu2_password(void);       // 二级：密码解锁
int  menu2_fingerprint(void);    // 二级：指纹解锁
int  menu2_Phone(void);          // 二级：手机解锁（预留）
int  menu2_RFID(void);           // 二级：RFID 解锁（预留）
int  menu2_config(void);         // 二级：功能设置
int  menu2_Clock(void);          // 二级：时钟显示
int  menu3_fpconfig(void);       // 三级：指纹设置（查看/录入/删除）
void IdleClock_Draw(void);       // 待机界面：完整重画（上电/回待机时调）
void IdleClock_Tick(void);       // 待机界面：非阻塞刷新（主循环每圈调）

#endif
