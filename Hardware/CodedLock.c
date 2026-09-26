#include "stm32f10x.h"                  // Device header
#include "MKey.h"
#include "Key.h"
#include "OLED.h"
#include "Delay.h"

#define PASSWORD_LEN   6                 // 密码位数

// 当前密码（6 位，字符形式；与 MKey_Read 返回的字符直接比较）
// 非 const：可在"密码修改"功能中被 ChangePassword() 更新
static char current_password[PASSWORD_LEN] = {'9', '5', '2', '7', '0', '0'};

// 界面坐标（与 menu2_password 原界面一致）
#define TITLE_X      24                  // 标题"请输入密码"起始 x
#define TITLE_Y      16
#define INPUT_X      20                  // 下划线占位起始 x
#define INPUT_Y      33
#define TIP_Y        48                  // 提示行

// 6 个下划线占位符的 x 坐标（8x16 字体，"_ " 交替，每个字符 8px，间隔 16px）
static const int16_t DashX[PASSWORD_LEN] = {20, 36, 52, 68, 84, 100};

/**
  * @brief 密码解锁界面：输入 6 位密码 → 键3确认比对 → 成功/失败反馈 → 键2返回
  * @param  无
  * @retval 1 = 解锁成功；0 = 用户按返回键退出（未解锁）
  * @note   数字键来自矩阵键盘 MKey_Read；* 键退格；确认/返回来自板载三键 Key_GetNum
  */
int CodedLock(void)
{
    char    input[PASSWORD_LEN];         // 用户输入缓冲
    uint8_t idx = 0;                      // 当前已输入位数
    char    key;                          // 本次按键（矩阵键盘字符）
    uint8_t k;                            // 板载键码
    uint8_t i;                            // 比对循环变量

    /* 进入界面：清屏 + 标题 + 下划线占位 */
    OLED_Clear();
    OLED_ShowString(TITLE_X, TITLE_Y, "请输入密码", OLED_8X16);
    OLED_ShowString(INPUT_X, INPUT_Y, "_ _ _ _ _ _", OLED_8X16);
    OLED_Update();

    while (1)
    {
        /* 1. 先扫矩阵键盘 */
        key = MKey_Read();

        /* 2. 矩阵没按键，再扫板载三键 */
        if (key == 0)
        {
            k = Key_GetNum();
        }

        /* 3. 数字键：存入缓冲并回显 */
        if (key >= '0' && key <= '9')
        {
            if (idx < PASSWORD_LEN)
            {
                input[idx] = key;
                idx++;

                /* 回显：对应下划线位置画 * */
                OLED_ShowChar(DashX[idx - 1], INPUT_Y, '*', OLED_8X16);
                OLED_Update();
            }
        }
        /* 4. * 键 = 退出/返回 */
        else if (key == '*')
        {
            OLED_Clear();
            OLED_Update();
            return 0;
        }
        /* 4.5 # 键 = 退格，删除最后一位 */
        else if (key == '#')
        {
            if (idx > 0)
            {
                idx--;
                /* 回显：对应位置恢复下划线 */
                OLED_ShowChar(DashX[idx], INPUT_Y, '_', OLED_8X16);
                OLED_Update();
            }
        }
        /* 5. 板载键3 = 确认比对 */
        else if (k == 3)
        {
            /* 位数不足，提示后清空重输 */
            if (idx < PASSWORD_LEN)
            {
                OLED_Clear();
                OLED_ShowString(32, 24, "位数不足", OLED_8X16);
                OLED_Update();
                Delay_ms(1000);
                idx = 0;                    // 状态归零，和显示一起从头重输
                /* 回到输入界面 */
                OLED_Clear();
                OLED_ShowString(TITLE_X, TITLE_Y, "请输入密码", OLED_8X16);
                OLED_ShowString(INPUT_X, INPUT_Y, "_ _ _ _ _ _", OLED_8X16);
                OLED_Update();
                continue;
            }

            /* 位数够了，逐位比对 */
            for (i = 0; i < PASSWORD_LEN; i++)
            {
                if (input[i] != current_password[i])
                {
                    break;
                }
            }

            if (i == PASSWORD_LEN)          // 全部相等 = 成功
            {
                /* 解锁成功：整屏正中间显示 */
                OLED_Clear();
                OLED_ShowString(32, 24, "解锁成功", OLED_8X16);
                OLED_Update();
                Delay_ms(1500);
                /* TODO: 这里以后接开锁动作（LED/蜂鸣器/电机） */
                return 1;
            }
            else                            // 有不等 = 失败，清空重输
            {
                OLED_Clear();
                OLED_ShowString(32, 24, "密码错误", OLED_8X16);
                OLED_Update();
                Delay_ms(1500);
                idx = 0;                    // 状态复位，重新输入
                /* 回到输入界面 */
                OLED_Clear();
                OLED_ShowString(TITLE_X, TITLE_Y, "请输入密码", OLED_8X16);
                OLED_ShowString(INPUT_X, INPUT_Y, "_ _ _ _ _ _", OLED_8X16);
                OLED_Update();
            }
        }
    }
}

