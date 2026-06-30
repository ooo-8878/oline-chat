#include "ui_chat.h"
#include "ui_style.h"
#include "ui_login.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

// 服务器配置
#define SERVER_IP "192.168.11.44"
#define SERVER_PORT 30000
#define FONT_PATH "font/simkai.ttf"

// 固定分辨率 800x480
#define SCREEN_W 800
#define SCREEN_H 480

// 会话数量和每个会话聊天记录最大长度
#define MAX_CHAT_SESSION_NUM 50
#define MAX_CHAT_HISTORY_LEN 8192

//  全局对象
static lv_obj_t * chat_scr = NULL; // 聊天界面的根对象

static lv_obj_t * friend_title = NULL; // 好友列表标题
static lv_obj_t * friend_list  = NULL; // 好友列表控件

static lv_obj_t * msg_title = NULL; // 发送消息区域标题
static lv_obj_t * ta_msg    = NULL; // 发送消息输入框
static lv_obj_t * send_btn  = NULL; // 发送消息按钮

static lv_obj_t * recv_title = NULL; // 接收消息区域标题
static lv_obj_t * recv_box   = NULL; // 接收消息滚动区域
static lv_obj_t * recv_text  = NULL; // 接收消息显示标签

static lv_obj_t * emoji_title = NULL; // 表情区域标题
static lv_obj_t * emoji_area  = NULL; // 表情包显示区域
static lv_obj_t * emoji_btn   = NULL; // 发送表情按钮

static lv_obj_t * file_title = NULL; // 文件区域标题（当前代码里没有真正创建显示）
static lv_obj_t * ta_file    = NULL; // 文件路径输入框
static lv_obj_t * file_btn   = NULL; // 发送文件按钮

static lv_obj_t * exit_btn = NULL; // 退出聊天按钮

static lv_obj_t * kb         = NULL; // 软键盘对象
static lv_obj_t * pinyin_ime = NULL; // 拼音输入法对象
static lv_obj_t * cand_panel = NULL; // 拼音候选词面板对象

// 三个表情包按钮
static lv_obj_t * emoji1_btn = NULL; // 第一个表情按钮
static lv_obj_t * emoji2_btn = NULL; // 第二个表情按钮
static lv_obj_t * emoji3_btn = NULL; // 第三个表情按钮

// 字体样式
static lv_style_t * style_title   = NULL; // 标题样式
static lv_style_t * style_text    = NULL; // 普通文本样式
static lv_style_t * style_btntext = NULL; // 按钮文字样式

// 当前选中的好友
static char current_peer_ip[20]         = {0}; // 当前选中好友的IP地址
static unsigned short current_peer_port = 0;   // 当前选中好友的端口号

// 网络
static int tcpsock = -1;   // 与聊天服务器连接的socket
static pthread_t recv_tid; // 接收线程线程ID

// UI 缓存
static char recv_cache[4096]   = {0}; // 接收到的普通消息缓存
static char friend_cache[2048] = {0}; // 接收到的好友列表缓存
static int new_recv_flag       = 0;   // 是否有新的接收消息标志
static int new_friend_flag     = 0;   // 是否有新的好友列表标志

// 当前缓存消息的来源好友
static char recv_from_ip[20]         = {0};
static unsigned short recv_from_port = 0;

static lv_timer_t * chat_timer = NULL; // 定时器
static int list_tick           = 0;

static char current_emoji_path[256] = ""; // 当前选中的表情文件真实路径

// 每个好友一份聊天记录
struct chat_session
{
    char ip[20];
    unsigned short port;
    char history[MAX_CHAT_HISTORY_LEN];
};

static struct chat_session g_sessions[MAX_CHAT_SESSION_NUM];
static int g_session_count = 0;

static int send_n(int sockfd, const void * buf, int len);


// 基础函数
static void msgbox_event_cb(lv_event_t * e)
{
    lv_obj_t * obj = lv_event_get_current_target(e);
    lv_obj_del_async(obj);
}

static void show_msgbox(const char * title, const char * text)
{
    static const char * btns[] = {"确定", ""};

    lv_obj_t * parent = chat_scr ? chat_scr : lv_scr_act();
    lv_obj_t * mbox   = lv_msgbox_create(parent, title, text, btns, false);
    lv_obj_center(mbox);
    lv_obj_add_event_cb(mbox, msgbox_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t * title_obj = lv_msgbox_get_title(mbox);
    lv_obj_t * txt_obj   = lv_msgbox_get_text(mbox);
    lv_obj_t * btns_obj  = lv_msgbox_get_btns(mbox);

    if(title_obj && style_text) lv_obj_add_style(title_obj, style_text, 0);
    if(txt_obj && style_text) lv_obj_add_style(txt_obj, style_text, 0);
    if(btns_obj && style_btntext) {
        lv_obj_add_style(btns_obj, style_btntext, LV_PART_ITEMS);
        lv_obj_add_style(btns_obj, style_btntext, LV_PART_MAIN);
    }
}

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

    if(ta_msg) lv_obj_clear_state(ta_msg, LV_STATE_FOCUSED);
    if(ta_file) lv_obj_clear_state(ta_file, LV_STATE_FOCUSED);
}

