/**
 * @file lv_draw_buf_vg_lite.c
 *
 */

/*********************
 *      INCLUDES
 *********************/

#include "lv_draw_vg_lite.h"

#if LV_USE_DRAW_VG_LITE

#include "../lv_draw_buf_private.h"
#include "lv_vg_lite_utils.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void init_handlers(lv_draw_buf_handlers_t * handlers);

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void lv_draw_buf_vg_lite_init_handlers(void)
{
    init_handlers(lv_draw_buf_get_handlers());
    init_handlers(lv_draw_buf_get_font_handlers());
    init_handlers(lv_draw_buf_get_image_handlers());
}

bool lv_draw_buf_clear_vg_lite(lv_draw_buf_t * draw_buf, const lv_area_t * area)
{
    LV_ASSERT_NULL(draw_buf);

    if(!lv_vg_lite_is_dest_cf_supported(draw_buf->header.cf)) {
        return false;
    }

    lv_area_t clear_area;
    if(area == NULL) {
        clear_area.x1 = 0;
        clear_area.y1 = 0;
        clear_area.x2 = draw_buf->header.w - 1;
        clear_area.y2 = draw_buf->header.h - 1;
        area = &clear_area;
    }

    vg_lite_buffer_t target;
    lv_vg_lite_buffer_from_draw_buf(&target, draw_buf);
    if(!lv_vg_lite_buffer_check(&target, false)) {
        return false;
    }

    vg_lite_rectangle_t rect;
    lv_vg_lite_rect(&rect, area);
    if(vg_lite_clear(&target, &rect, 0) != VG_LITE_SUCCESS) {
        return false;
    }

    /* Keep the CPU fallback contract: the buffer is ready on return. */
    if(vg_lite_finish() != VG_LITE_SUCCESS) {
        return false;
    }

    return true;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static uint32_t width_to_stride(uint32_t w, lv_color_format_t color_format)
{
    if(!lv_vg_lite_is_src_cf_supported(color_format)) {
        uint32_t width_byte = (w * lv_color_format_get_bpp(color_format) + 7) >> 3;
        return LV_ROUND_UP(width_byte, LV_DRAW_BUF_STRIDE_ALIGN);
    }
    return lv_vg_lite_width_to_stride(w, lv_vg_lite_vg_fmt(color_format));
}

static void init_handlers(lv_draw_buf_handlers_t * handlers)
{
    LV_ASSERT_NULL(handlers);
    handlers->width_to_stride_cb = width_to_stride;
}

#endif /*LV_USE_DRAW_VG_LITE*/
