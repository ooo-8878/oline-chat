#ifndef _UI_CHAT_H_
#define _UI_CHAT_H_
#include "lvgl/lvgl.h"
#include "lvgl/src/extra/others/ime/lv_ime_pinyin.h"


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

void ui_chat_create(void);
void chat_append_recv_msg(const char *msg);
void chat_append_send_msg(const char *msg);
void chat_update_friend_list(const char *list_str);

#endif