/**
  * @brief 内部辅助：输入 6 位数字密码，回显 * 号
  * @param  title   标题字符串（如"验证密码"/"输入新密码"/"确认密码"）
  * @param  buf     输出缓冲，长度至少 PASSWORD_LEN，成功时存入 6 位密码
  * @retval 1 = 输入完整并返回；0 = 用户按返回键退出
  * @note   复用 CodedLock 的输入逻辑；* 键退格，板载键3 确认，键2 返回
  */
static int input_password(const char *title, char *buf)
{
    uint8_t idx = 0;
    char    key;
    uint8_t k;

    OLED_Clear();
    OLED_ShowString(TITLE_X, TITLE_Y, (char *)title, OLED_8X16);
    OLED_ShowString(INPUT_X, INPUT_Y, "_ _ _ _ _ _", OLED_8X16);
    OLED_Update();

    while (1)
    {
        key = MKey_Read();
        if (key == 0)
        {
            k = Key_GetNum();
        }

        if (key >= '0' && key <= '9')
        {
            if (idx < PASSWORD_LEN)
            {
                buf[idx] = key;
                idx++;
                OLED_ShowChar(DashX[idx - 1], INPUT_Y, '*', OLED_8X16);
                OLED_Update();
            }
        }
        else if (key == '*')        // * 键 = 退出/返回
        {
            return 0;
        }
        else if (key == '#')        // # 键 = 退格
        {
            if (idx > 0)
            {
                idx--;
                OLED_ShowChar(DashX[idx], INPUT_Y, '_', OLED_8X16);
                OLED_Update();
            }
        }
        else if (k == 3)            // 板载键3 = 确认提交
        {
            if (idx < PASSWORD_LEN)
            {
                OLED_Clear();
                OLED_ShowString(32, 24, "位数不足", OLED_8X16);
                OLED_Update();
                Delay_ms(1000);
                OLED_Clear();
                OLED_ShowString(TITLE_X, TITLE_Y, (char *)title, OLED_8X16);
                OLED_ShowString(INPUT_X, INPUT_Y, "_ _ _ _ _ _", OLED_8X16);
                OLED_Update();
                continue;
            }
            return 1;               // 输满 6 位，返回
        }
    }
}

/**
  * @brief 密码修改功能：验证旧密码 → 输入新密码 → 确认新密码 → 修改成功
  * @param  无
  * @retval 1 = 修改成功；0 = 中途返回/失败
  * @note   接入"功能设置 -> 密码修改"；成功后 current_password 被更新
  */
int ChangePassword(void)
{
    char    old_pwd[PASSWORD_LEN];
    char    new_pwd[PASSWORD_LEN];
    char    confirm[PASSWORD_LEN];
    uint8_t i;

    /* 第一步：验证旧密码（失败循环重试，* 键退出） */
    while (1)
    {
        if (!input_password("验证密码", old_pwd))
        {
            return 0;               // * 键主动退出
        }
        for (i = 0; i < PASSWORD_LEN; i++)
        {
            if (old_pwd[i] != current_password[i])
                break;
        }
        if (i == PASSWORD_LEN)      // 验证通过，退出重试循环
        {
            break;
        }

        /* 验证失败：提示后回到验证界面重输（不 return，留在本功能内） */
        OLED_Clear();
        OLED_ShowString(32, 24, "密码错误", OLED_8X16);
        OLED_Update();
        Delay_ms(1500);
    }

    /* 第二步：输入新密码 */
    if (!input_password("输入新密码", new_pwd))
    {
        return 0;
    }

    /* 第三步：确认新密码 */
    if (!input_password("确认密码", confirm))
    {
        return 0;
    }

    /* 比对两次新密码是否一致 */
    for (i = 0; i < PASSWORD_LEN; i++)
    {
        if (new_pwd[i] != confirm[i])
            break;
    }
    if (i != PASSWORD_LEN)          // 两次不一致
    {
        OLED_Clear();
        OLED_ShowString(32, 24, "两次不一致", OLED_8X16);
        OLED_Update();
        Delay_ms(1500);
        return 0;
    }

    /* 写入新密码 */
    for (i = 0; i < PASSWORD_LEN; i++)
    {
        current_password[i] = new_pwd[i];
    }

    /* 修改成功 */
    OLED_Clear();
    OLED_ShowString(32, 24, "修改成功", OLED_8X16);
    OLED_Update();
    Delay_ms(1500);
    return 1;
}
