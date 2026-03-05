/**
 * LVGL Configuration for Pseudo Vinyl MP3 Player
 * ESP32-S3 + GC9A01 (240×240)
 */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/* Color depth: 16-bit (RGB565) to match GC9A01 */
#define LV_COLOR_DEPTH          16
#define LV_COLOR_16_SWAP        1       /* Byte-swap for SPI displays */

/* Memory */
#define LV_MEM_CUSTOM           1       /* Use stdlib malloc/free */
#define LV_MEM_CUSTOM_INCLUDE   <stdlib.h>
#define LV_MEM_CUSTOM_ALLOC     malloc
#define LV_MEM_CUSTOM_FREE      free
#define LV_MEM_CUSTOM_REALLOC   realloc

/* Display resolution */
#define LV_HOR_RES_MAX          240
#define LV_VER_RES_MAX          240

/* Tick - provided by our own timer */
#define LV_TICK_CUSTOM           1
#define LV_TICK_CUSTOM_INCLUDE   "Arduino.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())

/* Drawing */
#define LV_DRAW_COMPLEX          1
#define LV_SHADOW_CACHE_SIZE     0
#define LV_CIRCLE_CACHE_SIZE     4

/* GPU - none */
#define LV_USE_GPU_STM32_DMA2D   0
#define LV_USE_GPU_NXP_PXP       0
#define LV_USE_GPU_NXP_VG_LITE   0

/* Logging */
#define LV_USE_LOG               0

/* Assertions */
#define LV_USE_ASSERT_NULL       1
#define LV_USE_ASSERT_MALLOC     1
#define LV_USE_ASSERT_OBJ        0
#define LV_USE_ASSERT_STYLE      0

/* Fonts */
#define LV_FONT_MONTSERRAT_12   1
#define LV_FONT_MONTSERRAT_14   1
#define LV_FONT_MONTSERRAT_16   1
#define LV_FONT_MONTSERRAT_20   1
#define LV_FONT_DEFAULT          &lv_font_montserrat_14

/* Symbols (used in UI) */
#define LV_USE_FONT_PLACEHOLDER  1

/* Widgets */
#define LV_USE_ARC               1
#define LV_USE_BAR               1
#define LV_USE_BTN               1
#define LV_USE_BTNMATRIX         1
#define LV_USE_CANVAS            0
#define LV_USE_CHECKBOX          0
#define LV_USE_DROPDOWN          0
#define LV_USE_IMG               1
#define LV_USE_LABEL             1
#define LV_USE_LINE              1
#define LV_USE_ROLLER            0
#define LV_USE_SLIDER            1
#define LV_USE_SWITCH            1
#define LV_USE_TEXTAREA          0
#define LV_USE_TABLE             0

/* Extra widgets */
#define LV_USE_ANIMIMG           0
#define LV_USE_CALENDAR          0
#define LV_USE_CHART             0
#define LV_USE_COLORWHEEL        0
#define LV_USE_IMGBTN            0
#define LV_USE_KEYBOARD          0
#define LV_USE_LED               0
#define LV_USE_LIST              1
#define LV_USE_MENU              0
#define LV_USE_METER             0
#define LV_USE_MSGBOX            1
#define LV_USE_SPAN              0
#define LV_USE_SPINBOX           0
#define LV_USE_SPINNER           1
#define LV_USE_TABVIEW           0
#define LV_USE_TILEVIEW          0
#define LV_USE_WIN               0

/* Themes */
#define LV_USE_THEME_DEFAULT     1
#define LV_THEME_DEFAULT_DARK    1
#define LV_USE_THEME_BASIC       0

/* Animations */
#define LV_USE_ANIM              1

#endif /* LV_CONF_H */
