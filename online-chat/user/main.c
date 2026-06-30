#include "lvgl/lvgl.h"
#include "lvgl/demos/lv_demos.h"
#include "lv_drivers/display/fbdev.h"
#include "lv_drivers/indev/evdev.h"
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>
#include <stdlib.h>

#include "lvgl/examples/others/ime/lv_example_ime_pinyin.h"

#define DISP_BUF_SIZE (800 * 480)

int main(void)
{
    // LVGL的初始化,一定不能删除,做了核心功能的初始化
    lv_init();

    // 液晶屏的初始化-->open(打开液晶屏)-->ioctl(获取液晶屏参数)-->mmap(映射得到首地址)
    fbdev_init();

    // 定义数组存放要显示的内容
    static lv_color_t buf[DISP_BUF_SIZE];

    // 初始化数组
    static lv_disp_draw_buf_t disp_buf;
    lv_disp_draw_buf_init(&disp_buf, buf, NULL, DISP_BUF_SIZE);

    // 结构体变量,成员特别多
    static lv_disp_drv_t disp_drv;
    // 初始化刚才定义的结构体变量
    lv_disp_drv_init(&disp_drv);
    // 对结构体里面的成员赋值
    disp_drv.draw_buf = &disp_buf;   // 你刚才定义初始化的缓冲区
    disp_drv.flush_cb = fbdev_flush; // 函数指针,很重要,用来在液晶屏上画点/画矩形
    disp_drv.hor_res  = 800;         // 设置液晶屏屏幕的宽
    disp_drv.ver_res  = 480;         // 设置液晶屏屏幕的高
    lv_disp_drv_register(&disp_drv);

    // 触摸屏初始化
    evdev_init();
    // 定义结构体存放了触摸屏的信息
    static lv_indev_drv_t indev_drv_1;
    // 初始化结构体
    lv_indev_drv_init(&indev_drv_1); /*Basic initialization*/
    indev_drv_1.type = LV_INDEV_TYPE_POINTER;

    // 很重要: 触摸屏操作的核心函数
    indev_drv_1.read_cb = evdev_read; // 读取保存触摸屏坐标
    // lv_indev_t *mouse_indev =
    lv_indev_drv_register(&indev_drv_1);


  
    ui_login_create();//登录主界面
    // lv_example_ime_pinyin_1();
    

  

    // 防止主程序退出,lv_timer_handler();用来进行心跳检测
    while(1) {
        lv_timer_handler(); // 心跳函数,用来检测输入设备事件/刷新界面/...
        usleep(5000);
    }

    return 0;
}

/*Set in lv_conf.h as `LV_TICK_CUSTOM_SYS_TIME_EXPR`*/
uint32_t custom_tick_get(void)
{
    static uint64_t start_ms = 0;
    if(start_ms == 0) {
        struct timeval tv_start;
        gettimeofday(&tv_start, NULL);
        start_ms = (tv_start.tv_sec * 1000000 + tv_start.tv_usec) / 1000;
    }

    struct timeval tv_now;
    gettimeofday(&tv_now, NULL);
    uint64_t now_ms;
    now_ms = (tv_now.tv_sec * 1000000 + tv_now.tv_usec) / 1000;

    uint32_t time_ms = now_ms - start_ms;
    return time_ms;
}
