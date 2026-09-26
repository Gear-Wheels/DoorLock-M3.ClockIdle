#ifndef __CODEDLOCK_H
#define __CODEDLOCK_H

int CodedLock(void);          // 密码解锁：输入6位密码→比对，1=成功，0=返回
int ChangePassword(void);     // 密码修改：验证旧→输新→确认→写入，1=成功，0=返回/失败

#endif
