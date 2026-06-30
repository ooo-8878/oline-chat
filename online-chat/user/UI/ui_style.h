#ifndef _UI_STYLE_H_
#define _UI_STYLE_H_

#include "lvgl/lvgl.h"
#include "lvgl/src/extra/others/ime/lv_ime_pinyin.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>//close
#include <arpa/inet.h>
#include <sys/socket.h>


lv_style_t *my_lv_create_style(const char *fontpath, int fontsize);
void ui_switch_screen(lv_obj_t *new_scr);

#endif