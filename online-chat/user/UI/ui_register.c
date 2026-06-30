#include "ui_register.h"
#include "ui_style.h"
#include "ui_login.h"

#include <errno.h>

// 服务器配置
#define SERVER_IP "192.168.11.44"
#define SERVER_PORT 30000
#define FONT_PATH "font/simkai.ttf"

// 全局控件
static lv_obj_t * register_scr = NULL;
static lv_obj_t * reg_ta_user  = NULL;
static lv_obj_t * reg_ta_pass  = NULL;
static lv_obj_t * reg_ta_phone = NULL;
static lv_obj_t * reg_btn_ok   = NULL;
static lv_obj_t * reg_btn_back = NULL;
static lv_obj_t * kb           = NULL;
static lv_obj_t * pinyin_ime   = NULL;
static lv_obj_t * cand_panel   = NULL;

// 样式
static lv_style_t * style_title       = NULL;
static lv_style_t * style_text        = NULL;
static lv_style_t * style_btntext     = NULL;
static lv_style_t * style_placeholder = NULL;
static lv_style_t * style_msgbox_btn  = NULL;

static int register_success_flag = 0;

// 隐藏键盘
static void hide_keyboard(void)
{
    if(kb) {
        lv_keyboard_set_textarea(kb, NULL);
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }

    if(pinyin_ime) {
        lv_obj_add_flag(pinyin_ime, LV_OBJ_FLAG_HIDDEN);
    }
    if(cand_panel) {
        lv_obj_add_flag(cand_panel, LV_OBJ_FLAG_HIDDEN);
    }

    if(reg_ta_user) lv_obj_clear_state(reg_ta_user, LV_STATE_FOCUSED);
    if(reg_ta_pass) lv_obj_clear_state(reg_ta_pass, LV_STATE_FOCUSED);
    if(reg_ta_phone) lv_obj_clear_state(reg_ta_phone, LV_STATE_FOCUSED);
}

// 提示框关闭事件
static void msgbox_event_cb(lv_event_t * e)
{
    lv_obj_t * obj = lv_event_get_current_target(e); // 获取当前触发的消息框绑定事件
    lv_obj_del_async(obj);                           // 异步删除消息框
}

// 提示框
static void show_msgbox(const char * title, const char * text)
{
    static const char * btns[] = {"确定", ""};

    lv_obj_t * parent = register_scr ? register_scr : lv_scr_act();
    lv_obj_t * mbox   = lv_msgbox_create(parent, title, text, btns, false);
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

// 服务器通信
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

    lv_obj_t * target  = lv_event_get_target(e);
    lv_obj_t * current = lv_event_get_current_target(e);

    if(target == current) {
        hide_keyboard();
    }
}

// 文本框事件
static void ta_event_cb(lv_event_t * e)
{
    lv_obj_t * ta = lv_event_get_target(e);
    if(!ta) return;

    if(kb) {
        lv_keyboard_set_textarea(kb, ta);
        lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(kb);
        lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);
    }

    if(pinyin_ime) {
        lv_obj_clear_flag(pinyin_ime, LV_OBJ_FLAG_HIDDEN);
        lv_ime_pinyin_set_mode(pinyin_ime, LV_IME_PINYIN_MODE_K26);

        if(cand_panel) {
            lv_obj_move_foreground(cand_panel);
            lv_obj_align_to(cand_panel, kb, LV_ALIGN_OUT_TOP_MID, 0, 0);
        }
    }
}

// 确认注册
static void register_ok_btn_event_cb(lv_event_t * e)
{
    char sendbuf[256] = {0};
    char recvbuf[256] = {0};

    (void)e;
    hide_keyboard();

    const char * username = lv_textarea_get_text(reg_ta_user);
    const char * password = lv_textarea_get_text(reg_ta_pass);
    const char * phone = lv_textarea_get_text(reg_ta_phone);

    if(username == NULL || password == NULL || phone == NULL ||
       strlen(username) == 0 || strlen(password) == 0 || strlen(phone) == 0) {
        show_msgbox("提示", "账号或密码或手机号不能为空");
        return;
    }

    snprintf(sendbuf, sizeof(sendbuf), "register@%s@%s@%s", username, password, phone);

    if(send_to_server(sendbuf, recvbuf, sizeof(recvbuf)) == -1) {
        show_msgbox("错误", "连接服务器失败");
        return;
    }

    if(strcmp(recvbuf, "ok") == 0) {
        register_success_flag = 0;
        ui_login_create();
        show_msgbox("提示", "注册成功，请登录");
    }
    else if(strcmp(recvbuf, "user_exist") == 0) {
        register_success_flag = 0;
        show_msgbox("提示", "账号已存在");
    } else if(strcmp(recvbuf, "invalid") == 0) {
        register_success_flag = 0;
        show_msgbox("提示", "账号或密码或手机号不合法");
    } else {
        register_success_flag = 0;
        show_msgbox("提示", "注册失败");
    }
}

// 返回登录
static void register_back_btn_event_cb(lv_event_t * e)
{
    (void)e;
    hide_keyboard();
    ui_login_create();
}

