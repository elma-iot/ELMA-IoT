#if APP_HAS_ONBOARD_PANEL
#include "panel_theme.h"

namespace {
lv_theme_t theme;
lv_style_t frame, input, button, selected, disabled, focus, track, accent, knob, cursor, choices;
bool initialized=false;

void apply(lv_theme_t*,lv_obj_t* object) {
    if(lv_obj_check_type(object,&lv_obj_class) || lv_obj_check_type(object,&lv_msgbox_class)) {
        lv_obj_add_style(object,&frame,LV_PART_MAIN);
    } else if(lv_obj_check_type(object,&lv_dropdown_class) || lv_obj_check_type(object,&lv_textarea_class)) {
        lv_obj_add_style(object,&input,LV_PART_MAIN);
        lv_obj_add_style(object,&focus,LV_PART_MAIN|LV_STATE_FOCUSED);
        lv_obj_add_style(object,&disabled,LV_PART_MAIN|LV_STATE_DISABLED);
        if(lv_obj_check_type(object,&lv_dropdown_class))lv_obj_set_style_text_font(object,&lv_font_montserrat_18,LV_PART_MAIN);
        if(lv_obj_check_type(object,&lv_textarea_class))lv_obj_add_style(object,&cursor,LV_PART_CURSOR|LV_STATE_FOCUSED);
    } else if(lv_obj_check_type(object,&lv_dropdownlist_class)) {
        // The popup is a separate object on the top layer, not a child whose
        // colours can safely be inherited from the closed dropdown.
        lv_obj_add_style(object,&input,LV_PART_MAIN);
        lv_obj_add_style(object,&choices,LV_PART_MAIN);
        lv_obj_add_style(object,&selected,LV_PART_SELECTED);
        lv_obj_add_style(object,&selected,LV_PART_SELECTED|LV_STATE_CHECKED);
        lv_obj_add_style(object,&selected,LV_PART_SELECTED|LV_STATE_PRESSED);
        lv_obj_add_style(object,&selected,LV_PART_SELECTED|LV_STATE_CHECKED|LV_STATE_PRESSED);
    } else if(lv_obj_check_type(object,&lv_btn_class)) {
        lv_obj_add_style(object,&button,LV_PART_MAIN);
        lv_obj_add_style(object,&selected,LV_PART_MAIN|LV_STATE_PRESSED);
        lv_obj_add_style(object,&disabled,LV_PART_MAIN|LV_STATE_DISABLED);
    } else if(lv_obj_check_type(object,&lv_keyboard_class) || lv_obj_check_type(object,&lv_btnmatrix_class)) {
        lv_obj_add_style(object,&frame,LV_PART_MAIN);
        lv_obj_add_style(object,&input,LV_PART_ITEMS);
        lv_obj_add_style(object,&selected,LV_PART_ITEMS|LV_STATE_PRESSED);
        lv_obj_add_style(object,&selected,LV_PART_ITEMS|LV_STATE_CHECKED);
    } else if(lv_obj_check_type(object,&lv_bar_class) || lv_obj_check_type(object,&lv_slider_class) || lv_obj_check_type(object,&lv_switch_class)) {
        lv_obj_add_style(object,&track,LV_PART_MAIN);
        lv_obj_add_style(object,&accent,LV_PART_INDICATOR);
        lv_obj_add_style(object,&accent,LV_PART_INDICATOR|LV_STATE_CHECKED);
        lv_obj_add_style(object,&knob,LV_PART_KNOB);
    } else if(lv_obj_check_type(object,&lv_spinner_class) || lv_obj_check_type(object,&lv_arc_class)) {
        lv_obj_add_style(object,&track,LV_PART_MAIN);
        lv_obj_add_style(object,&accent,LV_PART_INDICATOR);
    }
}

void initFrame(lv_style_t& style,uint32_t background,uint32_t foreground) {
    lv_style_init(&style);
    lv_style_set_bg_color(&style,lv_color_hex(background));
    lv_style_set_bg_opa(&style,LV_OPA_COVER);
    lv_style_set_text_color(&style,lv_color_hex(foreground));
    lv_style_set_border_color(&style,lv_color_hex(0x64748b));
    lv_style_set_border_width(&style,1);
    lv_style_set_border_opa(&style,LV_OPA_COVER);
    lv_style_set_radius(&style,8);
    lv_style_set_pad_all(&style,8);
    lv_style_set_pad_row(&style,8);
    lv_style_set_text_line_space(&style,5);
}
}

