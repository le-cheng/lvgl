/**
 * @file lv_draw_vg_lite_layer.c
 *
 */

/*********************
 *      INCLUDES
 *********************/

#include "lv_draw_vg_lite.h"

#if LV_USE_DRAW_VG_LITE

#include "lv_draw_vg_lite_type.h"
#include "lv_vg_lite_utils.h"
#include "../../misc/lv_area_private.h"
#include "../lv_image_decoder_private.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/

static bool apply_bitmap_mask_dst_in(lv_draw_unit_t * u,lv_layer_t * layer,
                                     const lv_draw_image_dsc_t * draw_dsc);

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
void lv_draw_vg_lite_layer(lv_draw_task_t * t, const lv_draw_image_dsc_t * draw_dsc,
                           const lv_area_t * coords)
{
    lv_layer_t * layer = (lv_layer_t *)draw_dsc->src;
    lv_draw_vg_lite_unit_t * u = (lv_draw_vg_lite_unit_t *)t->draw_unit;

    /*It can happen that nothing was draw on a layer and therefore its buffer is not allocated.
     *In this case just return. */
    if(layer->draw_buf == NULL)
        return;

    LV_PROFILER_DRAW_BEGIN;

    /* The GPU output should already be premultiplied RGB */
    if(!lv_draw_buf_has_flag(layer->draw_buf, LV_IMAGE_FLAGS_PREMULTIPLIED)) {
        LV_LOG_WARN("Non-premultiplied layer buffer for GPU to draw.");
    }

    if(draw_dsc->bitmap_mask_src != NULL) {
        apply_bitmap_mask_dst_in(t->draw_unit, layer, draw_dsc);
        /* If mask is invalid,draw directly.
        if(!visible) {
            LV_PROFILER_DRAW_END;
            return;
        }
        */
    }

    lv_draw_image_dsc_t new_draw_dsc = *draw_dsc;
    new_draw_dsc.src = layer->draw_buf;
    /* mask has been baked into layer->draw_buf above; clear so downstream
     * paths do not try to reapply it */
    new_draw_dsc.bitmap_mask_src = NULL;
    lv_draw_vg_lite_img(t, &new_draw_dsc, coords, true);

    /* Wait for the GPU drawing to complete here,
     * otherwise it may cause the drawing to fail. */
    lv_vg_lite_finish(u);

    LV_PROFILER_DRAW_END;
}

#if LV_DRAW_USE_SCROLL_SNAPSHOT
bool lv_draw_vg_lite_apply_bitmap_mask_dst_in(lv_draw_unit_t * u, lv_layer_t * layer,
                                     const lv_draw_image_dsc_t * draw_dsc)
{
    return apply_bitmap_mask_dst_in(u, layer, draw_dsc);
}
#endif

/**********************
 *   STATIC FUNCTIONS
 **********************/

