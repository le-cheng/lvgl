/**
 * @file lv_label_private.h
 *
 */

#ifndef LV_LABEL_PRIVATE_H
#define LV_LABEL_PRIVATE_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include "../../draw/lv_draw_label_private.h"
#include "../../core/lv_obj_private.h"
#include "lv_label.h"

#if LV_USE_LABEL != 0

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

#if LV_USE_TXT_BATCH_RENDER
typedef struct _lv_label_line_t {
    uint32_t char_cnt;              /**< character count in this line */
    uint32_t byte_cnt;              /**< byte count in this line, include marker */
    uint32_t pixel_w;               /**< pixel width in this line */
} lv_label_line_t;

 typedef struct _lv_label_line_info_t {
    uint32_t total_char_cnt;        /**< total character count in this label */
    uint32_t line_cnt;              /**< line count in this label */
    uint32_t line_cap;              /**< how much line info in the following *line */
    lv_label_line_t *line;         /**< evety line info in this label */
} lv_label_line_info_t;

typedef struct _lv_label_txt_layout_info_t {
    void *font;
    int32_t letter_space;
    int32_t line_space;
    lv_text_decor_t decor;
    lv_text_align_t align;
    int32_t width;
    /* text_flags: lv_label_mark_need_refr_text has been done if expand and recolor changed */
}lv_label_txt_layout_info_t;
#endif

struct _lv_label_t {
    lv_obj_t obj;
    char * text;
#if LV_USE_TRANSLATION
    char * translation_tag;
#endif /*LV_USE_TRANSLATION*/
    char dot[LV_LABEL_DOT_NUM + 1]; /**< Bytes that have been replaced with dots */
    uint32_t dot_begin;  /**< Offset where bytes have been replaced with dots */

#if LV_LABEL_LONG_TXT_HINT
    lv_draw_label_hint_t hint;
#endif

#if LV_LABEL_TEXT_SELECTION
    uint32_t sel_start;
    uint32_t sel_end;
#endif

    lv_point_t size_cache;              /**< Text size cache */
    lv_point_t offset;                  /**< Text draw position offset */
    lv_label_long_mode_t long_mode : 4; /**< Determine what to do with the long texts */
    uint8_t static_txt : 1;             /**< Flag to indicate the text is static */
    uint8_t recolor : 1;                /**< Enable in-line letter re-coloring*/
    uint8_t expand : 1;                 /**< Ignore real width (used by the library with LV_LABEL_LONG_MODE_SCROLL) */
    uint8_t invalid_size_cache : 1;     /**< 1: Recalculate size and update cache */
    uint8_t need_refr_text : 1;         /**< 1: Refresh text after layout update completion */

    lv_point_t text_size;
#if LV_USE_TXT_BATCH_RENDER
    uint8_t enable_batch_render : 1;
    uint8_t first_draw : 1;
    uint8_t is_outline_font : 1;
    uint8_t is_layout_dots : 1;
    lv_label_line_info_t line_info;
    lv_draw_unit_path_manage path_mng;
    lv_area_t path_mng_area;
    lv_ll_t draw_unit_path;
    lv_draw_unit_t* draw_unit;
    lv_label_txt_layout_info_t  txt_layout_info;
#endif
};


/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**********************
 *      MACROS
 **********************/

#endif /* LV_USE_LABEL != 0 */

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*LV_LABEL_PRIVATE_H*/