lv_theme_t* elmaPanelTheme(lv_disp_t* display) {
    auto* base=lv_theme_basic_init(display);
    if(!initialized) {
        initFrame(frame,0x1f2937,0xf3f4f6);
        initFrame(input,0x111827,0xf3f4f6);
        initFrame(button,0xf59e0b,0x111827);
        initFrame(selected,0xfbbf24,0x111827);
        lv_style_init(&choices);
        lv_style_set_text_font(&choices,&lv_font_montserrat_18);
        lv_style_set_text_line_space(&choices,24);
        lv_style_init(&disabled);
        lv_style_set_bg_color(&disabled,lv_color_hex(0x374151));
        lv_style_set_text_color(&disabled,lv_color_hex(0xcbd5e1));
        lv_style_init(&focus);
        lv_style_set_border_color(&focus,lv_color_hex(0xfbbf24));
        lv_style_init(&track);
        lv_style_set_bg_color(&track,lv_color_hex(0x475569));
        lv_style_set_bg_opa(&track,LV_OPA_COVER);
        lv_style_set_radius(&track,LV_RADIUS_CIRCLE);
        lv_style_set_arc_color(&track,lv_color_hex(0x475569));
        lv_style_init(&accent);
        lv_style_set_bg_color(&accent,lv_color_hex(0xf59e0b));
        lv_style_set_bg_opa(&accent,LV_OPA_COVER);
        lv_style_set_arc_color(&accent,lv_color_hex(0xf59e0b));
        lv_style_init(&knob);
        lv_style_set_bg_color(&knob,lv_color_hex(0xf3f4f6));
        lv_style_set_bg_opa(&knob,LV_OPA_COVER);
        lv_style_set_radius(&knob,LV_RADIUS_CIRCLE);
        lv_style_init(&cursor);
        lv_style_set_border_color(&cursor,lv_color_hex(0xfbbf24));
        theme=*base;
        lv_theme_set_parent(&theme,base);
        lv_theme_set_apply_cb(&theme,apply);
        initialized=true;
    }
    theme.disp=display;
    return &theme;
}
lv_obj_t* elmaSdFormatDialog(lv_disp_t* display,lv_obj_t** format) {
    static const char* buttons[]={"Cancel","\n","Erase and format",""};
    auto* box=lv_msgbox_create(nullptr,"Format SD card","Erases ALL files. Back up the card first.",buttons,false);
    lv_obj_set_width(box,lv_disp_get_hor_res(display)-16);
    lv_obj_set_style_bg_color(box,lv_color_hex(0xe2e8f0),0);
    lv_obj_set_style_text_color(box,lv_color_hex(0x0f172a),0);
    lv_obj_set_style_border_color(box,lv_color_hex(0xf59e0b),0);lv_obj_set_style_border_width(box,2,0);
    lv_obj_set_style_pad_all(box,8,0);lv_obj_set_style_pad_row(box,6,0);
    auto* content=lv_msgbox_get_content(box);lv_obj_set_flex_flow(content,LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_opa(content,LV_OPA_TRANSP,0);lv_obj_set_style_border_width(content,0,0);
    lv_obj_set_style_text_color(content,lv_color_hex(0x0f172a),0);lv_obj_set_style_pad_all(content,0,0);
    lv_obj_set_style_text_color(lv_msgbox_get_title(box),lv_color_hex(0x0f172a),0);
    *format=lv_dropdown_create(content);lv_dropdown_set_options(*format,"FAT32");lv_obj_set_size(*format,LV_PCT(100),44);
    auto* matrix=lv_msgbox_get_btns(box);lv_obj_set_size(matrix,LV_PCT(100),96);
    lv_obj_set_style_bg_opa(matrix,LV_OPA_TRANSP,0);lv_obj_set_style_border_width(matrix,0,0);lv_obj_set_style_pad_all(matrix,0,0);
    lv_obj_set_style_bg_color(matrix,lv_color_hex(0x334155),LV_PART_ITEMS);lv_obj_set_style_text_color(matrix,lv_color_hex(0xffffff),LV_PART_ITEMS);
    lv_obj_set_style_pad_row(matrix,6,0);lv_obj_update_layout(box);lv_obj_center(box);return box;
}
#endif
