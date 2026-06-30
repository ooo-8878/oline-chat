#include "ui_style.h"
#include <stdlib.h>

lv_style_t *my_lv_create_style(const char *fontpath, int fontsize)
{
    // 这里不能再用 static 全局共享 info
    // 因为多个不同字号样式共用一个静态info，后面继续创建新样式时容易把前面的状态搞乱
    lv_ft_info_t info;

    memset(&info, 0, sizeof(info));
    info.name   = fontpath;
    info.weight = fontsize;
    info.style  = FT_FONT_STYLE_NORMAL;
    info.mem    = NULL;

    if(!lv_ft_font_init(&info))
    {
        LV_LOG_ERROR("font create failed\n");
        return NULL;
    }

    lv_style_t *style = malloc(sizeof(lv_style_t));
    if(style == NULL)
        return NULL;

    lv_style_init(style);
    lv_style_set_text_font(style, info.font);
    lv_style_set_text_align(style, LV_TEXT_ALIGN_CENTER);

    return style;
}

//统一释放内存
void ui_switch_screen(lv_obj_t *new_scr)
{
    lv_obj_t *old_scr = lv_scr_act();//获取当前活跃旧界面

    if(new_scr == NULL)
        return;

    lv_scr_load(new_scr);//加载新界面

    if(old_scr && old_scr != new_scr)
    {
        lv_obj_del_async(old_scr);//异步删除旧界面
    }
}