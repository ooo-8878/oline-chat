#include "ui_forget.h"
#include "ui_style.h"
#include "ui_chat.h"
#include "ui_register.h"
#include "ui_login.h"

#include <errno.h>
#include <time.h>

/*配置外网vi etc/profile
ifconfig eth0 up
ifconfig eth0 192.168.11.45
route add default gw 192.168.11.254
echo "nameserver 202.96.128.86" > /etc/resolv.conf
echo "nameserver 202.96.128.166" >> /etc/resolv.conf
echo "search mshome.net" >> /etc/resolv.conf
 */

// 服务器配置
#define SERVER_IP "192.168.11.44"
#define SERVER_PORT 30000

#define FONT_PATH "font/simkai.ttf"

static lv_obj_t * forget_src           = NULL; // 忘记界面
static lv_obj_t * forget_ta_user       = NULL; // 账号输入框
static lv_obj_t * forget_ta_phone      = NULL; // 手机号输入框
static lv_obj_t * forget_ta_check_pass = NULL; // 验证码

static lv_obj_t * cand_panel = NULL;

static lv_style_t * style_title       = NULL;
static lv_style_t * style_text        = NULL;
static lv_style_t * style_btntext     = NULL;
static lv_style_t * style_placeholder = NULL;
static lv_style_t * style_msgbox_btn  = NULL;

static lv_obj_t * kb         = NULL;
static lv_obj_t * pinyin_ime = NULL;

static lv_obj_t * for_btn_ok         = NULL;
static lv_obj_t * for_btn_back       = NULL;
static lv_obj_t * for_btn_check_pass = NULL; // 获取验证码按钮

static lv_timer_t * sms_cd_timer = NULL; // 验证码倒计时定时器
static int sms_cd_left           = 0;    // 剩余秒数

static char current_verify_code[16] = "";

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

    if(forget_ta_user) lv_obj_clear_state(forget_ta_user, LV_STATE_FOCUSED);
    if(forget_ta_phone) lv_obj_clear_state(forget_ta_phone, LV_STATE_FOCUSED);
    if(forget_ta_check_pass) lv_obj_clear_state(forget_ta_check_pass, LV_STATE_FOCUSED);
}

// 提示框关闭事件
static void msgbox_event_cb(lv_event_t * e)
{
    lv_obj_t * obj = lv_event_get_current_target(e);
    lv_obj_del_async(obj);
}

