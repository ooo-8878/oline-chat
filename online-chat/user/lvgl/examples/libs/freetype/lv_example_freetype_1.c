#include "../../lv_examples.h"
#if LV_BUILD_EXAMPLES
#if LV_USE_FREETYPE

/**
 * Load a font with FreeType
 */
void lv_example_freetype_1(void)
{
    //创建字体
    static lv_ft_info_t info;
    /*FreeType uses C standard file system, so no driver letter is required.*/
    info.name = "/font/simkai.ttf";  //我从windows里面把楷体字库下载到开发板
    info.weight = 24;  //字体大小
    info.style = FT_FONT_STYLE_NORMAL;
    info.mem = NULL;
    if(!lv_ft_font_init(&info)) {
        LV_LOG_ERROR("create failed.");
    }

    //创建样式,样式应用刚才的楷体
    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_text_font(&style, info.font);
    lv_style_set_text_align(&style, LV_TEXT_ALIGN_CENTER);

    //创建标签使用刚才的样式
    lv_obj_t * label = lv_label_create(lv_scr_act());
    //标签使用样式
    lv_obj_add_style(label, &style, 0);
    //设置标签上的文字
    lv_label_set_text(label, "我是秦始皇!");
    //居中显示标签
    lv_obj_center(label);
}
#else

void lv_example_freetype_1(void)
{
    /*TODO
     *fallback for online examples*/

    lv_obj_t * label = lv_label_create(lv_scr_act());
    lv_label_set_text(label, "FreeType is not installed");
    lv_obj_center(label);
}

#endif
#endif