// 文本框绑定键盘弹出事件
static void input_ta_event_cb(lv_event_t * e)
{
    lv_obj_t * ta = lv_event_get_target(e);
    if(!ta) return;

    if(current_peer_ip[0] == '\0' || current_peer_port == 0) {
        return;
    }

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

// 点背景隐藏键盘
static void bg_event_cb(lv_event_t * e)
{
    if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    lv_obj_t * target  = lv_event_get_target(e);
    lv_obj_t * current = lv_event_get_current_target(e);

    if(target == current) {
        hide_keyboard();
    }
}


// 会话管理：每个好友单独聊天记录
static struct chat_session * chat_find_session(const char * ip, unsigned short port)
{
    int i;

    if(ip == NULL || ip[0] == '\0' || port == 0) return NULL;

    for(i = 0; i < g_session_count; i++) {
        if(strcmp(g_sessions[i].ip, ip) == 0 && g_sessions[i].port == port) {
            return &g_sessions[i];
        }
    }

    return NULL;
}
//会话缓存
static struct chat_session * chat_get_or_create_session(const char * ip, unsigned short port)
{
    struct chat_session * s;

    if(ip == NULL || ip[0] == '\0' || port == 0) return NULL;

    s = chat_find_session(ip, port);
    if(s != NULL) return s;

    if(g_session_count >= MAX_CHAT_SESSION_NUM) {
        return NULL;
    }

    memset(&g_sessions[g_session_count], 0, sizeof(g_sessions[g_session_count]));
    strncpy(g_sessions[g_session_count].ip, ip, sizeof(g_sessions[g_session_count].ip) - 1);
    g_sessions[g_session_count].port = port;
    g_sessions[g_session_count].history[0] = '\0';

    g_session_count++;
    return &g_sessions[g_session_count - 1];
}

static void chat_load_current_session_to_ui(void)
{
    struct chat_session * s;

    if(recv_text == NULL) return;

    if(current_peer_ip[0] == '\0' || current_peer_port == 0) {
        lv_label_set_text(recv_text, "");
        return;
    }

    s = chat_get_or_create_session(current_peer_ip, current_peer_port);
    if(s == NULL) {
        lv_label_set_text(recv_text, "");
        return;
    }

    lv_label_set_text(recv_text, s->history);
}

static void chat_append_line_to_session(const char * ip, unsigned short port, const char * msg)
{
    struct chat_session * s;
    char newbuf[MAX_CHAT_HISTORY_LEN];

    if(ip == NULL || msg == NULL) return;

    s = chat_get_or_create_session(ip, port);
    if(s == NULL) return;

    memset(newbuf, 0, sizeof(newbuf));

    if(strlen(s->history) == 0) {
        snprintf(newbuf, sizeof(newbuf), "%s\n", msg);
    } else {
        snprintf(newbuf, sizeof(newbuf), "%s%s\n", s->history, msg);
    }

    strncpy(s->history, newbuf, sizeof(s->history) - 1);
    s->history[sizeof(s->history) - 1] = '\0';

    // 如果当前正在查看这个好友，就实时刷新右侧聊天框
    if(strcmp(current_peer_ip, ip) == 0 && current_peer_port == port) {
        lv_label_set_text(recv_text, s->history);
    }
}


void chat_append_recv_msg(const char * msg)
{
    if(msg == NULL) return;

    if(recv_from_ip[0] != '\0' && recv_from_port != 0) {
        chat_append_line_to_session(recv_from_ip, recv_from_port, msg);
    } else if(current_peer_ip[0] != '\0' && current_peer_port != 0) {
        chat_append_line_to_session(current_peer_ip, current_peer_port, msg);
    }
}

// 保留原接口名，尽量不动别的地方
void chat_append_send_msg(const char * msg)
{
    char line[4096] = {0};

    if(msg == NULL) return;
    if(current_peer_ip[0] == '\0' || current_peer_port == 0) return;

    snprintf(line, sizeof(line), "我: %s", msg);
    chat_append_line_to_session(current_peer_ip, current_peer_port, line);
}





// 防止粘包，先发送长度，再发内容
static int send_str_to_server(const char * buf)
{
    int len;

    if(tcpsock < 0 || buf == NULL) return -1;

    len = strlen(buf);

    if(send_n(tcpsock, &len, sizeof(len)) < 0) return -1;

    if(len > 0) {
        if(send_n(tcpsock, buf, len) < 0) return -1;
    }

    return 0;
}

static void request_friend_list(void)
{
    send_str_to_server("getlist");
}

// 发送
static int send_n(int sockfd, const void * buf, int len)
{
    int total = 0;
    int ret;
    const char * p = (const char *)buf;

    while(total < len) {
        ret = send(sockfd, p + total, len - total, 0);
        if(ret < 0) {
            if(errno == EINTR) continue;
            perror("send failed");
            return -1;
        }
        if(ret == 0) {
            return -1;
        }
        total += ret;
    }

    return 0;
}

// 接收
static int recv_n(int sockfd, void * buf, int len)
{
    int total = 0;
    int ret;
    char * p = (char *)buf;

    while(total < len) {
        ret = recv(sockfd, p + total, len - total, 0);
        if(ret < 0) {
            if(errno == EINTR) continue;
            perror("recv failed");
            return -1;
        }
        if(ret == 0) {
            return -1;
        }
        total += ret;
    }

    return 0;
}


// 控件使能
static void chat_set_send_widgets_enable(int enable)
{
    if(enable) {
        if(ta_msg) lv_obj_clear_state(ta_msg, LV_STATE_DISABLED);
        if(send_btn) lv_obj_clear_state(send_btn, LV_STATE_DISABLED);
        if(ta_file) lv_obj_clear_state(ta_file, LV_STATE_DISABLED);
        if(file_btn) lv_obj_clear_state(file_btn, LV_STATE_DISABLED);
        if(emoji_btn) lv_obj_clear_state(emoji_btn, LV_STATE_DISABLED);

        if(emoji1_btn) lv_obj_clear_state(emoji1_btn, LV_STATE_DISABLED);
        if(emoji2_btn) lv_obj_clear_state(emoji2_btn, LV_STATE_DISABLED);
        if(emoji3_btn) lv_obj_clear_state(emoji3_btn, LV_STATE_DISABLED);
    } else {
        if(ta_msg) lv_obj_add_state(ta_msg, LV_STATE_DISABLED);
        if(send_btn) lv_obj_add_state(send_btn, LV_STATE_DISABLED);
        if(ta_file) lv_obj_add_state(ta_file, LV_STATE_DISABLED);
        if(file_btn) lv_obj_add_state(file_btn, LV_STATE_DISABLED);
        if(emoji_btn) lv_obj_add_state(emoji_btn, LV_STATE_DISABLED);

        if(emoji1_btn) lv_obj_add_state(emoji1_btn, LV_STATE_DISABLED);
        if(emoji2_btn) lv_obj_add_state(emoji2_btn, LV_STATE_DISABLED);
        if(emoji3_btn) lv_obj_add_state(emoji3_btn, LV_STATE_DISABLED);
    }
}


// 好友列表
static void friend_item_event_cb(lv_event_t * e)
{
    lv_obj_t * btn   = lv_event_get_target(e);
    const char * txt = lv_list_get_btn_text(friend_list, btn);
    if(txt == NULL) return;

    {
        char ip[20]         = {0};
        unsigned short port = 0;

        if(sscanf(txt, "%19[^:]:%hu", ip, &port) == 2) {
            strcpy(current_peer_ip, ip);
            current_peer_port = port;

            chat_set_send_widgets_enable(1);

            // 切换到当前好友自己的聊天记录
            chat_load_current_session_to_ui();

            {
                char buf[128] = {0};
                snprintf(buf, sizeof(buf), "已选择好友: %s:%hu", current_peer_ip, current_peer_port);
                show_msgbox("提示", buf);
            }
        }
    }
}

// 更新好友列表
void chat_update_friend_list(const char * list_str)
{
    if(friend_list == NULL || list_str == NULL) return;

    lv_obj_clean(friend_list);

    {
        char buf[2048] = {0};
        char * token;

        strncpy(buf, list_str, sizeof(buf) - 1);
        token = strtok(buf, "@");

        while(token != NULL) {
            if(strlen(token) > 0) {
                if(strchr(token, ':') != NULL) {
                    lv_obj_t * btn = lv_list_add_btn(friend_list, NULL, token);

                    if(style_text) lv_obj_add_style(btn, style_text, 0);
                    lv_obj_add_event_cb(btn, friend_item_event_cb, LV_EVENT_CLICKED, NULL);
                }
            }

            token = strtok(NULL, "@");
        }
    }
}


// 网络部分

static int connect_server(void)
{
    struct sockaddr_in serveraddr;
    int ret;

    tcpsock = socket(AF_INET, SOCK_STREAM, 0);
    if(tcpsock < 0) {
        perror("socket");
        return -1;
    }

    memset(&serveraddr, 0, sizeof(serveraddr));
    serveraddr.sin_family      = AF_INET;
    serveraddr.sin_addr.s_addr = inet_addr(SERVER_IP);
    serveraddr.sin_port        = htons(SERVER_PORT);

    ret = connect(tcpsock, (struct sockaddr *)&serveraddr, sizeof(serveraddr));
    if(ret < 0) {
        perror("connect");
        close(tcpsock);
        tcpsock = -1;
        return -1;
    }

    return 0;
}

// 接收服务器线程
static void * recv_servermsg(void * arg)
{
    char rbuf[4096];
    int msglen;
    (void)arg;

    while(1) {
        char tmpbuf[4096];
        char *p1, *p2, *p3, *p4, *p5;

        msglen = 0;
        if(recv_n(tcpsock, &msglen, sizeof(msglen)) < 0) {
            break;
        }

        if(msglen < 0 || msglen >= (int)sizeof(rbuf)) {
            break;
        }

        if(msglen == 0) {
            continue;
        }

        memset(rbuf, 0, sizeof(rbuf));
        if(recv_n(tcpsock, rbuf, msglen) < 0) {
            break;
        }

        rbuf[msglen] = '\0';

        if(strcmp(rbuf, "quit") == 0) {
            break;
        }
        if(strcmp(rbuf, "online_ok") == 0) {
            continue;
        }

        // 好友列表协议：list@ip:port@ip:port@
        if(strncmp(rbuf, "list@", 5) == 0) {
            memset(friend_cache, 0, sizeof(friend_cache));
            strncpy(friend_cache, rbuf + 5, sizeof(friend_cache) - 1);
            new_friend_flag = 1;
            continue;
        }

        // 兼容直接发 ip:port@ip:port@ 的情况
        if(strchr(rbuf, ':') != NULL && strstr(rbuf, "msg@") == NULL &&
           strstr(rbuf, "file@") == NULL && strstr(rbuf, "emoji@") == NULL) {
            memset(friend_cache, 0, sizeof(friend_cache));
            strncpy(friend_cache, rbuf, sizeof(friend_cache) - 1);
            new_friend_flag = 1;
            continue;
        }

        // 复制一份用于协议解析
        memset(tmpbuf, 0, sizeof(tmpbuf));
        strncpy(tmpbuf, rbuf, sizeof(tmpbuf) - 1);

        p1 = strtok(tmpbuf, "@");
        if(p1 == NULL) {
            memset(recv_cache, 0, sizeof(recv_cache));
            strncpy(recv_cache, rbuf, sizeof(recv_cache) - 1);

            memset(recv_from_ip, 0, sizeof(recv_from_ip));
            recv_from_port = 0;

            new_recv_flag = 1;
            continue;
        }

        // 普通文本消息
        if(strcmp(p1, "msg") == 0) {
            p2 = strtok(NULL, "@"); // 发送方ip
            p3 = strtok(NULL, "@"); // 发送方端口
            p4 = strtok(NULL, "");  // 真正消息内容

            memset(recv_from_ip, 0, sizeof(recv_from_ip));
            recv_from_port = 0;

            if(p2 && p3 && p4) {
                strncpy(recv_from_ip, p2, sizeof(recv_from_ip) - 1);
                recv_from_port = (unsigned short)atoi(p3);
                snprintf(recv_cache, sizeof(recv_cache), "%s:%s -> %s", p2, p3, p4);
            } else {
                snprintf(recv_cache, sizeof(recv_cache), "%s", rbuf);
            }

            new_recv_flag = 1;
            continue;
        }

        // 文件消息
        if(strcmp(p1, "file") == 0) {
            long filesize;
            long remaining;
            int fd;
            char savepath[512];
            char filebuf[1024];
            int once;

            p2 = strtok(NULL, "@"); // 发送方ip
            p3 = strtok(NULL, "@"); // 发送方端口
            p4 = strtok(NULL, "@"); // 文件名
            p5 = strtok(NULL, "@"); // 文件大小

            memset(recv_from_ip, 0, sizeof(recv_from_ip));
            recv_from_port = 0;

            if(p2 == NULL || p3 == NULL || p4 == NULL || p5 == NULL) {
                snprintf(recv_cache, sizeof(recv_cache), "收到异常文件协议: %s", rbuf);
                new_recv_flag = 1;
                continue;
            }

            strncpy(recv_from_ip, p2, sizeof(recv_from_ip) - 1);
            recv_from_port = (unsigned short)atoi(p3);

            filesize  = atol(p5);
            remaining = filesize;

            mkdir("./recvfiles", 0777);
            snprintf(savepath, sizeof(savepath), "./recvfiles/%s", p4);

            fd = open(savepath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
            if(fd == -1) {
                perror("open recv file failed");

                while(remaining > 0) {
                    once = remaining > (long)sizeof(filebuf) ? sizeof(filebuf) : (int)remaining;
                    if(recv_n(tcpsock, filebuf, once) < 0) {
                        break;
                    }
                    remaining -= once;
                }

                snprintf(recv_cache, sizeof(recv_cache), "%s:%s -> 接收文件失败: %s", p2, p3, p4);
                new_recv_flag = 1;
                continue;
            }

            while(remaining > 0) {
                once = remaining > (long)sizeof(filebuf) ? sizeof(filebuf) : (int)remaining;

                if(recv_n(tcpsock, filebuf, once) < 0) {
                    perror("recv file data failed");
                    close(fd);
                    fd = -1;
                    break;
                }

                if(write(fd, filebuf, once) != once) {
                    perror("write recv file failed");
                    close(fd);
                    fd = -1;
                    break;
                }

                remaining -= once;
            }

            if(fd != -1) {
                close(fd);
            }

            if(remaining <= 0) {
                snprintf(recv_cache, sizeof(recv_cache), "%s:%s -> 发送文件: %s", p2, p3, p4);
            } else {
                snprintf(recv_cache, sizeof(recv_cache), "%s:%s -> 接收文件中断: %s", p2, p3, p4);
            }

            new_recv_flag = 1;
            continue;
        }

        // 表情包消息
        if(strcmp(p1, "emoji") == 0) {
            long emojisize;
            long remaining;
            int fd;
            int once;
            char savepath[512];
            char filebuf[1024];

            p2 = strtok(NULL, "@"); // 发送方ip
            p3 = strtok(NULL, "@"); // 发送方端口
            p4 = strtok(NULL, "@"); // 表情大小

            memset(recv_from_ip, 0, sizeof(recv_from_ip));
            recv_from_port = 0;

            if(p2 == NULL || p3 == NULL || p4 == NULL) {
                snprintf(recv_cache, sizeof(recv_cache), "收到异常表情协议: %s", rbuf);
                new_recv_flag = 1;
                continue;
            }

            strncpy(recv_from_ip, p2, sizeof(recv_from_ip) - 1);
            recv_from_port = (unsigned short)atoi(p3);

            emojisize = atol(p4);
            remaining = emojisize;

            mkdir("./recvemoji", 0777);
            snprintf(savepath, sizeof(savepath), "./recvemoji/emoji_%s_%s.jpg", p2, p3);

            fd = open(savepath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
            if(fd == -1) {
                perror("open recv emoji failed");

                while(remaining > 0) {
                    once = remaining > (long)sizeof(filebuf) ? sizeof(filebuf) : (int)remaining;
                    if(recv_n(tcpsock, filebuf, once) < 0) {
                        break;
                    }
                    remaining -= once;
                }

                snprintf(recv_cache, sizeof(recv_cache), "%s:%s -> 接收表情失败", p2, p3);
                new_recv_flag = 1;
                continue;
            }

            while(remaining > 0) {
                once = remaining > (long)sizeof(filebuf) ? sizeof(filebuf) : (int)remaining;

                if(recv_n(tcpsock, filebuf, once) < 0) {
                    perror("recv emoji data failed");
                    close(fd);
                    fd = -1;
                    break;
                }

                if(write(fd, filebuf, once) != once) {
                    perror("write recv emoji failed");
                    close(fd);
                    fd = -1;
                    break;
                }

                remaining -= once;
            }

            if(fd != -1) {
                close(fd);
            }

            if(remaining <= 0) {
                snprintf(recv_cache, sizeof(recv_cache), "%s:%s -> 收到表情: %s", p2, p3, savepath);
            } else {
                snprintf(recv_cache, sizeof(recv_cache), "%s:%s -> 接收表情中断", p2, p3);
            }

            new_recv_flag = 1;
            continue;
        }

        memset(recv_cache, 0, sizeof(recv_cache));
        strncpy(recv_cache, rbuf, sizeof(recv_cache) - 1);

        memset(recv_from_ip, 0, sizeof(recv_from_ip));
        recv_from_port = 0;

        new_recv_flag = 1;
    }

    return NULL;
}

// 定时器刷新界面
static void chat_timer_cb(lv_timer_t * timer)
{
    (void)timer;

    list_tick++;
    if(list_tick >= 10) { // 100ms * 10 = 1000ms
        request_friend_list();
        list_tick = 0;
    }

    if(new_friend_flag) {
        new_friend_flag = 0;
        chat_update_friend_list(friend_cache);
    }

    if(new_recv_flag) {
        new_recv_flag = 0;
        chat_append_recv_msg(recv_cache);
    }
}




// 发送按钮
static void send_btn_event_cb(lv_event_t * e)
{
    const char * msg;
    char sendbuf[2048] = {0};

    (void)e;
    hide_keyboard();

    msg = lv_textarea_get_text(ta_msg);

    if(current_peer_ip[0] == '\0' || current_peer_port == 0) {
        show_msgbox("提示", "请先选择好友");
        return;
    }

    if(msg == NULL || strlen(msg) == 0) {
        show_msgbox("提示", "请输入发送信息内容");
        return;
    }

    snprintf(sendbuf, sizeof(sendbuf), "msg@%s@%hu@%s", current_peer_ip, current_peer_port, msg);

    if(send_str_to_server(sendbuf) < 0) {
        show_msgbox("提示", "发送失败");
        return;
    }

    chat_append_send_msg(msg);
    lv_textarea_set_text(ta_msg, "");
}

// 选择表情包按钮事件
static void emoji_item_event_cb(lv_event_t * e)
{
    const char * path = (const char *)lv_event_get_user_data(e);
    if(path == NULL) return;

    strncpy(current_emoji_path, path, sizeof(current_emoji_path) - 1);
    current_emoji_path[sizeof(current_emoji_path) - 1] = '\0';

    show_msgbox("提示", current_emoji_path);
}

// 发送表情包按钮事件
static void emoji_btn_event_cb(lv_event_t * e)
{
    int fd;
    int ret;
    long filesize;
    char sendbuf[1024] = {0};

    (void)e;
    hide_keyboard();

    if(current_peer_ip[0] == '\0' || current_peer_port == 0) {
        show_msgbox("提示", "请先选择好友");
        return;
    }

    if(strlen(current_emoji_path) == 0) {
        show_msgbox("提示", "请选择表情包");
        return;
    }

    fd = open(current_emoji_path, O_RDONLY);
    if(fd == -1) {
        perror("open emoji failed");
        show_msgbox("提示", "表情包打开失败");
        return;
    }

    {
        struct stat mystat;
        if(stat(current_emoji_path, &mystat) == -1) {
            close(fd);
            show_msgbox("提示", "获取表情包数据大小失败");
            return;
        }
        filesize = mystat.st_size;
    }

    snprintf(sendbuf, sizeof(sendbuf), "emoji@%s@%hu@%ld", current_peer_ip, current_peer_port, filesize);

    ret = send_str_to_server(sendbuf);
    if(ret < 0) {
        close(fd);
        show_msgbox("提示", "表情包协议失败");
        return;
    }

    while(1) {
        char filebuf[1024];

        ret = read(fd, filebuf, sizeof(filebuf));
        if(ret == -1) {
            show_msgbox("提示", "读取表情包失败");
            close(fd);
            return;
        }
        if(ret == 0) break;

        if(send_n(tcpsock, filebuf, ret) < 0) {
            close(fd);
            show_msgbox("提示", "发送表情包失败");
            return;
        }
    }

    close(fd);
    chat_append_send_msg("[发送表情包]");
}

// 发送文件按钮事件
static void file_btn_event_cb(lv_event_t * e)
{
    int fd;
    int ret;
    long filesize;
    char sendbuf[1024] = {0};
    char filebuf[1024];
    struct stat mystat;
    char * filename = NULL;
    char msgbuf[256] = {0};

    (void)e;
    hide_keyboard();

    {
        const char * filepath = lv_textarea_get_text(ta_file);

        if(current_peer_ip[0] == '\0' || current_peer_port == 0) {
            show_msgbox("提示", "请先选择好友");
            return;
        }

        if(filepath == NULL || strlen(filepath) == 0) {
            show_msgbox("提示", "请输入文件路径");
            return;
        }

        fd = open(filepath, O_RDONLY);
        if(fd == -1) {
            perror("open file failed");
            show_msgbox("提示", "文件打开失败");
            return;
        }

        if(stat(filepath, &mystat) == -1) {
            perror("stat file failed");
            close(fd);
            show_msgbox("提示", "获取文件大小失败");
            return;
        }

        filesize = mystat.st_size;

        filename = strrchr(filepath, '/');
        if(filename != NULL) {
            filename++;
        } else {
            filename = (char *)filepath;
        }

        snprintf(sendbuf, sizeof(sendbuf), "file@%s@%hu@%s@%ld", current_peer_ip, current_peer_port, filename, filesize);

        ret = send_str_to_server(sendbuf);
        if(ret < 0) {
            close(fd);
            show_msgbox("提示", "发送文件协议失败");
            return;
        }

        while(1) {
            ret = read(fd, filebuf, sizeof(filebuf));
            if(ret < 0) {
                perror("read file failed");
                close(fd);
                show_msgbox("提示", "读取文件失败");
                return;
            }

            if(ret == 0) {
                break;
            }

            if(send_n(tcpsock, filebuf, ret) < 0) {
                close(fd);
                show_msgbox("提示", "发送文件内容失败");
                return;
            }
        }

        close(fd);

        snprintf(msgbuf, sizeof(msgbuf), "文件%s", filename);
        chat_append_send_msg(msgbuf);
        lv_textarea_set_text(ta_file, "");
    }
}

// 返回退出聊天界面按钮
static void exit_btn_event_cb(lv_event_t * e)
{
    (void)e;
    hide_keyboard();

    if(chat_timer) {
        lv_timer_del(chat_timer);
        chat_timer = NULL;
    }

    if(tcpsock != -1) {
        shutdown(tcpsock, SHUT_RDWR);
        close(tcpsock);
        tcpsock = -1;
    }

    current_peer_ip[0]    = '\0';
    current_peer_port     = 0;
    current_emoji_path[0] = '\0';
    new_recv_flag         = 0;
    new_friend_flag       = 0;

    memset(recv_from_ip, 0, sizeof(recv_from_ip));
    recv_from_port = 0;

    chat_set_send_widgets_enable(0);
    ui_login_create();
}


// 创建界面

void ui_chat_create(void)
{
    if(style_title == NULL) style_title = my_lv_create_style(FONT_PATH, 24);
    if(style_text == NULL) style_text = my_lv_create_style(FONT_PATH, 18);
    if(style_btntext == NULL) style_btntext = my_lv_create_style(FONT_PATH, 18);

    // 每次进入聊天界面时，先清空当前选择
    current_peer_ip[0] = '\0';
    current_peer_port  = 0;
    recv_from_ip[0]    = '\0';
    recv_from_port     = 0;

    chat_scr = lv_obj_create(NULL);
    lv_obj_set_size(chat_scr, SCREEN_W, SCREEN_H);
    lv_obj_clear_flag(chat_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(chat_scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(chat_scr, bg_event_cb, LV_EVENT_CLICKED, NULL);

    // 左侧：好友列表
    friend_title = lv_label_create(chat_scr);
    lv_label_set_text(friend_title, "好友列表");
    lv_obj_set_pos(friend_title, 25, 10);
    if(style_title) lv_obj_add_style(friend_title, style_title, 0);

    friend_list = lv_list_create(chat_scr);
    lv_obj_set_pos(friend_list, 10, 45);
    lv_obj_set_size(friend_list, 160, 360);
    lv_obj_set_style_pad_left(friend_list, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_row(friend_list, 6, LV_PART_MAIN);
    lv_obj_set_style_border_width(friend_list, 2, 0);

    // 中上：接收显示区
    recv_box = lv_obj_create(chat_scr);
    lv_obj_set_size(recv_box, 350, 350);
    lv_obj_align(recv_box, LV_ALIGN_TOP_MID, -40, 20);
    lv_obj_set_style_border_width(recv_box, 2, 0);
    lv_obj_add_flag(recv_box, LV_OBJ_FLAG_SCROLLABLE);

    recv_text = lv_label_create(recv_box);
    lv_label_set_text(recv_text, "");
    lv_obj_set_width(recv_text, 330);
    lv_label_set_long_mode(recv_text, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(recv_text, 5, 5);
    lv_obj_set_style_text_align(recv_text, LV_TEXT_ALIGN_LEFT, 0);
    if(style_text) lv_obj_add_style(recv_text, style_text, 0);

    // 中下：发送信息区
    ta_msg = lv_textarea_create(chat_scr);
    lv_obj_set_size(ta_msg, 300, 45);
    lv_obj_set_pos(ta_msg, 210, 395);
    lv_textarea_set_placeholder_text(ta_msg, "请输入发送信息");
    lv_obj_add_event_cb(ta_msg, input_ta_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_border_width(ta_msg, 2, 0);
    if(style_text) lv_obj_add_style(ta_msg, style_text, LV_PART_MAIN);

    send_btn = lv_btn_create(chat_scr);
    lv_obj_set_size(send_btn, 70, 45);
    lv_obj_align_to(send_btn, ta_msg, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
    lv_obj_add_event_cb(send_btn, send_btn_event_cb, LV_EVENT_CLICKED, NULL);

    {
        lv_obj_t * send_label = lv_label_create(send_btn);
        lv_label_set_text(send_label, "发送");
        lv_obj_center(send_label);
        if(style_btntext) lv_obj_add_style(send_label, style_btntext, 0);
    }

    // 右上：表情包区域
    emoji_title = lv_label_create(chat_scr);
    lv_label_set_text(emoji_title, "表情包区域");
    lv_obj_set_pos(emoji_title, 600, 10);
    if(style_title) lv_obj_add_style(emoji_title, style_title, 0);

    emoji_area = lv_obj_create(chat_scr);
    lv_obj_set_pos(emoji_area, 550, 45);
    lv_obj_set_size(emoji_area, 220, 150);
    lv_obj_set_style_border_width(emoji_area, 2, 0);
    lv_obj_clear_flag(emoji_area, LV_OBJ_FLAG_SCROLLABLE);

    emoji1_btn = lv_btn_create(emoji_area);
    lv_obj_set_pos(emoji1_btn, 10, 10);
    lv_obj_set_size(emoji1_btn, 50, 50);
    lv_obj_add_event_cb(emoji1_btn, emoji_item_event_cb, LV_EVENT_CLICKED, "./emoji/1.jpg");
    {
        lv_obj_t * img = lv_img_create(emoji1_btn);
        lv_img_set_src(img, "S:emoji/1.jpg");
        lv_obj_center(img);
    }

    emoji2_btn = lv_btn_create(emoji_area);
    lv_obj_set_pos(emoji2_btn, 60, 10);
    lv_obj_set_size(emoji2_btn, 50, 50);
    lv_obj_add_event_cb(emoji2_btn, emoji_item_event_cb, LV_EVENT_CLICKED, "./emoji/2.jpg");
    {
        lv_obj_t * img = lv_img_create(emoji2_btn);
        lv_img_set_src(img, "S:emoji/2.jpg");
        lv_obj_center(img);
    }

    emoji3_btn = lv_btn_create(emoji_area);
    lv_obj_set_pos(emoji3_btn, 110, 10);
    lv_obj_set_size(emoji3_btn, 50, 50);
    lv_obj_add_event_cb(emoji3_btn, emoji_item_event_cb, LV_EVENT_CLICKED, "./emoji/3.jpg");
    {
        lv_obj_t * img = lv_img_create(emoji3_btn);
        lv_img_set_src(img, "S:emoji/3.jpg");
        lv_obj_center(img);
    }

    ta_file = lv_textarea_create(chat_scr);
    lv_obj_set_pos(ta_file, 550, 205);
    lv_obj_set_size(ta_file, 220, 70);
    lv_textarea_set_placeholder_text(ta_file, "请输入文件路径");
    lv_obj_add_event_cb(ta_file, input_ta_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_border_width(ta_file, 2, 0);
    if(style_text) lv_obj_add_style(ta_file, style_text, LV_PART_MAIN);

    // 右下：功能按钮
    emoji_btn = lv_btn_create(chat_scr);
    lv_obj_set_pos(emoji_btn, 560, 295);
    lv_obj_set_size(emoji_btn, 90, 45);
    lv_obj_add_event_cb(emoji_btn, emoji_btn_event_cb, LV_EVENT_CLICKED, NULL);

    {
        lv_obj_t * emoji_btn_label = lv_label_create(emoji_btn);
        lv_label_set_text(emoji_btn_label, "发送表情");
        lv_obj_center(emoji_btn_label);
        if(style_btntext) lv_obj_add_style(emoji_btn_label, style_btntext, 0);
    }

    file_btn = lv_btn_create(chat_scr);
    lv_obj_set_pos(file_btn, 670, 295);
    lv_obj_set_size(file_btn, 90, 45);
    lv_obj_add_event_cb(file_btn, file_btn_event_cb, LV_EVENT_CLICKED, NULL);

    {
        lv_obj_t * file_btn_label = lv_label_create(file_btn);
        lv_label_set_text(file_btn_label, "发送文件");
        lv_obj_center(file_btn_label);
        if(style_btntext) lv_obj_add_style(file_btn_label, style_btntext, 0);
    }

    exit_btn = lv_btn_create(chat_scr);
    lv_obj_set_size(exit_btn, 100, 45);
    lv_obj_align(exit_btn, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_add_event_cb(exit_btn, exit_btn_event_cb, LV_EVENT_CLICKED, NULL);

    {
        lv_obj_t * exit_btn_label = lv_label_create(exit_btn);
        lv_label_set_text(exit_btn_label, "退出聊天");
        lv_obj_center(exit_btn_label);
        if(style_btntext) lv_obj_add_style(exit_btn_label, style_btntext, 0);
    }

    chat_set_send_widgets_enable(0);
    ui_switch_screen(chat_scr);

    // 软键盘
    kb = lv_keyboard_create(chat_scr);
    lv_obj_set_size(kb, 800, 180);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    if(style_btntext) lv_obj_add_style(kb, style_btntext, 0);
    lv_obj_move_foreground(kb);
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);

    // 拼音输入法
    pinyin_ime = lv_ime_pinyin_create(chat_scr);
    lv_obj_add_flag(pinyin_ime, LV_OBJ_FLAG_HIDDEN);
    if(style_text) lv_obj_add_style(pinyin_ime, style_text, 0);
    lv_ime_pinyin_set_keyboard(pinyin_ime, kb);
    lv_ime_pinyin_set_mode(pinyin_ime, LV_IME_PINYIN_MODE_K26);

    cand_panel = lv_ime_pinyin_get_cand_panel(pinyin_ime);
    if(cand_panel) {
        lv_obj_set_size(cand_panel, LV_PCT(100), 50);
        lv_obj_align_to(cand_panel, kb, LV_ALIGN_OUT_TOP_MID, 0, 0);
        lv_obj_add_flag(cand_panel, LV_OBJ_FLAG_HIDDEN);
        if(style_text) lv_obj_add_style(cand_panel, style_text, 0);
        lv_obj_move_foreground(cand_panel);
    }

    // 建立连接
    if(connect_server() == -1) {
        show_msgbox("错误", "连接聊天服务器失败");
        return;
    }

    if(pthread_create(&recv_tid, NULL, recv_servermsg, NULL) == 0) {
        pthread_detach(recv_tid);
    }

    send_str_to_server("online");
    request_friend_list();

    // 初始为空聊天区
    chat_load_current_session_to_ui();

    chat_timer = lv_timer_create(chat_timer_cb, 100, NULL);
}