// 提示框
static void show_msgbox(const char * title, const char * text)
{
    static const char * btns[] = {"确定", ""};
    lv_obj_t * parent          = forget_src ? forget_src : lv_scr_act();
    lv_obj_t * mbox            = lv_msgbox_create(parent, title, text, btns, false);
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

// 连接服务器
static int send_to_server(const char * sendbuf, char * recvbuf, int recvbuf_size)
{
    int sockfd;
    int msglen;
    struct sockaddr_in server_addr;

    if(sendbuf == NULL || recvbuf == NULL || recvbuf_size <= 1) return -1;

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

// 随机生成验证码
static void gen_verify_code(char * codebuf, int size)
{
    int code;

    // if(codebuf == NULL || size < 7)
    //     return;

    // // 原来这里是写死 18456，这样每次验证码都一样
    // // 现在改成真正随机的6位验证码
    // code = rand() % 900000 + 100000;
    // snprintf(codebuf, size, "%06d", code);
    if(codebuf == NULL || size < 6) return;

    snprintf(codebuf, size, "%s", "18456");
}

// 定时器
static void sms_cd_timer_cb(lv_timer_t * timer)
{
    char buf[64] = {0};

    (void)timer;

    if(for_btn_check_pass == NULL) return;

    sms_cd_left--;

    if(sms_cd_left <= 0) {
        lv_timer_del(sms_cd_timer);
        sms_cd_timer = NULL;
        sms_cd_left  = 0;

        lv_obj_clear_state(for_btn_check_pass, LV_STATE_DISABLED);

        lv_obj_t * label = lv_obj_get_child(for_btn_check_pass, 0);
        if(label) {
            lv_label_set_text(label, "获取验证码");
        }
        return;
    }

    snprintf(buf, sizeof(buf), "%ds后重发", sms_cd_left);
    lv_obj_t * label = lv_obj_get_child(for_btn_check_pass, 0);
    if(label) {
        lv_label_set_text(label, buf);
    }
}

// 请求外部服务器
static int send_sms_code_http(const char * phone, const char * code)
{
    int ret;
    int tcpsock;
    char rbuf[4096]   = {0};
    char ipbuf[64]    = {0};
    char reqbuf[2048] = {0};

    struct hostent * result = gethostbyname("smssend.shumaidata.com");
    if(result == NULL) {
        perror("gethostbyname failed");
        return -1;
    }

    strcpy(ipbuf, inet_ntoa(*((struct in_addr *)((result->h_addr_list)[0]))));
    printf("短信接口IP: %s\n", ipbuf);

    struct sockaddr_in serveraddr;
    memset(&serveraddr, 0, sizeof(serveraddr));
    serveraddr.sin_family      = AF_INET;
    serveraddr.sin_addr.s_addr = inet_addr(ipbuf);
    serveraddr.sin_port        = htons(80);

    tcpsock = socket(AF_INET, SOCK_STREAM, 0);
    if(tcpsock == -1) {
        perror("socket failed");
        return -1;
    }

    ret = connect(tcpsock, (struct sockaddr *)&serveraddr, sizeof(serveraddr));
    if(ret == -1) {
        perror("connect sms server failed");
        close(tcpsock);
        return -1;
    }

    snprintf(reqbuf, sizeof(reqbuf),
             "POST /sms/send?receive=%s&templateId=ada3680fce94126321470cf55744222a&ta=%s HTTP/1.1\r\n"
             "Host:smssend.shumaidata.com\r\n"
             "Authorization:APPCODE 973a70ab520745b59259f60fdcb1df9a\r\n"
             "Content-Length:0\r\n\r\n",
             phone, code);

    printf("发送请求:\n%s\n", reqbuf);

    ret = send(tcpsock, reqbuf, strlen(reqbuf), 0);
    if(ret <= 0) {
        perror("send sms request failed");
        close(tcpsock);
        return -1;
    }

    ret = recv(tcpsock, rbuf, sizeof(rbuf) - 1, 0);
    if(ret <= 0) {
        perror("recv sms response failed");
        close(tcpsock);
        return -1;
    }

    rbuf[ret] = '\0';
    printf("短信接口返回:\n%s\n", rbuf);

    close(tcpsock);

    if(strstr(rbuf, "\"success\":true") != NULL || strstr(rbuf, "\"code\":200") != NULL) {
        return 0;
    }

    return -1;
}

// 返回登录
static void forget_back_btn_event_cb(lv_event_t * e)
{
    (void)e;
    hide_keyboard();

    if(sms_cd_timer) {
        lv_timer_del(sms_cd_timer);
        sms_cd_timer = NULL;
    }
    sms_cd_left = 0;

    current_verify_code[0] = '\0';

    ui_login_create();
}

// 点击获取验证码按钮，获取文本框手机号和账号，并且调用随机数发送给服务器
static void forget_btn_check_pass_event_cb(lv_event_t * e)
{
    char sendbuf[256] = {0};
    char recvbuf[256] = {0};

    (void)e;
    hide_keyboard();

    if(sms_cd_left > 0) {
        show_msgbox("提示", "60s后才能发送");
        return;
    }

    const char * username = lv_textarea_get_text(forget_ta_user);
    const char * phone    = lv_textarea_get_text(forget_ta_phone);

    if(username == NULL || strlen(username) == 0 || phone == NULL || strlen(phone) == 0) {
        show_msgbox("提示", "请输入账号和手机号");
        return;
    }

    gen_verify_code(current_verify_code, sizeof(current_verify_code));
    printf("当前生成验证码: %s\n", current_verify_code);

    // 这里不再把验证码发给服务器缓存
    // 服务器只校验账号和手机号是否匹配
    snprintf(sendbuf, sizeof(sendbuf), "phonecheck@%s@%s", username, phone);

    if(send_to_server(sendbuf, recvbuf, sizeof(recvbuf)) == -1) {
        show_msgbox("错误", "连接服务器失败");
        return;
    }

    if(strcmp(recvbuf, "ok") == 0) {
        if(send_sms_code_http(phone, current_verify_code) == -1) {
            show_msgbox("提示", "短信发送失败");
            return;
        }

        sms_cd_left = 60;
        lv_obj_add_state(for_btn_check_pass, LV_STATE_DISABLED);

        lv_obj_t * label = lv_obj_get_child(for_btn_check_pass, 0);
        if(label) {
            lv_label_set_text(label, "60s后重发");
        }

        if(sms_cd_timer) {
            lv_timer_del(sms_cd_timer);
            sms_cd_timer = NULL;
        }
        sms_cd_timer = lv_timer_create(sms_cd_timer_cb, 1000, NULL);

        show_msgbox("提示", "验证码已发送，60s后才能再次发送");
    } else if(strcmp(recvbuf, "no_user") == 0) {
        show_msgbox("提示", "账号不存在");
    } else if(strcmp(recvbuf, "phone_error") == 0) {
        show_msgbox("提示", "手机号与账号不匹配");
    } else {
        show_msgbox("提示", "获取验证码失败");
    }
}

// 确认找回，然后服务器返回消息框里面提示密码
static void forget_ok_btn_event_cb(lv_event_t * e)
{
    char sendbuf[256] = {0};
    char recvbuf[256] = {0};
    char msgbuf[256]  = {0};

    (void)e;
    hide_keyboard();

    const char * username = lv_textarea_get_text(forget_ta_user);
    const char * phone    = lv_textarea_get_text(forget_ta_phone);
    const char * code     = lv_textarea_get_text(forget_ta_check_pass);

    if(username == NULL || strlen(username) == 0 || phone == NULL || strlen(phone) == 0 || code == NULL ||
       strlen(code) == 0) {
        show_msgbox("提示", "请输入完整信息");
        return;
    }

    if(strlen(current_verify_code) == 0) {
        show_msgbox("提示", "请先获取验证码");
        return;
    }

    // 先在客户端本地校验验证码
    // 这样服务器端就不需要保存全局验证码缓存了，避免多个用户同时找回时串号
    if(strcmp(code, current_verify_code) != 0) {
        show_msgbox("提示", "验证码错误");
        return;
    }

    snprintf(sendbuf, sizeof(sendbuf), "findpass@%s@%s@%s", username, phone, code);

    if(send_to_server(sendbuf, recvbuf, sizeof(recvbuf)) == -1) {
        show_msgbox("错误", "连接服务器失败");
        return;
    }

    if(strncmp(recvbuf, "pass@", 5) == 0) {
        snprintf(msgbuf, sizeof(msgbuf), "找回成功，密码是：%s", recvbuf + 5);
        show_msgbox("提示", msgbuf);
    } else if(strcmp(recvbuf, "no_user") == 0) {
        show_msgbox("提示", "账号不存在");
    } else if(strcmp(recvbuf, "phone_error") == 0) {
        show_msgbox("提示", "手机号与账号不匹配");
    } else if(strcmp(recvbuf, "invalid") == 0) {
        show_msgbox("提示", "请求格式错误");
    } else {
        show_msgbox("提示", recvbuf);
    }
}

// 忘记密码，找回界面
void ui_forget_create(void)
{
    // 程序每次进入找回页面时初始化一次随机种子
    srand((unsigned int)(time(NULL) ^ getpid()));

    if(style_title == NULL) style_title = my_lv_create_style(FONT_PATH, 36);
    if(style_text == NULL) style_text = my_lv_create_style(FONT_PATH, 20);
    if(style_btntext == NULL) style_btntext = my_lv_create_style(FONT_PATH, 20);
    if(style_placeholder == NULL) style_placeholder = my_lv_create_style(FONT_PATH, 20);
    if(style_msgbox_btn == NULL) style_msgbox_btn = my_lv_create_style(FONT_PATH, 20);

    forget_src = lv_obj_create(NULL);
    lv_obj_clear_flag(forget_src, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(forget_src, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(forget_src, LV_OPA_COVER, 0);

    lv_obj_add_flag(forget_src, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(forget_src, bg_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * title = lv_label_create(forget_src);
    lv_label_set_text(title, "找回密码");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 35);
    if(style_title) lv_obj_add_style(title, style_title, 0);

    forget_ta_user = lv_textarea_create(forget_src);
    lv_obj_set_size(forget_ta_user, 320, 52);
    lv_obj_align(forget_ta_user, LV_ALIGN_TOP_MID, 0, 100);
    lv_textarea_set_placeholder_text(forget_ta_user, "请输入账号");
    lv_textarea_set_one_line(forget_ta_user, true);
    lv_obj_add_event_cb(forget_ta_user, ta_event_cb, LV_EVENT_CLICKED, NULL);
    if(style_text) lv_obj_add_style(forget_ta_user, style_text, LV_PART_MAIN);
    if(style_placeholder) lv_obj_add_style(forget_ta_user, style_placeholder, 0);

    forget_ta_phone = lv_textarea_create(forget_src);
    lv_obj_set_size(forget_ta_phone, 320, 52);
    lv_obj_align_to(forget_ta_phone, forget_ta_user, LV_ALIGN_OUT_BOTTOM_MID, 0, 24);
    lv_textarea_set_placeholder_text(forget_ta_phone, "请输入手机号");
    lv_textarea_set_one_line(forget_ta_phone, true);
    lv_obj_add_event_cb(forget_ta_phone, ta_event_cb, LV_EVENT_CLICKED, NULL);
    if(style_text) lv_obj_add_style(forget_ta_phone, style_text, LV_PART_MAIN);
    if(style_placeholder) lv_obj_add_style(forget_ta_phone, style_placeholder, 0);

    forget_ta_check_pass = lv_textarea_create(forget_src);
    lv_obj_set_size(forget_ta_check_pass, 200, 52);
    lv_obj_align_to(forget_ta_check_pass, forget_ta_phone, LV_ALIGN_OUT_RIGHT_MID, 20, 0);
    lv_textarea_set_placeholder_text(forget_ta_check_pass, "请输入验证码");
    lv_textarea_set_one_line(forget_ta_check_pass, true);
    lv_obj_add_event_cb(forget_ta_check_pass, ta_event_cb, LV_EVENT_CLICKED, NULL);
    if(style_text) lv_obj_add_style(forget_ta_check_pass, style_text, LV_PART_MAIN);
    if(style_placeholder) lv_obj_add_style(forget_ta_check_pass, style_placeholder, 0);

    for_btn_back = lv_btn_create(forget_src);
    lv_obj_set_size(for_btn_back, 120, 52);
    lv_obj_align_to(for_btn_back, forget_ta_phone, LV_ALIGN_OUT_BOTTOM_RIGHT, 8, 36);
    lv_obj_add_event_cb(for_btn_back, forget_back_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * label_back = lv_label_create(for_btn_back);
    lv_label_set_text(label_back, "返回登录");
    lv_obj_center(label_back);
    if(style_btntext) lv_obj_add_style(label_back, style_btntext, 0);

    for_btn_ok = lv_btn_create(forget_src);
    lv_obj_set_size(for_btn_ok, 120, 52);
    lv_obj_align_to(for_btn_ok, forget_ta_phone, LV_ALIGN_OUT_BOTTOM_LEFT, -8, 36);
    lv_obj_add_event_cb(for_btn_ok, forget_ok_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * label_ok = lv_label_create(for_btn_ok);
    lv_label_set_text(label_ok, "确认找回");
    lv_obj_center(label_ok);
    if(style_btntext) lv_obj_add_style(label_ok, style_btntext, 0);

    for_btn_check_pass = lv_btn_create(forget_src);
    lv_obj_set_size(for_btn_check_pass, 120, 52);
    lv_obj_align_to(for_btn_check_pass, forget_ta_check_pass, LV_ALIGN_OUT_BOTTOM_LEFT, 8, 36);
    lv_obj_add_event_cb(for_btn_check_pass, forget_btn_check_pass_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * label_check_pass = lv_label_create(for_btn_check_pass);
    lv_label_set_text(label_check_pass, "获取验证码");
    lv_obj_center(label_check_pass);
    if(style_btntext) lv_obj_add_style(label_check_pass, style_btntext, 0);

    ui_switch_screen(forget_src); // 加载注册界面，异步删除旧界面

    kb = lv_keyboard_create(forget_src);
    lv_obj_set_size(kb, 800, 180);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    if(style_btntext) lv_obj_add_style(kb, style_btntext, 0);
    lv_obj_move_foreground(kb);
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);

    pinyin_ime = lv_ime_pinyin_create(forget_src);
    lv_obj_add_flag(pinyin_ime, LV_OBJ_FLAG_HIDDEN);
    if(style_text) lv_obj_add_style(pinyin_ime, style_text, 0);
    lv_ime_pinyin_set_keyboard(pinyin_ime, kb);
    lv_ime_pinyin_set_mode(pinyin_ime, LV_IME_PINYIN_MODE_K26);

    cand_panel = lv_ime_pinyin_get_cand_panel(pinyin_ime);
    lv_obj_set_size(cand_panel, LV_PCT(100), 50);
    lv_obj_align_to(cand_panel, kb, LV_ALIGN_OUT_TOP_MID, 0, 0);
    if(style_text) lv_obj_add_style(cand_panel, style_text, 0);
    lv_obj_move_foreground(cand_panel);
    lv_obj_add_flag(cand_panel, LV_OBJ_FLAG_HIDDEN);
}