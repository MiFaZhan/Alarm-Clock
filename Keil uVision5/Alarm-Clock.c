#include <reg51.h>
#include <intrins.h>

#define uchar unsigned char
#define uint unsigned int

// === 控件定义 ===
sbit K1 = P1^0;
sbit K2 = P1^1;
sbit K3 = P1^2;
sbit K4 = P1^3;

sbit LED   = P2^4;
sbit RELAY = P2^3;
sbit BEEP  = P2^5;

sbit LCD_RS = P2^0;
sbit LCD_RW = P2^1;
sbit LCD_EN = P2^2;
#define LCD_DATA_PORT P3

typedef struct {
    uchar hour;
    uchar min;
    uchar sec;
} Time;

Time sysTime = {23, 58, 0};
Time alarmTime = {0, 0, 0};

bit beepEnable = 1;
bit alarmTrig = 0;
bit alarmActive = 0;
uchar mode = 0;

uchar alarm_view_timer = 0;
uint ms_counter = 0;
bit flash_flag = 0;
uint flash_cnt = 0;
uint alarm_duration = 0;  // 响铃持续秒数

// === 延时函数 ===
void delay_ms(uint ms) {
    uint i, j;
    for(i = 0; i < ms; i++)
        for(j = 0; j < 120; j++);
}

// === LCD 控制 ===
void lcd_write_cmd(uchar cmd) {
    LCD_RS = 0; LCD_RW = 0;
    LCD_DATA_PORT = cmd;
    LCD_EN = 1; delay_ms(1);
    LCD_EN = 0; delay_ms(1);
}

void lcd_write_data(uchar dat) {
    LCD_RS = 1; LCD_RW = 0;
    LCD_DATA_PORT = dat;
    LCD_EN = 1; delay_ms(1);
    LCD_EN = 0; delay_ms(1);
}

void lcd_init() {
    lcd_write_cmd(0x38);
    lcd_write_cmd(0x0C);
    lcd_write_cmd(0x06);
    lcd_write_cmd(0x01);
    delay_ms(2);
}

void lcd_set_cursor(uchar row, uchar col) {
    lcd_write_cmd((row == 0 ? 0x80 : 0xC0) + col);
}

void lcd_show_str(uchar row, uchar col, uchar *str) {
    lcd_set_cursor(row, col);
    while(*str) lcd_write_data(*str++);
}

void lcd_show_time(Time t, uchar col, bit blink) {
    lcd_set_cursor(1, col);
    if(blink && flash_flag) {
        lcd_show_str(1, col, "     ");
    } else {
        lcd_write_data(t.hour / 10 + '0');
        lcd_write_data(t.hour % 10 + '0');
        lcd_write_data(':');
        lcd_write_data(t.min / 10 + '0');
        lcd_write_data(t.min % 10 + '0');
    }
}

// === 响铃控制：每秒3声
void alarm_beep_ctrl() {
    uchar i;
    if(alarmTrig && alarmActive) {
        if(beepEnable) {
            for(i = 0; i < 3; i++) {
                BEEP = 0; delay_ms(100);
                BEEP = 1; delay_ms(100);
            }
        } else {
            BEEP = 0; delay_ms(300);
            BEEP = 1;
        }
        alarmTrig = 0;  // 一秒清除，由定时器每秒重新置位
    } else {
        BEEP = 1;
    }
}

// === 时间更新 ===
void update_time() {
    sysTime.sec++;
    if(sysTime.sec >= 60) {
        sysTime.sec = 0;
        sysTime.min++;
        if(sysTime.min >= 60) {
            sysTime.min = 0;
            sysTime.hour++;
            if(sysTime.hour >= 24) sysTime.hour = 0;
        }
    }

    if(sysTime.hour == alarmTime.hour && sysTime.min == alarmTime.min && sysTime.sec == 0) {
        alarmTrig = 1;
        alarmActive = 1;
        alarm_duration = 60;  // 响铃 60 秒
        RELAY = 0;
    }
}

// === 按键扫描 ===
void key_scan() {
    uchar i;

    if(K1 == 0) {
        delay_ms(10); while(K1 == 0);
        if(mode == 0) mode = 2;
        else if(mode == 2) sysTime.hour = (sysTime.hour + 1) % 24;
        else if(mode == 3) alarmTime.hour = (alarmTime.hour + 1) % 24;
    }

    if(K2 == 0) {
        delay_ms(10); while(K2 == 0);
        if(mode == 0) {
            mode = 1;
            alarm_view_timer = 3;
        }
        else if(mode == 2) sysTime.min = (sysTime.min + 1) % 60;
        else if(mode == 3) alarmTime.min = (alarmTime.min + 1) % 60;
    }

    if(K3 == 0) {
        delay_ms(10); while(K3 == 0);
        if(mode == 1) mode = 0;
        else if(mode == 0) mode = 3;
        else mode = 0;
    }

    if(K4 == 0) {
        delay_ms(10); while(K4 == 0);
        if(alarmActive) {
            // 闹钟期间关闭
            alarmActive = 0;
            alarmTrig = 0;
            alarm_duration = 0;
            RELAY = 1;
            BEEP = 1;
        } else {
            // 平时切换蜂鸣器开关
            beepEnable = !beepEnable;
            for(i = 0; i < 3; i++) {
                BEEP = 0; delay_ms(100);
                BEEP = 1; delay_ms(100);
            }
        }
    }
}

// === 定时器0 初始化 ===
void timer0_init() {
    TMOD = 0x01;
    TH0 = 0xFC; TL0 = 0x67;  // 1ms
    ET0 = 1; EA = 1; TR0 = 1;
}

void timer0_isr(void) interrupt 1 {
    TH0 = 0xFC; TL0 = 0x67;
    ms_counter++;

    if(ms_counter % 20 == 0) LED = ~LED;

    flash_cnt++;
    if(flash_cnt >= 500) {
        flash_cnt = 0;
        flash_flag = !flash_flag;
    }

    if(ms_counter >= 1000) {
        ms_counter = 0;
        update_time();

        // 每秒执行一次响铃控制
        if(alarmActive) {
            alarmTrig = 1;  // 每秒置位，由 alarm_beep_ctrl 使用并清除
            alarm_beep_ctrl();
            if(alarm_duration > 0) {
                alarm_duration--;
            } else {
                alarmActive = 0;
                alarmTrig = 0;
                RELAY = 1;
                BEEP = 1;
            }
        }

        if(alarm_view_timer > 0) {
            alarm_view_timer--;
            if(alarm_view_timer == 0 && mode == 1) mode = 0;
        }
    }
}

// === 显示刷新 ===
void display_update() {
    lcd_show_str(0, 2, "H.I.T. CHINA");
    if(mode == 0 || mode == 2) {
        lcd_show_str(1, 2, "Time  ");
        lcd_show_time(sysTime, 8, (mode == 2));
    } else if(mode == 1 || mode == 3) {
        lcd_show_str(1, 2, "Alarm ");
        lcd_show_time(alarmTime, 8, (mode == 3));
    }
}

// === 主函数 ===
void main() {
    lcd_init();
    timer0_init();

    RELAY = 1;
    BEEP = 1;
    LED = 0;

    delay_ms(500);

    while(1) {
        key_scan();
        display_update();
    }
}