// 创建注册界面
void ui_register_create()
{
    if(style_title == NULL) style_title = my_lv_create_style(FONT_PATH, 36);
    if(style_text == NULL) style_text = my_lv_create_style(FONT_PATH, 20);
    if(style_btntext == NULL) style_btntext = my_lv_create_style(FONT_PATH, 20);
    if(style_placeholder == NULL) style_placeholder = my_lv_create_style(FONT_PATH, 20);
    if(style_msgbox_btn == NULL) style_msgbox_btn = my_lv_create_style(FONT_PATH, 20);

    register_scr = lv_obj_create(NULL);
    lv_obj_clear_flag(register_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(register_scr, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(register_scr, LV_OPA_COVER, 0);

    lv_obj_add_flag(register_scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(register_scr, bg_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * title = lv_label_create(register_scr);
    lv_label_set_text(title, "用户注册");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 35);
    if(style_title) lv_obj_add_style(title, style_title, 0);

    reg_ta_user = lv_textarea_create(register_scr);
    lv_obj_set_size(reg_ta_user, 320, 52);
    lv_obj_align(reg_ta_user, LV_ALIGN_TOP_MID, 0, 100);
    lv_textarea_set_placeholder_text(reg_ta_user, "请输入注册账号");
    lv_textarea_set_one_line(reg_ta_user, true);
    lv_obj_add_event_cb(reg_ta_user, ta_event_cb, LV_EVENT_CLICKED, NULL);
    if(style_text) lv_obj_add_style(reg_ta_user, style_text, LV_PART_MAIN);
    if(style_placeholder) lv_obj_add_style(reg_ta_user, style_placeholder, 0);

    reg_ta_pass = lv_textarea_create(register_scr);
    lv_obj_set_size(reg_ta_pass, 320, 52);
    lv_obj_align_to(reg_ta_pass, reg_ta_user, LV_ALIGN_OUT_BOTTOM_MID, 0, 24);
    lv_textarea_set_placeholder_text(reg_ta_pass, "请输入注册密码");
    lv_textarea_set_one_line(reg_ta_pass, true);
    lv_textarea_set_password_mode(reg_ta_pass, true);//隐藏密码
    lv_obj_add_event_cb(reg_ta_pass, ta_event_cb, LV_EVENT_CLICKED, NULL);
    if(style_text) lv_obj_add_style(reg_ta_pass, style_text, LV_PART_MAIN);
    if(style_placeholder) lv_obj_add_style(reg_ta_pass, style_placeholder, 0);

    reg_ta_phone = lv_textarea_create(register_scr);
    lv_obj_set_size(reg_ta_phone, 320, 52);
    lv_obj_align_to(reg_ta_phone, reg_ta_pass, LV_ALIGN_OUT_BOTTOM_MID, 0, 24);
    lv_textarea_set_placeholder_text(reg_ta_phone, "请输入手机号");
    lv_textarea_set_one_line(reg_ta_phone, true);
    lv_obj_add_event_cb(reg_ta_phone, ta_event_cb, LV_EVENT_CLICKED, NULL);
    if(style_text) lv_obj_add_style(reg_ta_phone, style_text, LV_PART_MAIN);
    if(style_placeholder) lv_obj_add_style(reg_ta_phone, style_placeholder, 0);

    reg_btn_ok = lv_btn_create(register_scr);
    lv_obj_set_size(reg_btn_ok, 120, 52);
    lv_obj_align_to(reg_btn_ok, reg_ta_phone, LV_ALIGN_OUT_BOTTOM_LEFT, -8, 36);
    lv_obj_add_event_cb(reg_btn_ok, register_ok_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * label_ok = lv_label_create(reg_btn_ok);
    lv_label_set_text(label_ok, "确认注册");
    lv_obj_center(label_ok);
    if(style_btntext) lv_obj_add_style(label_ok, style_btntext, 0);

    reg_btn_back = lv_btn_create(register_scr);
    lv_obj_set_size(reg_btn_back, 120, 52);
    lv_obj_align_to(reg_btn_back, reg_ta_phone, LV_ALIGN_OUT_BOTTOM_RIGHT, 8, 36);
    lv_obj_add_event_cb(reg_btn_back, register_back_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * label_back = lv_label_create(reg_btn_back);
    lv_label_set_text(label_back, "返回登录");
    lv_obj_center(label_back);
    if(style_btntext) lv_obj_add_style(label_back, style_btntext, 0);

    ui_switch_screen(register_scr);//加载注册界面，异步删除旧界面

    kb = lv_keyboard_create(register_scr);
    lv_obj_set_size(kb, 800, 180);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    if(style_btntext) lv_obj_add_style(kb, style_btntext, 0);
    lv_obj_move_foreground(kb);
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);

    pinyin_ime = lv_ime_pinyin_create(register_scr);
    lv_obj_add_flag(pinyin_ime, LV_OBJ_FLAG_HIDDEN);
    if(style_text) lv_obj_add_style(pinyin_ime, style_text, 0);
    lv_ime_pinyin_set_keyboard(pinyin_ime, kb);
    lv_ime_pinyin_set_mode(pinyin_ime, LV_IME_PINYIN_MODE_K26);

    cand_panel = lv_ime_pinyin_get_cand_panel(pinyin_ime);
    lv_obj_set_size(cand_panel, LV_PCT(100), 50);
    lv_obj_align_to(cand_panel, kb, LV_ALIGN_OUT_TOP_MID, 0, 0);
    if(style_text) lv_obj_add_style(cand_panel, style_text, 0);
    lv_obj_move_foreground(cand_panel);
}