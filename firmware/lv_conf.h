#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0
#define LV_MEM_SIZE (28U * 1024U)
#define LV_DISP_DEF_REFR_PERIOD 25
#define LV_INDEV_DEF_READ_PERIOD 20
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_USE_USER_DATA 1
#define LV_USE_THEME_DEFAULT 0
#define LV_USE_THEME_BASIC 0
#define LV_USE_THEME_MONO 0
#define LV_USE_FLEX 0
#define LV_USE_GRID 0
#define LV_USE_IMG 1
#define LV_USE_LABEL 1
#define LV_USE_ARC 0
#define LV_USE_BAR 1
#define LV_USE_BTN 0
#define LV_USE_BTNMATRIX 1
#define LV_USE_CANVAS 1
#define LV_USE_QRCODE 1
#define LV_USE_CHECKBOX 0
#define LV_USE_DROPDOWN 0
#define LV_USE_LINE 0
#define LV_USE_ROLLER 0
#define LV_USE_SLIDER 1
#define LV_USE_SWITCH 1
#define LV_USE_TEXTAREA 1
#define LV_USE_TABLE 0
#define LV_USE_ANIMIMG 0
#define LV_USE_CALENDAR 0
#define LV_USE_CHART 1
#define LV_USE_COLORWHEEL 0
#define LV_USE_IMGBTN 0
#define LV_USE_KEYBOARD 1
#define LV_USE_LED 0
#define LV_USE_LIST 0
#define LV_USE_MENU 0
#define LV_USE_METER 0
#define LV_USE_MSGBOX 0
#define LV_USE_SPAN 0
#define LV_USE_SPINBOX 0
#define LV_USE_SPINNER 0
#define LV_USE_TABVIEW 0
#define LV_USE_TILEVIEW 0
#define LV_USE_WIN 0
#define LV_BUILD_EXAMPLES 0

#if defined(H1_ARM_FIRMWARE)
#include <stdint.h>
#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE "lv_conf.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (firmwareUiMilliseconds())
#ifdef __cplusplus
extern "C" {
#endif
uint32_t firmwareUiMilliseconds(void);
void firmwareUiFault(void);
#ifdef __cplusplus
}
#endif
#define LV_ASSERT_HANDLER firmwareUiFault();
#endif

#endif