static bool apply_bitmap_mask_dst_in(lv_draw_unit_t * u, lv_layer_t * layer,
                                     const lv_draw_image_dsc_t * draw_dsc)
{
    LV_PROFILER_DRAW_BEGIN;

    lv_image_decoder_dsc_t mask_decoder;
    lv_result_t res = lv_image_decoder_open(&mask_decoder, draw_dsc->bitmap_mask_src, NULL);
    if(res != LV_RESULT_OK || mask_decoder.decoded == NULL) {
        if(res == LV_RESULT_OK) lv_image_decoder_close(&mask_decoder);
        LV_LOG_WARN("Could not open bitmap_mask_src; mask not applied");
        LV_PROFILER_DRAW_END;
        return true;
    }

    const lv_draw_buf_t * mask_db = mask_decoder.decoded;
    if(mask_db->header.cf != LV_COLOR_FORMAT_A8 &&
       mask_db->header.cf != LV_COLOR_FORMAT_L8) {
        LV_LOG_WARN("bitmap_mask_src must be A8/L8 (got cf=%d); mask not applied",
                    (int)mask_db->header.cf);
        lv_image_decoder_close(&mask_decoder);
        LV_PROFILER_DRAW_END;
        return true;
    }

    /* Center the mask inside the full image_area, then clip to this strip */
    lv_area_t image_area = draw_dsc->image_area;
    lv_area_t mask_area;
    lv_area_set(&mask_area, 0, 0,
                mask_db->header.w - 1,
                mask_db->header.h - 1);
    lv_area_align(&image_area, &mask_area, LV_ALIGN_CENTER, 0, 0);

    const lv_area_t layer_area = layer->buf_area;
    lv_area_t tmp;
    if(!lv_area_intersect(&tmp, &mask_area, &layer_area)) {
        lv_image_decoder_close(&mask_decoder);
        LV_PROFILER_DRAW_END;
        return false;
    }

    const uint32_t mask_w = mask_db->header.w;
    const uint32_t mask_h = mask_db->header.h;
    const uint32_t a8_stride = lv_vg_lite_width_to_stride(mask_w, VG_LITE_A8);
    const void * mask_ptr = mask_db->data;
    uint8_t * mask_copy = NULL;

    if(a8_stride != mask_db->header.stride) {
        LV_LOG_INFO("A8 mask stride mismatch: decoder=%u, vg_lite=%u; using scratch buffer",
                    (unsigned)mask_db->header.stride, (unsigned)a8_stride);
        mask_copy = lv_malloc(a8_stride * mask_h);
        if(mask_copy == NULL) {
            LV_LOG_WARN("Failed to allocate A8 mask scratch buffer");
            lv_image_decoder_close(&mask_decoder);
            LV_PROFILER_DRAW_END;
            return true;
        }
        const uint8_t * src = mask_db->data;
        for(uint32_t row = 0; row < mask_h; row++) {
            lv_memcpy(mask_copy + row * a8_stride,
                      src + row * mask_db->header.stride,
                      mask_w);
        }
        mask_ptr = mask_copy;
    }

    lv_draw_buf_flush_cache(mask_db, NULL);

    vg_lite_buffer_t layer_target;
    lv_vg_lite_buffer_from_draw_buf(&layer_target, layer->draw_buf);

    vg_lite_buffer_t mask_buf;
    lv_vg_lite_buffer_init(&mask_buf, mask_ptr,
                           (int32_t)mask_w, (int32_t)mask_h,
                           (uint32_t)a8_stride, VG_LITE_A8, false);

    vg_lite_matrix_t m;
    vg_lite_identity(&m);
/*
    When the layer is part area of the whole obj, if mask rect is the whole mask area, blit will overflow the target buffer.
    vg_lite_translate((vg_lite_float_t)(mask_area.x1 - layer_area.x1),
                      (vg_lite_float_t)(mask_area.y1 - layer_area.y1),
                      &m);
    vg_lite_rectangle_t rect = {
        .x = 0,
        .y = 0,
        .width = (int32_t)mask_w,
        .height = (int32_t)mask_h,
    };
*/
    /* So we use the crop area and translate. */
    vg_lite_translate((vg_lite_float_t)(tmp.x1 - layer_area.x1),
                      (vg_lite_float_t)(tmp.y1 - layer_area.y1),
                      &m);

    vg_lite_rectangle_t rect = {
        .x = tmp.x1 - mask_area.x1,
        .y = tmp.y1 - mask_area.y1,
        .width = (int32_t)(tmp.x2 - tmp.x1 + 1),
        .height = (int32_t)(tmp.y2 - tmp.y1 + 1),
    };
    vg_lite_set_scissor(0, 0, layer_target.width, layer_target.height);
    lv_vg_lite_blit_rect(&layer_target, &mask_buf, &rect, &m,
                         VG_LITE_BLEND_DST_IN, 0xFFFFFFFF, VG_LITE_FILTER_POINT);
    lv_area_t * scissor_area = &(((lv_draw_vg_lite_unit_t *)u)->current_scissor_area);
    vg_lite_set_scissor(scissor_area->x1, scissor_area->y1, scissor_area->x2 + 1, scissor_area->y2 + 1);

    if(mask_copy) lv_free(mask_copy);
    lv_image_decoder_close(&mask_decoder);
    LV_PROFILER_DRAW_END;
    return true;
}

#endif /*LV_USE_DRAW_VG_LITE*/
