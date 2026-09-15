/**
 * @file lv_draw_vg_lite.c
 *
 */

/*********************
 *      INCLUDES
 *********************/

#include "lv_draw_vg_lite.h"

#if LV_USE_DRAW_VG_LITE

#include "lv_draw_vg_lite_type.h"
#include "lv_vg_lite_path.h"
#include "lv_vg_lite_utils.h"
#include "lv_vg_lite_decoder.h"
#include "lv_vg_lite_grad.h"
#include "lv_vg_lite_pending.h"
#include "lv_vg_lite_stroke.h"

/*********************
 *      DEFINES
 *********************/

#define VG_LITE_DRAW_UNIT_ID 2

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/

static int32_t draw_dispatch(lv_draw_unit_t * draw_unit, lv_layer_t * layer);

static int32_t draw_evaluate(lv_draw_unit_t * draw_unit, lv_draw_task_t * task);

static int32_t draw_delete(lv_draw_unit_t * draw_unit);

static void draw_event_cb(lv_event_t * e);

/**********************
 *  STATIC VARIABLES
 **********************/

typedef struct {
    lv_draw_vg_lite_task_pred_cb_t pred_cb;
    lv_draw_vg_lite_task_done_cb_t done_cb;
    void * user_data;
} task_sync_hook_t;

static task_sync_hook_t s_task_sync_hooks[LV_DRAW_VG_LITE_TASK_SYNC_MAX];

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
void lv_draw_vg_lite_init(void)
{
#if LV_VG_LITE_USE_GPU_INIT
    extern void gpu_init(void);
    static bool inited = false;
    if(!inited) {
        gpu_init();
        inited = true;
    }
#endif

    lv_vg_lite_dump_info();

    lv_draw_buf_vg_lite_init_handlers();

    lv_draw_vg_lite_unit_t * unit = lv_draw_create_unit(sizeof(lv_draw_vg_lite_unit_t));
    unit->base_unit.dispatch_cb = draw_dispatch;
    unit->base_unit.evaluate_cb = draw_evaluate;
    unit->base_unit.delete_cb = draw_delete;
    unit->base_unit.event_cb = draw_event_cb;
    unit->base_unit.name = "VG_LITE";

    lv_vg_lite_image_dsc_init(unit);
#if LV_USE_VECTOR_GRAPHIC
    unit->grad_ctx = lv_vg_lite_grad_ctx_create(LV_VG_LITE_GRAD_CACHE_CNT, unit);
    lv_vg_lite_stroke_init(unit, LV_VG_LITE_STROKE_CACHE_CNT);
#endif
    lv_vg_lite_path_init(unit);
    lv_vg_lite_decoder_init();
    lv_draw_vg_lite_label_init(unit);
}

void lv_draw_vg_lite_deinit(void)
{
    lv_memzero(s_task_sync_hooks, sizeof(s_task_sync_hooks));
}

bool lv_draw_vg_lite_add_task_sync_cb(lv_draw_vg_lite_task_pred_cb_t pred_cb,
                                      lv_draw_vg_lite_task_done_cb_t done_cb,
                                      void * user_data)
{
    if(pred_cb == NULL) {
        return false;
    }

    for(uint32_t i = 0; i < LV_DRAW_VG_LITE_TASK_SYNC_MAX; i++) {
        if(s_task_sync_hooks[i].pred_cb == NULL) {
            s_task_sync_hooks[i] = (task_sync_hook_t){pred_cb, done_cb, user_data};
            return true;
        }
    }
    return false;
}

