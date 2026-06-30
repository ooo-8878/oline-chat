#include "ui_login.h"
#include "ui_register.h"
#include "ui_style.h"
#include "ui_chat.h"
#include "ui_forget.h"

#include <errno.h>

// 服务器配置
#define SERVER_IP "192.168.11.44"
#define SERVER_PORT 30000

// 背景图路径
#define THEME_IMG_LIGHT_PATH "S:/background/1.jpg"
#define THEME_IMG_DARK_PATH "S:/background/2.jpg"

// 字体路径
#define FONT_PATH "font/simkai.ttf"

// 全局控件
static lv_obj_t * login_scr    = NULL;
static lv_obj_t * ta_user      = NULL;
static lv_obj_t * ta_pass      = NULL;
static lv_obj_t * btn_login    = NULL;
static lv_obj_t * btn_register = NULL;
static lv_obj_t * btn_forget   = NULL;
static lv_obj_t * btn_theme    = NULL;
static lv_obj_t * kb           = NULL;
static lv_obj_t * pinyin_ime   = NULL;
static lv_obj_t * bg_img       = NULL;
static lv_obj_t * cand_panel   = NULL;

// 样式
static lv_style_t * style_title       = NULL;
static lv_style_t * style_text        = NULL;
static lv_style_t * style_btntext     = NULL;
static lv_style_t * style_placeholder = NULL;
static lv_style_t * style_msgbox_btn  = NULL;

static int theme_flag = 0;

// 隐藏键盘
static void hide_keyboard(void)
{
    if(kb) {
        lv_keyboard_set_textarea(kb, NULL); // 取消软键盘和文本框的绑定
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }

    if(pinyin_ime) {
        lv_obj_add_flag(pinyin_ime, LV_OBJ_FLAG_HIDDEN);
    }
    if(cand_panel) {
        lv_obj_add_flag(cand_panel, LV_OBJ_FLAG_HIDDEN);
    }

    if(ta_user) lv_obj_clear_state(ta_user, LV_STATE_FOCUSED); // 清除用户名输入框的焦点状态
    if(ta_pass) lv_obj_clear_state(ta_pass, LV_STATE_FOCUSED); // 清除密码输入框的焦点状态
}

// 提示框关闭事件
static void msgbox_event_cb(lv_event_t * e)
{
    lv_obj_t * obj = lv_event_get_current_target(e); // 获取当前触发事件的消息框对象
    lv_obj_del_async(obj);                           // 异步删除消息框
}