void lv_draw_vg_lite_remove_task_sync_cb(lv_draw_vg_lite_task_pred_cb_t pred_cb,
                                         lv_draw_vg_lite_task_done_cb_t done_cb,
                                         void * user_data)
{
    for(uint32_t i = 0; i < LV_DRAW_VG_LITE_TASK_SYNC_MAX; i++) {
        if(s_task_sync_hooks[i].pred_cb == pred_cb &&
           s_task_sync_hooks[i].done_cb == done_cb &&
           s_task_sync_hooks[i].user_data == user_data) {
            s_task_sync_hooks[i] = (task_sync_hook_t){0};
        }
    }
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static bool check_image_is_supported(const lv_draw_image_dsc_t * dsc)
{
    return lv_vg_lite_is_src_cf_supported(dsc->header.cf);
}

static bool check_arc_is_supported(const lv_draw_arc_dsc_t * dsc)
{
    if(dsc->img_src == NULL) {
        return true;
    }

    lv_image_header_t header;
    lv_result_t res = lv_image_decoder_get_info(dsc->img_src, &header);
    if(res != LV_RESULT_OK) {
        LV_LOG_TRACE("get image info failed");
        return false;
    }

    return lv_vg_lite_is_src_cf_supported(header.cf);
}

static void draw_execute(lv_draw_vg_lite_unit_t * u, bool force_sync)
{
    lv_draw_task_t * t = u->task_act;
    lv_layer_t * layer = t->target_layer;

    /* remember draw unit for access to unit's context */
    t->draw_unit = (lv_draw_unit_t *)u;

    lv_vg_lite_buffer_from_draw_buf(&u->target_buffer, layer->draw_buf);

    /* VG-Lite will output premultiplied image, set the flag correspondingly. */
    lv_draw_buf_set_flag(layer->draw_buf, LV_IMAGE_FLAGS_PREMULTIPLIED);

    vg_lite_identity(&u->global_matrix);
    if(layer->buf_area.x1 || layer->buf_area.y1) {
        vg_lite_translate(-layer->buf_area.x1, -layer->buf_area.y1, &u->global_matrix);
    }

#if LV_DRAW_TRANSFORM_USE_MATRIX
    vg_lite_matrix_t layer_matrix;
    lv_vg_lite_matrix(&layer_matrix, &t->matrix);
    lv_vg_lite_matrix_multiply(&u->global_matrix, &layer_matrix);
#endif

    if(vg_lite_query_feature(gcFEATURE_BIT_VG_SCISSOR)) {
        /* Crop out extra pixels drawn due to scaling accuracy issues */
        lv_area_t scissor_area = layer->phy_clip_area;
        lv_area_move(&scissor_area, -layer->buf_area.x1, -layer->buf_area.y1);
        lv_vg_lite_set_scissor_area(u, &scissor_area);
    }

    switch(t->type) {
        case LV_DRAW_TASK_TYPE_LETTER:
            lv_draw_vg_lite_letter(t, t->draw_dsc, &t->area);
            break;
        case LV_DRAW_TASK_TYPE_LABEL:
            lv_draw_vg_lite_label(t, t->draw_dsc, &t->area);
            break;
        case LV_DRAW_TASK_TYPE_FILL:
            lv_draw_vg_lite_fill(t, t->draw_dsc, &t->area);
            break;
        case LV_DRAW_TASK_TYPE_BORDER:
            lv_draw_vg_lite_border(t, t->draw_dsc, &t->area);
            break;
        case LV_DRAW_TASK_TYPE_BOX_SHADOW:
            lv_draw_vg_lite_box_shadow(t, t->draw_dsc, &t->area);
            break;
        case LV_DRAW_TASK_TYPE_IMAGE:
            lv_draw_vg_lite_img(t, t->draw_dsc, &t->area, false);
            break;
        case LV_DRAW_TASK_TYPE_ARC:
            lv_draw_vg_lite_arc(t, t->draw_dsc, &t->area);
            break;
        case LV_DRAW_TASK_TYPE_LINE:
            lv_draw_line_iterate(t, t->draw_dsc, lv_draw_vg_lite_line);
            break;
        case LV_DRAW_TASK_TYPE_LAYER:
            lv_draw_vg_lite_layer(t, t->draw_dsc, &t->area);
            break;
        case LV_DRAW_TASK_TYPE_TRIANGLE:
            lv_draw_vg_lite_triangle(t, t->draw_dsc);
            break;
        case LV_DRAW_TASK_TYPE_MASK_RECTANGLE:
            lv_draw_vg_lite_mask_rect(t, t->draw_dsc, &t->area);
            break;
#if LV_USE_VECTOR_GRAPHIC
        case LV_DRAW_TASK_TYPE_VECTOR:
            lv_draw_vg_lite_vector(t, t->draw_dsc);
            break;
#endif
        default:
            break;
    }

    if(force_sync) {
        /* Do not enter the normal flush-count path. finish submits every
         * pending command, including this task, and waits for GPU completion. */
        lv_vg_lite_finish(u);
    }
    else {
        lv_vg_lite_flush(u);
    }
}

static int32_t draw_dispatch(lv_draw_unit_t * draw_unit, lv_layer_t * layer)
{
    lv_draw_vg_lite_unit_t * u = (lv_draw_vg_lite_unit_t *)draw_unit;
    bool force_sync = false;
    bool matched[LV_DRAW_VG_LITE_TASK_SYNC_MAX] = {false};

    /* Return immediately if it's busy with draw task. */
    if(u->task_act) {
        return 0;
    }

    /* Try to get an ready to draw. */
    lv_draw_task_t * t = lv_draw_get_available_task(layer, NULL, VG_LITE_DRAW_UNIT_ID);

    /* Return 0 is no selection, some tasks can be supported by other units. */
    if(!t || t->preferred_draw_unit_id != VG_LITE_DRAW_UNIT_ID) {
        lv_vg_lite_finish(u);
        return LV_DRAW_UNIT_IDLE;
    }

    /* Return if target buffer format is not supported. */
    if(!lv_vg_lite_is_dest_cf_supported(layer->color_format)) {
        return LV_DRAW_UNIT_IDLE;
    }

    void * buf = lv_draw_layer_alloc_buf(layer);
    if(!buf) {
        return LV_DRAW_UNIT_IDLE;
    }

    t->state = LV_DRAW_TASK_STATE_IN_PROGRESS;
    u->task_act = t;

    for(uint32_t i = 0; i < LV_DRAW_VG_LITE_TASK_SYNC_MAX; i++) {
        if(s_task_sync_hooks[i].pred_cb &&
           s_task_sync_hooks[i].pred_cb(t, s_task_sync_hooks[i].user_data)) {
            matched[i] = true;
            force_sync = true;
        }
    }
    draw_execute(u, force_sync);

    u->task_act->state = LV_DRAW_TASK_STATE_FINISHED;
    u->task_act = NULL;

    /* draw_execute() has already waited for the GPU on the force-sync path. */
    if(force_sync) {
        for(uint32_t i = 0; i < LV_DRAW_VG_LITE_TASK_SYNC_MAX; i++) {
            if(matched[i] && s_task_sync_hooks[i].done_cb) {
                s_task_sync_hooks[i].done_cb(t, s_task_sync_hooks[i].user_data);
            }
        }
    }

    /*The draw unit is free now. Request a new dispatching as it can get a new task*/
    lv_draw_dispatch_request();

    return 1;
}

static int32_t draw_evaluate(lv_draw_unit_t * draw_unit, lv_draw_task_t * task)
{
    LV_UNUSED(draw_unit);

    /* Return if target buffer format is not supported. */
    const lv_draw_dsc_base_t * base_dsc = task->draw_dsc;
    if(!lv_vg_lite_is_dest_cf_supported(base_dsc->layer->color_format)) {
        return -1;
    }

    switch(task->type) {
        case LV_DRAW_TASK_TYPE_LETTER:
        case LV_DRAW_TASK_TYPE_LABEL:
        case LV_DRAW_TASK_TYPE_FILL:
        case LV_DRAW_TASK_TYPE_BORDER:
#if LV_VG_LITE_USE_BOX_SHADOW
        case LV_DRAW_TASK_TYPE_BOX_SHADOW:
#endif
#ifdef CONFIG_GPU_VGLITE_GC265
            break;

        case LV_DRAW_TASK_TYPE_LAYER: {
                const lv_draw_image_dsc_t * draw_dsc = task->draw_dsc;
                /* bitmap_mask_src is applied on-GPU inside lv_draw_vg_lite_layer (DST_IN) */
                switch(draw_dsc->blend_mode) {
                    case LV_BLEND_MODE_NORMAL:
                    case LV_BLEND_MODE_ADDITIVE:
                    case LV_BLEND_MODE_SUBTRACTIVE:
                    case LV_BLEND_MODE_MULTIPLY:
                        break;
                    default:
                        // if(draw_dsc->blend_mode > LV_BLEND_MODE_DIFFERENCE)
                        // LV_LOG_WARN("Unsupported blend mode (%d) in LV_DRAW_TASK_TYPE_LAYER, fallback to SW",
                        //             (int)draw_dsc->blend_mode);
                        return 0;
                }
            }
            break;
#else
        case LV_DRAW_TASK_TYPE_LAYER:
#endif
        case LV_DRAW_TASK_TYPE_LINE:
        case LV_DRAW_TASK_TYPE_TRIANGLE:
        case LV_DRAW_TASK_TYPE_MASK_RECTANGLE:

#if LV_USE_VECTOR_GRAPHIC
        case LV_DRAW_TASK_TYPE_VECTOR:
#endif
            break;

        case LV_DRAW_TASK_TYPE_ARC: {
                if(!check_arc_is_supported(task->draw_dsc)) {
                    return 0;
                }
            }
            break;

        case LV_DRAW_TASK_TYPE_IMAGE: {
                if(!check_image_is_supported(task->draw_dsc)) {
                    return 0;
                }
            }
            break;

        default:
            /*The draw unit is not able to draw this task. */
            return 0;
    }

    if(task->preference_score > 80) {
        /* The draw unit is able to draw this task. */
        task->preference_score = 80;
        task->preferred_draw_unit_id = VG_LITE_DRAW_UNIT_ID;
    }

    return 1;
}

static int32_t draw_delete(lv_draw_unit_t * draw_unit)
{
    lv_draw_vg_lite_unit_t * unit = (lv_draw_vg_lite_unit_t *)draw_unit;

    lv_vg_lite_image_dsc_deinit(unit);
#if LV_USE_VECTOR_GRAPHIC
    lv_vg_lite_grad_ctx_delete(unit->grad_ctx);
    lv_vg_lite_stroke_deinit(unit);
#endif
    lv_vg_lite_path_deinit(unit);
    lv_vg_lite_decoder_deinit();
    lv_draw_vg_lite_label_deinit(unit);
    return 1;
}

static void draw_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);

    switch(code) {
        case LV_EVENT_CANCEL: {
#if LV_USE_VECTOR_GRAPHIC
                /**
                 * Because VG-Lite will deinitialize the context (including the GPU independent heap)
                 * before the GPU goes to sleep, it is necessary to first discard and dereference
                 * all caches that depend on the independent heap.
                 */
                lv_draw_vg_lite_unit_t * unit = lv_event_get_current_target(e);
                lv_cache_drop_all(lv_vg_lite_grad_ctx_get_cache(unit->grad_ctx), NULL);
                lv_cache_drop_all(unit->stroke_cache, NULL);
                LV_LOG_INFO("dropt all cache");
#endif
            }
            break;
        case LV_EVENT_FOCUSED:
            lv_vg_lite_set_dump_param_enable(true);
            break;
        case LV_EVENT_DEFOCUSED:
            lv_vg_lite_set_dump_param_enable(false);
            break;
        case LV_EVENT_HIT_TEST:
            lv_vg_lite_dump_info();
            break;
#if LV_USE_TXT_BATCH_RENDER
        case LV_EVENT_ADD_CHAR_PATH:
            {
                lv_vg_lite_add_char_path(lv_event_get_param(e));
                break;
            }
        case LV_EVENT_CHECK_PATH_CAPA:
            {
                lv_vg_lite_check_path_capa(lv_event_get_param(e));
                break;
            }
        case LV_EVENT_DELETE_UNIT_PATH:
            {
                lv_vg_lite_delete_draw_unit_path(lv_event_get_param(e));
                break;
            }
        case LV_EVENT_DELETE_PATH_MNG:
            {
                lv_vg_lite_delete_path_mng(lv_event_get_param(e));
                break;
            }
        case LV_EVENT_BUILD_PATH:
            {
                lv_vg_lite_build_draw_unit_path(lv_event_get_param(e));
                break;
            }
        case LV_EVENT_DRAW_BUILD_PATH:
            {
                lv_vg_lite_draw_unit_path(lv_event_get_param(e));
                break;
            }
        case LV_EVENT_SET_SCISSOR_AREA:
            {
                lv_vg_lite_event_set_scissor_area(lv_event_get_param(e));
                break;
            }
        case LV_EVENT_PATH_CMD:
            {
                lv_vg_lite_path_cmd_handle(lv_event_get_param(e));
                break;
            }
#endif
        default:
            break;
    }
}

#endif /*LV_USE_DRAW_VG_LITE*/