// 提示框
static void show_msgbox(const char * title, const char * text)
{
    static const char * btns[] = {"确定", ""}; // 消息框按钮数组，只有一个“确定”按钮
    lv_obj_t * parent = login_scr ? login_scr : lv_scr_act();
    lv_obj_t * mbox = lv_msgbox_create(parent, title, text, btns, false);
    lv_obj_center(mbox);
    lv_obj_add_event_cb(mbox, msgbox_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t * title_obj = lv_msgbox_get_title(mbox);
    lv_obj_t * txt_obj   = lv_msgbox_get_text(mbox);
    lv_obj_t * btns_obj  = lv_msgbox_get_btns(mbox);

    if(title_obj && style_text) lv_obj_add_style(title_obj, style_text, 0);
    if(txt_obj && style_text) lv_obj_add_style(txt_obj, style_text, 0);

    if(btns_obj && style_msgbox_btn) {
        lv_obj_add_style(btns_obj, style_msgbox_btn, LV_PART_ITEMS);
        lv_obj_add_style(btns_obj, style_msgbox_btn, LV_PART_MAIN);
    }
}

static int send_n(int sockfd, const void * buf, int len)
{
    int total = 0;
    int ret;
    const char * p = (const char *)buf;

    while(total < len) {
        ret = send(sockfd, p + total, len - total, 0);
        if(ret < 0) {
            if(errno == EINTR) continue;
            perror("send");
            return -1;
        }
        if(ret == 0) return -1;
        total += ret;
    }

    return 0;
}

static int recv_n(int sockfd, void * buf, int len)
{
    int total = 0;
    int ret;
    char * p = (char *)buf;

    while(total < len) {
        ret = recv(sockfd, p + total, len - total, 0);
        if(ret < 0) {
            if(errno == EINTR) continue;
            perror("recv");
            return -1;
        }
        if(ret == 0) return -1;
        total += ret;
    }

    return 0;
}

// 建立服务器通信
// 这里改成和聊天模块一致：先发长度，再发内容；先收长度，再收内容
static int send_to_server(const char * sendbuf, char * recvbuf, int recvbuf_size)
{
    int sockfd;
    int msglen;
    struct sockaddr_in server_addr;

    if(sendbuf == NULL || recvbuf == NULL || recvbuf_size <= 1)
        return -1;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(sockfd < 0) {
        perror("socket");
        return -1;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_port        = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = inet_addr(SERVER_IP);

    if(connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        close(sockfd);
        return -1;
    }

    msglen = strlen(sendbuf);

    if(send_n(sockfd, &msglen, sizeof(msglen)) < 0) {
        close(sockfd);
        return -1;
    }

    if(send_n(sockfd, sendbuf, msglen) < 0) {
        close(sockfd);
        return -1;
    }

    if(recv_n(sockfd, &msglen, sizeof(msglen)) < 0) {
        close(sockfd);
        return -1;
    }

    if(msglen < 0 || msglen >= recvbuf_size) {
        close(sockfd);
        return -1;
    }

    memset(recvbuf, 0, recvbuf_size);

    if(recv_n(sockfd, recvbuf, msglen) < 0) {
        close(sockfd);
        return -1;
    }

    recvbuf[msglen] = '\0';
    close(sockfd);
    return 0;
}

// 背景点击隐藏键盘
static void bg_event_cb(lv_event_t * e)
{
    if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    lv_obj_t * target  = lv_event_get_target(e);//获取当前被点击的对象
    lv_obj_t * current = lv_event_get_current_target(e);// 获取当前绑定事件的对象

    //如果点击是背景
    if(target == current) {
        hide_keyboard();//隐藏软键盘
    }
}

// 文本框事件
static void ta_event_cb(lv_event_t * e)
{
    lv_obj_t * ta = lv_event_get_target(e);
    if(!ta) return;

    if(kb) {
        lv_keyboard_set_textarea(kb, ta);
        lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);//清除隐藏标志
        lv_obj_move_foreground(kb);// 将软键盘移到最前面
        lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);// 设置软键盘为小写文本模式
    }

    if(pinyin_ime) {
        lv_obj_clear_flag(pinyin_ime, LV_OBJ_FLAG_HIDDEN);
        lv_ime_pinyin_set_mode(pinyin_ime, LV_IME_PINYIN_MODE_K26);// 设置软键盘为小写文本模式

        if(cand_panel) {
            lv_obj_move_foreground(cand_panel);//把候选词面板移到前景
            lv_obj_align_to(cand_panel, kb, LV_ALIGN_OUT_TOP_MID, 0, 0);
        }
    }
}

// 登录按钮
static void login_btn_event_cb(lv_event_t * e)
{
    char sendbuf[256] = {0};
    char recvbuf[256] = {0};

    (void)e;
    hide_keyboard();

    const char * username = lv_textarea_get_text(ta_user);
    const char * password = lv_textarea_get_text(ta_pass);

    if(username == NULL || password == NULL || strlen(username) == 0 || strlen(password) == 0) {
        show_msgbox("提示", "账号或密码不能为空");
        return;
    }

    snprintf(sendbuf, sizeof(sendbuf), "login@%s@%s", username, password);

    if(send_to_server(sendbuf, recvbuf, sizeof(recvbuf)) == -1) {
        show_msgbox("错误", "连接服务器失败");
        return;
    }

    if(strcmp(recvbuf, "ok") == 0) {
        ui_chat_create();
    } else if(strcmp(recvbuf, "invalid") == 0) {
        show_msgbox("提示", "账号或密码不合法");
    } else {
        show_msgbox("提示", "账号或密码错误");
    }
}

// 注册按钮：切到注册界面
static void register_btn_event_cb(lv_event_t * e)
{
    (void)e;
    hide_keyboard();
    ui_register_create();
}

// 忘记密码按钮
static void forget_btn_event_cb(lv_event_t * e)
{
    (void)e;
    hide_keyboard();
    ui_forget_create();
}

// 主题切换按钮
static void theme_btn_event_cb(lv_event_t * e)
{
    (void)e;
    hide_keyboard();

    theme_flag = !theme_flag;

    if(theme_flag == 0) {
        lv_obj_set_style_bg_color(login_scr, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(login_scr, LV_OPA_COVER, 0);
        if(bg_img) lv_img_set_src(bg_img, THEME_IMG_LIGHT_PATH);
    } else {
        lv_obj_set_style_bg_color(login_scr, lv_color_hex(0x1E1E1E), 0);
        lv_obj_set_style_bg_opa(login_scr, LV_OPA_COVER, 0);
        if(bg_img) lv_img_set_src(bg_img, THEME_IMG_DARK_PATH);
    }

    if(bg_img) {
        lv_obj_center(bg_img);
        lv_obj_move_background(bg_img);
    }
}

// 创建登录界面
void ui_login_create(void)
{
    if(style_title == NULL) style_title = my_lv_create_style(FONT_PATH, 36);
    if(style_text == NULL) style_text = my_lv_create_style(FONT_PATH, 20);
    if(style_btntext == NULL) style_btntext = my_lv_create_style(FONT_PATH, 20);
    if(style_placeholder == NULL) style_placeholder = my_lv_create_style(FONT_PATH, 20);
    if(style_msgbox_btn == NULL) style_msgbox_btn = my_lv_create_style(FONT_PATH, 20);

    login_scr = lv_obj_create(NULL);
    lv_obj_clear_flag(login_scr, LV_OBJ_FLAG_SCROLLABLE);                // 取消屏幕滚动功能
    lv_obj_set_style_bg_color(login_scr, lv_color_white(), 0);           // 设置屏幕背景色为白色
    lv_obj_set_style_bg_opa(login_scr, LV_OPA_COVER, 0);                 // 设置背景完全不透明
    lv_obj_add_flag(login_scr, LV_OBJ_FLAG_CLICKABLE);                   // 允许屏幕接收点击事件
    lv_obj_add_event_cb(login_scr, bg_event_cb, LV_EVENT_CLICKED, NULL); // 点背景隐藏键盘的事件

    bg_img = lv_img_create(login_scr);
    lv_img_set_src(bg_img, THEME_IMG_LIGHT_PATH);
    lv_obj_center(bg_img);
    lv_obj_move_background(bg_img); // 放到父容器最底层，作为背景

    lv_obj_t * title = lv_label_create(login_scr);
    lv_label_set_text(title, "心灵感应聊天软件登录");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 35);
    if(style_title) lv_obj_add_style(title, style_title, 0);

    btn_forget = lv_btn_create(login_scr);
    lv_obj_set_size(btn_forget, 110, 40);
    lv_obj_align(btn_forget, LV_ALIGN_TOP_RIGHT, -35, 28);
    lv_obj_add_event_cb(btn_forget, forget_btn_event_cb, LV_EVENT_CLICKED, NULL); // 给忘记按钮的事件

    lv_obj_t * label_forget = lv_label_create(btn_forget);
    lv_label_set_text(label_forget, "忘记密码");
    lv_obj_center(label_forget);
    if(style_btntext) lv_obj_add_style(label_forget, style_btntext, 0);

    ta_user = lv_textarea_create(login_scr);
    lv_obj_set_size(ta_user, 320, 52);
    lv_obj_set_style_bg_opa(ta_user, LV_OPA_70, LV_PART_MAIN); // 设置背景透明度
    lv_obj_set_style_border_width(ta_user, 1, LV_PART_MAIN);   // 设置边宽厚度
    lv_obj_set_style_radius(ta_user, 8, LV_PART_MAIN);
    lv_obj_align(ta_user, LV_ALIGN_TOP_MID, 0, 120);
    lv_textarea_set_placeholder_text(ta_user, "请输入账号");           // 设置文本框里提示的文字
    lv_textarea_set_one_line(ta_user, true);                           // 设置文本框单行输入
    lv_obj_add_event_cb(ta_user, ta_event_cb, LV_EVENT_CLICKED, NULL); // 文本框的事件
    if(style_text) lv_obj_add_style(ta_user, style_text, LV_PART_MAIN);
    if(style_placeholder) lv_obj_add_style(ta_user, style_placeholder, 0);

    ta_pass = lv_textarea_create(login_scr);
    lv_obj_set_size(ta_pass, 320, 52);
    lv_obj_set_style_bg_opa(ta_pass, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_border_width(ta_pass, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(ta_pass, 8, LV_PART_MAIN);
    lv_obj_align_to(ta_pass, ta_user, LV_ALIGN_OUT_BOTTOM_MID, 0, 24);
    lv_textarea_set_placeholder_text(ta_pass, "请输入密码");
    lv_textarea_set_one_line(ta_pass, true);
    lv_textarea_set_password_mode(ta_pass, true);//隐藏密码
    lv_obj_add_event_cb(ta_pass, ta_event_cb, LV_EVENT_CLICKED, NULL); // 文本框事件
    if(style_text) lv_obj_add_style(ta_pass, style_text, LV_PART_MAIN);
    if(style_placeholder) lv_obj_add_style(ta_pass, style_placeholder, 0);

    btn_login = lv_btn_create(login_scr);
    lv_obj_set_size(btn_login, 120, 52);
    lv_obj_align_to(btn_login, ta_pass, LV_ALIGN_OUT_BOTTOM_LEFT, 8, 36);
    lv_obj_add_event_cb(btn_login, login_btn_event_cb, LV_EVENT_CLICKED, NULL); // 登录按钮的触发事件

    lv_obj_t * label_login = lv_label_create(btn_login);
    lv_label_set_text(label_login, "登录");
    lv_obj_center(label_login);
    if(style_btntext) lv_obj_add_style(label_login, style_btntext, 0);

    btn_register = lv_btn_create(login_scr);
    lv_obj_set_size(btn_register, 120, 52);
    lv_obj_align_to(btn_register, ta_pass, LV_ALIGN_OUT_BOTTOM_RIGHT, -8, 36);
    lv_obj_add_event_cb(btn_register, register_btn_event_cb, LV_EVENT_CLICKED, NULL); // 注册按钮的触发事件

    lv_obj_t * label_register = lv_label_create(btn_register);
    lv_label_set_text(label_register, "注册");
    lv_obj_center(label_register);
    if(style_btntext) lv_obj_add_style(label_register, style_btntext, 0);

    btn_theme = lv_btn_create(login_scr);
    lv_obj_set_size(btn_theme, 120, 40);
    lv_obj_align(btn_theme, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_add_event_cb(btn_theme, theme_btn_event_cb, LV_EVENT_CLICKED, NULL); // 主题按钮的事件

    lv_obj_t * label_theme = lv_label_create(btn_theme);
    lv_label_set_text(label_theme, "主题切换");
    lv_obj_center(label_theme);
    if(style_btntext) lv_obj_add_style(label_theme, style_btntext, 0);

    ui_switch_screen(login_scr); // 用于多界面跳转函数

    // 软键盘
    kb = lv_keyboard_create(login_scr);
    lv_obj_set_size(kb, 800, 180);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN); // 设置软键盘为隐藏
    if(style_btntext) lv_obj_add_style(kb, style_btntext, 0);
    lv_obj_move_foreground(kb); // 设置键盘层级：最顶层
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);

    pinyin_ime = lv_ime_pinyin_create(login_scr); // 创建拼音输入法
    lv_obj_add_flag(pinyin_ime, LV_OBJ_FLAG_HIDDEN);
    if(style_text) lv_obj_add_style(pinyin_ime, style_text, 0);
    lv_ime_pinyin_set_keyboard(pinyin_ime, kb);
    lv_ime_pinyin_set_mode(pinyin_ime, LV_IME_PINYIN_MODE_K26); // 设置拼音模式

    cand_panel = lv_ime_pinyin_get_cand_panel(pinyin_ime); // 创建候选字
    lv_obj_set_size(cand_panel, LV_PCT(100), 50);
    lv_obj_align_to(cand_panel, kb, LV_ALIGN_OUT_TOP_MID, 0, 0);
    if(style_text) lv_obj_add_style(cand_panel, style_text, 0);
    lv_obj_move_foreground(cand_panel); // 设置最顶层
}