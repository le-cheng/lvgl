/**
 * @file lv_draw_vg_lite_label.c
 *
 */

/*********************
 *      INCLUDES
 *********************/

#include "lv_draw_vg_lite.h"

#if LV_USE_DRAW_VG_LITE

#include "lv_draw_vg_lite_type.h"
#include "lv_vg_lite_utils.h"
#include "lv_vg_lite_path.h"
#include "lv_vg_lite_pending.h"
#include "lv_vg_lite_bitmap_font_cache.h"
#include "../../misc/cache/lv_cache_entry_private.h"
#include "../../misc/lv_area_private.h"
#include "../../libs/freetype/lv_freetype_private.h"
#include "../lv_draw_label_private.h"
#include "../lv_draw_image_private.h"
#include "../../core/lv_global.h"

/*********************
 *      DEFINES
 *********************/

#define PATH_DATA_COORD_FORMAT VG_LITE_S16
#if LV_USE_TXT_BATCH_RENDER
#define UNIT_PATH_MEM_SIZE_SF 2
#endif

#if LV_VG_LITE_FLUSH_MAX_COUNT > 0
    #define PATH_FLUSH_COUNT_MAX 0
#else
    /* When using IDLE Flush mode, reduce the number of flushes */
    #define PATH_FLUSH_COUNT_MAX 8
#endif

#define FT_F26DOT6_SHIFT 6

/** After converting the font reference size, it is also necessary to scale the 26dot6 data
 * in the path to the real physical size
 */
#define FT_F26DOT6_TO_PATH_SCALE(x) (LV_FREETYPE_F26DOT6_TO_FLOAT(x) / (1 << FT_F26DOT6_SHIFT))

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void draw_letter_cb(lv_draw_task_t * t, lv_draw_glyph_dsc_t * glyph_draw_dsc,
                           lv_draw_fill_dsc_t * fill_draw_dsc, const lv_area_t * fill_area);

static void draw_letter_bitmap(lv_draw_task_t * t, const lv_draw_glyph_dsc_t * dsc, vg_lite_buffer_t * src_buf);

static void bitmap_cache_release_cb(void * entry, void * user_data);

#if LV_USE_FREETYPE
    static void freetype_outline_event_cb(lv_event_t * e);
    static void draw_letter_outline(lv_draw_task_t * t, const lv_draw_glyph_dsc_t * dsc);
    static void outline_iter_cb(void * user_data, uint8_t op_code, const float * data, uint32_t len);
#endif /* LV_USE_FREETYPE */

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void lv_draw_vg_lite_label_init(struct _lv_draw_vg_lite_unit_t * u)
{
    LV_ASSERT_NULL(u);

#if LV_USE_FREETYPE
    /*Set up the freetype outline event*/
    lv_freetype_outline_add_event(freetype_outline_event_cb, LV_EVENT_ALL, u);
#endif /* LV_USE_FREETYPE */

    lv_vg_lite_bitmap_font_cache_init(u, LV_VG_LITE_BITMAP_FONT_CACHE_CNT);
    u->letter_pending = lv_vg_lite_pending_create(sizeof(lv_font_glyph_dsc_t), 8);
    lv_vg_lite_pending_set_free_cb(u->letter_pending, bitmap_cache_release_cb, NULL);
}

void lv_draw_vg_lite_label_deinit(struct _lv_draw_vg_lite_unit_t * u)
{
    LV_ASSERT_NULL(u);
    LV_ASSERT_NULL(u->letter_pending);
    lv_vg_lite_pending_destroy(u->letter_pending);
    u->letter_pending = NULL;

    lv_vg_lite_bitmap_font_cache_deinit(u);
}

void lv_draw_vg_lite_letter(lv_draw_task_t * t, const lv_draw_letter_dsc_t * dsc, const lv_area_t * coords)
{
    if(dsc->opa <= LV_OPA_MIN)
        return;

    LV_PROFILER_DRAW_BEGIN;

    lv_draw_glyph_dsc_t glyph_dsc;
    lv_draw_glyph_dsc_init(&glyph_dsc);
    glyph_dsc.opa = dsc->opa;
    glyph_dsc.bg_coords = NULL;
    glyph_dsc.color = dsc->color;
    glyph_dsc.rotation = dsc->rotation;
    glyph_dsc.pivot = dsc->pivot;

    lv_draw_unit_draw_letter(t, &glyph_dsc, &(lv_point_t) {
        .x = coords->x1, .y = coords->y1
    },
    dsc->font, dsc->unicode, draw_letter_cb);

    if(glyph_dsc._draw_buf) {
        lv_draw_buf_destroy(glyph_dsc._draw_buf);
        glyph_dsc._draw_buf = NULL;
    }

    LV_PROFILER_DRAW_END;
}

void lv_draw_vg_lite_label(lv_draw_task_t * t, const lv_draw_label_dsc_t * dsc,
                           const lv_area_t * coords)
{
    LV_PROFILER_DRAW_BEGIN;
    lv_draw_label_iterate_characters(t, dsc, coords, draw_letter_cb);
    LV_PROFILER_DRAW_END;
}

void lv_vg_lite_log_path(uint8_t* path, uint32_t path_size, vg_lite_format_t PathFormat)
{
    uint8_t *pathOpcode = (uint8_t *)path;
    float *pathData_f = (float *)path;
    int32_t *pathData_s32 = (int32_t *)path;
    int16_t *pathData_s16 = (int16_t *)path;
    int8_t *pathData_s8 = (int8_t *)path;
    uint8_t *pathLast = (uint8_t *)(path + path_size);
    uint8_t opcode;
    int32_t v1,v2,v3,v4,v5,v6;
    float vf1,vf2,vf3,vf4,vf5,vf6;
    char *opcodename;

    LV_LOG_WARN("path size: %lu",path_size);
    while (pathOpcode < pathLast) {
        opcode = *pathOpcode;
        if(VG_LITE_S8 == PathFormat) {
            pathData_s8++;
        } else if(VG_LITE_S16 == PathFormat) {
            pathData_s16++;
        } else if(VG_LITE_S32 == PathFormat) {
            pathData_s32++;
        } else if(VG_LITE_FP32 == PathFormat) {
            pathData_f++;
        } else {
            LV_LOG_ERROR("unknown pathformat\r\n");
            return;
        }
        switch(opcode)
        {
            case 0:
                LV_LOG_WARN("e");
                break;
            case 1:
                LV_LOG_WARN("cl");
                break;
            case 2:  // M
            case 4:  // L
                if (2 == opcode) {
                    opcodename = "m";
                } else {
                    opcodename = "l";
                }
                if(VG_LITE_S8 == PathFormat) {
                    v1 = *pathData_s8++;
                    v2 = *pathData_s8++;
                    LV_LOG_WARN("%s %ld %ld ", opcodename, v1, v2);
                } else if(VG_LITE_S16 == PathFormat) {
                    v1 = *pathData_s16++;
                    v2 = *pathData_s16++;
                    LV_LOG_WARN("%s %ld %ld ", opcodename, v1, v2);
                } else if(VG_LITE_S32 == PathFormat) {
                    v1 = *pathData_s32++;
                    v2 = *pathData_s32++;
                    LV_LOG_WARN("%s %ld %ld ", opcodename, v1, v2);
                } else if(VG_LITE_FP32 == PathFormat) {
                    vf1 = *pathData_f++;
                    vf2 = *pathData_f++;
                    LV_LOG_WARN("%s %f %f ", opcodename, vf1, vf2);
                }
                break;
            case 6:  // conic
                opcodename = "q";
                if(VG_LITE_S8 == PathFormat) {
                    v1 = *pathData_s8++;
                    v2 = *pathData_s8++;
                    v3 = *pathData_s8++;
                    v4 = *pathData_s8++;
                    LV_LOG_WARN("%s %ld %ld %ld %ld ", opcodename, v1, v2, v3, v4);
                } else if(VG_LITE_S16 == PathFormat) {
                    v1 = *pathData_s16++;
                    v2 = *pathData_s16++;
                    v3 = *pathData_s16++;
                    v4 = *pathData_s16++;
                    LV_LOG_WARN("%s %ld %ld %ld %ld ", opcodename, v1, v2, v3, v4);
                } else if(VG_LITE_S32 == PathFormat) {
                    v1 = *pathData_s32++;
                    v2 = *pathData_s32++;
                    v3 = *pathData_s32++;
                    v4 = *pathData_s32++;
                    LV_LOG_WARN("%s %ld %ld %ld %ld ", opcodename, v1, v2, v3, v4);
                } else if(VG_LITE_FP32 == PathFormat) {
                    vf1 = *pathData_f++;
                    vf2 = *pathData_f++;
                    vf3 = *pathData_f++;
                    vf4 = *pathData_f++;
                    LV_LOG_WARN("%s %f %f %f %f ", opcodename, vf1, vf2, vf3, vf4);
                }
                break;
            case 8:  // cubic
                opcodename = "c";
                if(VG_LITE_S8 == PathFormat) {
                    v1 = *pathData_s8++;
                    v2 = *pathData_s8++;
                    v3 = *pathData_s8++;
                    v4 = *pathData_s8++;
                    v5 = *pathData_s8++;
                    v6 = *pathData_s8++;
                    LV_LOG_WARN("%s %ld %ld %ld %ld %ld %ld ", opcodename, v1, v2, v3, v4, v5, v6);
                } else if(VG_LITE_S16 == PathFormat) {
                    v1 = *pathData_s16++;
                    v2 = *pathData_s16++;
                    v3 = *pathData_s16++;
                    v4 = *pathData_s16++;
                    v5 = *pathData_s16++;
                    v6 = *pathData_s16++;
                    LV_LOG_WARN("%s %ld %ld %ld %ld %ld %ld ", opcodename, v1, v2, v3, v4, v5, v6);
                } else if(VG_LITE_S32 == PathFormat) {
                    v1 = *pathData_s32++;
                    v2 = *pathData_s32++;
                    v3 = *pathData_s32++;
                    v4 = *pathData_s32++;
                    v5 = *pathData_s32++;
                    v6 = *pathData_s32++;
                    LV_LOG_WARN("%s %ld %ld %ld %ld %ld %ld ", opcodename, v1, v2, v3, v4, v5, v6);
                } else if(VG_LITE_FP32 == PathFormat) {
                    vf1 = *pathData_f++;
                    vf2 = *pathData_f++;
                    vf3 = *pathData_f++;
                    vf4 = *pathData_f++;
                    vf5 = *pathData_f++;
                    vf6 = *pathData_f++;
                    LV_LOG_WARN("%s %f %f %f %f %f %f", opcodename, vf1, vf2, vf3, vf4, vf5, vf6);
                }
                break;
            case 0x13:  // SCCWARC
            case 0x15:  // SCWARC
            case 0x17:  // LCCWARC
            case 0x19:  // LCWARC
                if (0x13 == opcode) {
                    opcodename = "sccw";
                } else if (0x15 == opcode) {
                    opcodename = "scw";
                } else if (0x17 == opcode) {
                    opcodename = "lccw";
                } else {
                    opcodename = "lcw";
                }
                if(VG_LITE_S8 == PathFormat) {
                    v1 = *pathData_s8++;
                    v2 = *pathData_s8++;
                    v3 = *pathData_s8++;
                    v4 = *pathData_s8++;
                    v5 = *pathData_s8++;
                    LV_LOG_WARN("%s %ld %ld %ld %ld %ld ", opcodename, v1, v2, v3, v4, v5);
                } else if(VG_LITE_S16 == PathFormat) {
                    v1 = *pathData_s16++;
                    v2 = *pathData_s16++;
                    v3 = *pathData_s16++;
                    v4 = *pathData_s16++;
                    v5 = *pathData_s16++;
                    LV_LOG_WARN("%s %ld %ld %ld %ld %ld ", opcodename, v1, v2, v3, v4, v5);
                } else if(VG_LITE_S32 == PathFormat) {
                    v1 = *pathData_s32++;
                    v2 = *pathData_s32++;
                    v3 = *pathData_s32++;
                    v4 = *pathData_s32++;
                    v5 = *pathData_s32++;
                    LV_LOG_WARN("%s %ld %ld %ld %ld %ld ", opcodename, v1, v2, v3, v4, v5);
                } else if(VG_LITE_FP32 == PathFormat) {
                    vf1 = *pathData_f++;
                    vf2 = *pathData_f++;
                    vf3 = *pathData_f++;
                    vf4 = *pathData_f++;
                    vf5 = *pathData_f++;
                    LV_LOG_WARN("%s %f %f %f %f %f", opcodename, vf1, vf2, vf3, vf4, vf5);
                }
                break;
            default:
                LV_LOG_ERROR("unknown opcode");
                return;
                break;
        }
        if(VG_LITE_S8 == PathFormat) {
            pathOpcode = (uint8_t *)pathData_s8;
        } else if(VG_LITE_S16 == PathFormat) {
            pathOpcode = (uint8_t *)pathData_s16;
        } else if(VG_LITE_S32 == PathFormat) {
            pathOpcode = (uint8_t *)pathData_s32;
        } else if(VG_LITE_FP32 == PathFormat) {
            pathOpcode = (uint8_t *)pathData_f;
        }
    }
}

#if LV_USE_TXT_BATCH_RENDER
#define PATH_MAX_SIZE   (32 *1024)

void lv_vg_lite_check_path_capa(void *in)
{
    lv_event_param_check_path_capa *param = (lv_event_param_check_path_capa *)in;
    if (param->path_h > param->display_h) {
        param->ret = false;
        return;
    }
    uint32_t path_size = param->path_mng->total_size;
    if (path_size > PATH_MAX_SIZE) {
        param->ret = false;
    } else {
        param->ret = true;
    }
}

void lv_vg_lite_add_char_path(void *in)
{
    lv_event_param_add_char *param = (lv_event_param_add_char *)in;
    lv_font_glyph_dsc_t * glyph_dsc = param->glyph_dsc;
    lv_point_t* pos = &(param->pos);
    lv_draw_unit_path_manage* path_mng = param->path_mng;
    lv_char_path_node *d;
    vg_lite_path_t * path;
    uint8_t *p_u8, *cur, *end;
    float *pf;
    lv_vg_lite_path_t * outline = (lv_vg_lite_path_t *) lv_font_get_glyph_bitmap(glyph_dsc, NULL);
    if (!outline) {
        param->ret = LV_EVENT_ADD_CHAR_OUTLINE_NULL;
        return;
    }

    path = lv_vg_lite_path_get_path(outline);
    /* outline is PATH_DATA_COORD_FORMAT(S16) type */
    uint8_t outline_format_len = lv_vg_lite_path_get_path_format_len(outline);
    uint32_t param_size = path->path_length * UNIT_PATH_MEM_SIZE_SF;
    uint32_t real_param_size = param_size - sizeof(float);
    if (real_param_size > 0) {
        d = lv_malloc(sizeof(lv_char_path_node) + param_size);
        if (d) {
            float scale = FT_F26DOT6_TO_PATH_SCALE(lv_freetype_outline_get_scale(glyph_dsc->resolved_font));
            d->param = (d + 1);
            d->param_size = real_param_size;          /* ignore opcode end */
            d->next = NULL;

            p_u8 = (uint8_t *)d->param;
            cur = path->path;
            end = cur + path->path_length;
            int16_t *p_src;
            while(cur < end) {
                pf = (((float*)p_u8) + 1);
                p_src = (int16_t *)(cur + outline_format_len);
                *p_u8 = *cur;
                switch(*p_u8){
                    case VLC_OP_MOVE:
                        *pf++ = (((float)(*p_src)) * scale) + pos->x; p_src++;
                        *pf++ = (((float)(*p_src)) * scale) + pos->y; p_src++;
                        break;
                    case VLC_OP_LINE:
                        *pf++ = (((float)(*p_src)) * scale) + pos->x; p_src++;
                        *pf++ = (((float)(*p_src)) * scale) + pos->y; p_src++;
                        break;
                    case VLC_OP_QUAD:
                        *pf++ = (((float)(*p_src)) * scale) + pos->x; p_src++;
                        *pf++ = (((float)(*p_src)) * scale) + pos->y; p_src++;
                        *pf++ = (((float)(*p_src)) * scale) + pos->x; p_src++;
                        *pf++ = (((float)(*p_src)) * scale) + pos->y; p_src++;
                        break;
                    case VLC_OP_CUBIC:
                        *pf++ = (((float)(*p_src)) * scale) + pos->x; p_src++;
                        *pf++ = (((float)(*p_src)) * scale) + pos->y; p_src++;
                        *pf++ = (((float)(*p_src)) * scale) + pos->x; p_src++;
                        *pf++ = (((float)(*p_src)) * scale) + pos->y; p_src++;
                        *pf++ = (((float)(*p_src)) * scale) + pos->x; p_src++;
                        *pf++ = (((float)(*p_src)) * scale) + pos->y; p_src++;
                    case VLC_OP_END:
                        break;
                    default:
                        LV_ASSERT_FORMAT_MSG(false, "unknown op_code: %d", *p_u8);
                        param->ret = LV_EVENT_ADD_CHAR_FAIL;
                        return;
                }
                cur = (uint8_t *)p_src;
                p_u8 = (uint8_t *)pf;
            }
            if(path_mng->head) {
                path_mng->tail->next = d;
            } else {
                path_mng->head = d;
            }
            path_mng->tail = d;
            path_mng->total_size += d->param_size;
            param->ret = LV_EVENT_ADD_CHAR_SUCCESS;
        } else {
            param->ret = LV_EVENT_ADD_CHAR_FAIL;
        }
    } else {
        param->ret = LV_EVENT_ADD_CHAR_SUCCESS;
    }
    lv_font_glyph_release_draw_data(glyph_dsc);
}

#define VG_LITE_DATA(count)         (0x40000000 | count)
#define VG_LITE_RETURN()            (0x70000000)

void lv_vg_lite_build_draw_unit_path(void * in)
{
    lv_event_param_build_path *param = (lv_event_param_build_path *)in;
    uint32_t element_size = sizeof(float);
    lv_draw_unit_path_manage* path_mng = param->path_mng;
    uint32_t path_size = path_mng->total_size;

    /* add end opcode */
    path_size += element_size;

    if (path_size > PATH_MAX_SIZE) {
        param->ret = NULL;
        return;
    }
    uint8_t *PathBuf = NULL, *buf;
    uint32_t real_bytes;
    uint32_t* p32;
    lv_char_path_node *pNode;
    lv_vg_lite_path_build *createdPath = NULL;
    lv_mem_ops_t *mem_op = &(LV_GLOBAL_DEFAULT()->mem_hw_ops_cb);

    real_bytes = (8 + path_size + 7 + 8) & ~7;
    PathBuf = mem_op->malloc_align_cb(64,real_bytes);
    if( NULL == PathBuf) {
        goto ErrorHandler;
    }
    p32 = (uint32_t*)PathBuf;
    /* Initialize command buffer prefix. */
    *p32++ = VG_LITE_DATA((path_size + 7) / 8);
    *p32++ = 0;

    pNode = path_mng->head;
    buf = (uint8_t *)p32;
    while(pNode) {
        lv_memcpy(buf, pNode->param, pNode->param_size);
        buf += pNode->param_size;
        pNode = pNode->next;
    }

    /* add end opcode */
    *buf = VLC_OP_END;

    p32 = (uint32_t*)(PathBuf + real_bytes);
    p32 -= 2;
    /* Initialize command buffer postfix. */
    *p32++ = VG_LITE_RETURN();
    *p32 = 0;

    createdPath = lv_malloc(sizeof(lv_vg_lite_path_build));
    createdPath->path = PathBuf;
    createdPath->path_size = path_size;
    createdPath->upload_size = real_bytes;

    /* GpuMemalign是cachable的区域，需要刷cache，clean path cache for clean cache only one time */
    if (mem_op->clean_cache_cb) mem_op->clean_cache_cb((void *)PathBuf, real_bytes);

    createdPath->stroke_path = NULL;
    createdPath->stroke_path_size = 0;
    createdPath->stroke_upload_size = 0;
    param->ret = createdPath;
    return;
ErrorHandler:
    LV_LOG_WARN("%s error",__func__);
    if(PathBuf) {
        lv_free(PathBuf);
    }
    if(createdPath) {
        lv_free(createdPath);
    }
    param->ret = NULL;
    return;
}

void lv_vg_lite_delete_path_mng(void *in)
{
    lv_draw_unit_path_manage* path_mng = (lv_draw_unit_path_manage*)in;
    lv_char_path_node *head = path_mng->head;
    lv_char_path_node *next;
    while (head) {
        next = head->next;
        lv_free(head);            // Path_List_Node and pcmd and pCmdParam is in the same memory
        head = next;
    }
    lv_memset(path_mng, 0, sizeof(lv_draw_unit_path_manage));
}

void lv_vg_lite_delete_draw_unit_path(void *in)
{
    lv_mem_ops_t *mem_op = &(LV_GLOBAL_DEFAULT()->mem_hw_ops_cb);
    lv_vg_lite_path_build *draw_unit_path = (lv_vg_lite_path_build *)in;
    if (draw_unit_path->path) {
        mem_op->free_align_cb(draw_unit_path->path);
    }
    if (draw_unit_path->stroke_path) {
        mem_op->free_align_cb(draw_unit_path->stroke_path);
    }
    lv_free(draw_unit_path);
}

void lv_vg_lite_add_end_cmd(void *param)
{
    lv_event_param_path *para = (lv_event_param_path *)param;
    lv_char_path_node *node = (lv_char_path_node *)(para->param);
    uint8_t *p = (uint8_t *)node->param;
    p += node->param_size;
    *p = VLC_OP_END;
    node->param_size += sizeof(float);
}

void lv_vg_lite_build_rect_border(void *param)
{
    lv_event_param_path *para_in = (lv_event_param_path *)param;
    lv_event_param_path_rect *para = (lv_event_param_path_rect *)(para_in->param);
    lv_draw_unit_path_manage* path_mng = para->path_mng;
    lv_char_path_node *d;
    uint8_t *data;
    float *fp_data;
    uint32_t param_size = 104;

    d = lv_malloc(sizeof(lv_char_path_node) + param_size);
    if (d) {
        d->param = (uint8_t *)(d + 1);
        d->param_size = param_size;
        d->next = NULL;

        data = (uint8_t *)(d->param);
        fp_data = (float*)data;
        *data = VLC_OP_MOVE; fp_data ++;
        *fp_data ++ = (float)(para->area->x1); *fp_data ++ = (float)(para->area->y1);

        data = (uint8_t *)fp_data;
        *data = VLC_OP_LINE; fp_data ++;
        *fp_data ++ = (float)(para->area->x2); *fp_data ++ = (float)(para->area->y1);

        data = (uint8_t *)fp_data;
        *data = VLC_OP_LINE; fp_data ++;
        *fp_data ++ = (float)(para->area->x2); *fp_data ++ = (float)(para->area->y2);

        data = (uint8_t *)fp_data;
        *data = VLC_OP_LINE; fp_data ++;
        *fp_data ++ = (float)(para->area->x1); *fp_data ++ = (float)(para->area->y2);

        data = (uint8_t *)fp_data;
        *data = VLC_OP_CLOSE; fp_data ++;

        /* inner rect */
        int32_t border_w = 1;
        data = (uint8_t *)fp_data;
        *data = VLC_OP_MOVE; fp_data ++;
        *fp_data ++ = (float)(para->area->x1 + border_w); *fp_data ++ = (float)(para->area->y1 + border_w);

        data = (uint8_t *)fp_data;
        *data = VLC_OP_LINE; fp_data ++;
        *fp_data ++ = (float)(para->area->x1 + border_w); *fp_data ++ = (float)(para->area->y2 - border_w);

        data = (uint8_t *)fp_data;
        *data = VLC_OP_LINE; fp_data ++;
        *fp_data ++ = (float)(para->area->x2 - border_w); *fp_data ++ = (float)(para->area->y2 - border_w);

        data = (uint8_t *)fp_data;
        *data = VLC_OP_LINE; fp_data ++;
        *fp_data ++ = (float)(para->area->x2 - border_w); *fp_data ++ = (float)(para->area->y1 + border_w);

        data = (uint8_t *)fp_data;
        *data = VLC_OP_CLOSE; fp_data ++;

        if(path_mng->head) {
            path_mng->tail->next = d;
        } else {
            path_mng->head = d;
        }
        path_mng->tail = d;
        path_mng->total_size += d->param_size;
        para_in->ret = true;
    } else {
        para_in->ret = false;
    }
}

void lv_vg_lite_build_rect(void *param)
{
    lv_event_param_path *para_in = (lv_event_param_path *)param;
    lv_event_param_path_rect *para = (lv_event_param_path_rect *)(para_in->param);
    lv_draw_unit_path_manage* path_mng = para->path_mng;
    lv_char_path_node *d;
    uint8_t *data;
    float *fp_data;
    uint32_t param_size = 52;

    d = lv_malloc(sizeof(lv_char_path_node) + param_size);
    if (d) {
        d->param = (uint8_t *)(d + 1);
        d->param_size = param_size;
        d->next = NULL;

        data = (uint8_t *)(d->param);
        fp_data = (float*)data;
        *data = VLC_OP_MOVE; fp_data ++;
        *fp_data ++ = (float)(para->area->x1); *fp_data ++ = (float)(para->area->y1);

        data = (uint8_t *)fp_data;
        *data = VLC_OP_LINE; fp_data ++;
        *fp_data ++ = (float)(para->area->x2 + 1); *fp_data ++ = (float)(para->area->y1);

        data = (uint8_t *)fp_data;
        *data = VLC_OP_LINE; fp_data ++;
        *fp_data ++ = (float)(para->area->x2 + 1); *fp_data ++ = (float)(para->area->y2 + 1);

        data = (uint8_t *)fp_data;
        *data = VLC_OP_LINE; fp_data ++;
        *fp_data ++ = (float)(para->area->x1); *fp_data ++ = (float)(para->area->y2 + 1);

        data = (uint8_t *)fp_data;
        *data = VLC_OP_CLOSE; fp_data ++;

        if(path_mng->head) {
            path_mng->tail->next = d;
        } else {
            path_mng->head = d;
        }
        path_mng->tail = d;
        path_mng->total_size += d->param_size;
        para_in->ret = true;
    } else {
        para_in->ret = false;
    }
}

void lv_vg_lite_path_cmd_handle(void *param)
{
    lv_event_param_path *path_param = (lv_event_param_path *)param;
    switch(path_param->cmd) {
        case LV_PATH_ADD_END:
            lv_vg_lite_add_end_cmd(path_param);
            break;
        case LV_PATH_RECT_BORDER:
            lv_vg_lite_build_rect_border(path_param);
            break;
        case LV_PATH_RECT:
            lv_vg_lite_build_rect(path_param);
            break;
        default:
            LV_LOG_ERROR("unknown vg_lite path cmd");
            break;
    }
}

void lv_vg_lite_draw_unit_path(void * in)
{
    LV_PROFILER_DRAW_BEGIN;

    vg_lite_path_t path;
    lv_event_param_draw_path *param = (lv_event_param_draw_path *)in;
    lv_draw_unit_path_node *d = param->path_node;
    lv_vg_lite_path_build *createPath = d->unit_path;

    lv_draw_vg_lite_unit_t * u = (lv_draw_vg_lite_unit_t *)(param->t->draw_unit);

    /* calc convert matrix */
    vg_lite_matrix_t matrix;
    vg_lite_identity(&matrix);

    vg_lite_translate(param->trans.x, param->trans.y, &matrix);

    /* matrix for drawing, different from matrix for calculating the bounding box */
    vg_lite_matrix_t draw_matrix = u->global_matrix;
    lv_vg_lite_matrix_multiply(&draw_matrix, &matrix);

    if (param->uploaded_path) {
        vg_lite_init_path(&path, VG_LITE_FP32, VG_LITE_HIGH, createPath->path_size, (void *)(createPath->path + 2 * sizeof(uint32_t)), d->area.x1, d->area.y1, d->area.x2,d->area.y2);

        path.uploaded.address = (uint32_t)createPath->path;
        path.uploaded.bytes = createPath->upload_size;
        path.path_changed = 0;
        VLM_PATH_ENABLE_UPLOAD(path);      /* Implicitly enable path uploading. */
    } else {
        LV_ASSERT(vg_lite_init_path(&path, VG_LITE_FP32, VG_LITE_HIGH, param->no_uploaded_path_len, (void *)d->unit_path, d->area.x1, d->area.y1, d->area.x2,d->area.y2) == VG_LITE_SUCCESS);
    }

    lv_vg_lite_draw(
        &u->target_buffer,
        &path,
        VG_LITE_FILL_NON_ZERO,
        &draw_matrix,
        VG_LITE_BLEND_SRC_OVER,
        lv_vg_lite_color(param->draw_letter_dsc->color, param->draw_letter_dsc->opa, true));

    lv_vg_lite_force_flush(u);

    LV_PROFILER_DRAW_END;
}

void lv_vg_lite_event_set_scissor_area(void * in)
{
    lv_event_param_set_scissor_area *param = (lv_event_param_set_scissor_area *)in;
    lv_draw_vg_lite_unit_t * u = (lv_draw_vg_lite_unit_t *)(param->t->draw_unit);
    lv_vg_lite_set_scissor_area(u, param->scissor_area);
}
#endif

/**********************
 *   STATIC FUNCTIONS
 **********************/

static inline bool init_buffer_from_glyph_dsc(vg_lite_buffer_t * buffer, lv_font_glyph_dsc_t * g_dsc)
{
    const void * glyph_bitmap = lv_font_get_glyph_static_bitmap(g_dsc);
    if(!glyph_bitmap) {
        return false;
    }

    if(!LV_VG_LITE_IS_ALIGNED(glyph_bitmap, 16)) {
        LV_LOG_WARN("Glyph data %p is not aligned to 16 bytes", glyph_bitmap);
        return false;
    }

    if(!LV_VG_LITE_IS_ALIGNED(g_dsc->stride, 16)) {
        LV_LOG_WARN("Glyph stride %" LV_PRIu32 " is not aligned to 16 bytes", (uint32_t)g_dsc->stride);
        return false;
    }

    lv_vg_lite_buffer_init(buffer, glyph_bitmap, g_dsc->box_w, g_dsc->box_h, g_dsc->stride, VG_LITE_A8, false);
    return true;
}

static void draw_letter_cb(lv_draw_task_t * t, lv_draw_glyph_dsc_t * glyph_draw_dsc,
                           lv_draw_fill_dsc_t * fill_draw_dsc, const lv_area_t * fill_area)
{
    lv_draw_vg_lite_unit_t * u = (lv_draw_vg_lite_unit_t *)t->draw_unit;
    if(glyph_draw_dsc) {
        switch(glyph_draw_dsc->format) {
            case LV_FONT_GLYPH_FORMAT_A1:
            case LV_FONT_GLYPH_FORMAT_A2:
            case LV_FONT_GLYPH_FORMAT_A3:
            case LV_FONT_GLYPH_FORMAT_A4:
            case LV_FONT_GLYPH_FORMAT_A8: {
                    const lv_font_t * resolved_font = glyph_draw_dsc->g->resolved_font;
                    vg_lite_buffer_t src_buf;
                    if(lv_font_has_static_bitmap(resolved_font)) {
                        if(!init_buffer_from_glyph_dsc(&src_buf, glyph_draw_dsc->g)) {
                            return;
                        }
                    }
                    else {
                        if(resolved_font->release_glyph) {
                            /* For dynamic fonts, its internal implementation already supports cache management. */
                            glyph_draw_dsc->glyph_data = lv_font_get_glyph_bitmap(glyph_draw_dsc->g, glyph_draw_dsc->_draw_buf);
                        }
                        else {
                            /* For non-cached unaligned fonts, we need to manage the cache manually. */
                            glyph_draw_dsc->glyph_data = lv_vg_lite_bitmap_font_cache_get(u, glyph_draw_dsc->g);
                        }

                        if(!glyph_draw_dsc->glyph_data) {
                            return;
                        }

                        lv_vg_lite_buffer_from_draw_buf(&src_buf, glyph_draw_dsc->glyph_data);
                    }

                    draw_letter_bitmap(t, glyph_draw_dsc, &src_buf);
                }
                break;

#if LV_USE_FREETYPE
            case LV_FONT_GLYPH_FORMAT_VECTOR: {
                    if(lv_freetype_is_outline_font(glyph_draw_dsc->g->resolved_font)) {
                        if(!glyph_draw_dsc->glyph_data) {
                            return;
                        }

                        draw_letter_outline(t, glyph_draw_dsc);
                    }
                }
                break;
#endif /* LV_USE_FREETYPE */

            case LV_FONT_GLYPH_FORMAT_IMAGE: {
                    glyph_draw_dsc->glyph_data = lv_font_get_glyph_bitmap(glyph_draw_dsc->g, glyph_draw_dsc->_draw_buf);
                    if(!glyph_draw_dsc->glyph_data) {
                        return;
                    }

                    lv_draw_image_dsc_t image_dsc;
                    lv_draw_image_dsc_init(&image_dsc);
                    image_dsc.opa = glyph_draw_dsc->opa;
                    image_dsc.src = glyph_draw_dsc->glyph_data;
                    image_dsc.rotation = glyph_draw_dsc->rotation;
                    lv_draw_vg_lite_img(t, &image_dsc, glyph_draw_dsc->letter_coords, false);
                }
                break;

#if LV_USE_FONT_PLACEHOLDER
            case LV_FONT_GLYPH_FORMAT_NONE: {
                    if(glyph_draw_dsc->bg_coords == NULL) break;
                    /* Draw a placeholder rectangle*/
                    lv_draw_border_dsc_t border_draw_dsc;
                    lv_draw_border_dsc_init(&border_draw_dsc);
                    border_draw_dsc.opa = glyph_draw_dsc->opa;
                    border_draw_dsc.color = glyph_draw_dsc->color;
                    border_draw_dsc.width = 1;
                    lv_draw_vg_lite_border(t, &border_draw_dsc, glyph_draw_dsc->bg_coords);
                }
                break;
#endif /* LV_USE_FONT_PLACEHOLDER */

            default:
                break;
        }
    }

    if(fill_draw_dsc && fill_area) {
        lv_draw_vg_lite_fill(t, fill_draw_dsc, fill_area);
    }

    /* Flush in time to avoid accumulation of drawing commands */
    u->letter_count++;
    if(u->letter_count > PATH_FLUSH_COUNT_MAX) {
        lv_vg_lite_flush(u);
    }
}

static inline void convert_letter_matrix(vg_lite_matrix_t * matrix, const lv_draw_glyph_dsc_t * dsc)
{
    vg_lite_translate(dsc->letter_coords->x1, dsc->letter_coords->y1, matrix);

    if(!dsc->rotation) {
        return;
    }

    const lv_point_t pivot = {
        .x = dsc->pivot.x,
        .y = dsc->g->box_h + dsc->g->ofs_y
    };
    vg_lite_translate(pivot.x, pivot.y, matrix);
    vg_lite_rotate(dsc->rotation / 10.0f, matrix);
    vg_lite_translate(-pivot.x, -pivot.y, matrix);
}

static bool draw_letter_clip_areas(lv_draw_task_t * t, const lv_draw_glyph_dsc_t * dsc, lv_area_t * letter_area,
                                   lv_area_t * cliped_area)
{
    *letter_area = *dsc->letter_coords;

    if(dsc->rotation) {
        const lv_point_t pivot = {
            .x = dsc->pivot.x,
            .y = dsc->g->box_h + dsc->g->ofs_y
        };

        lv_image_buf_get_transformed_area(
            letter_area,
            lv_area_get_width(dsc->letter_coords),
            lv_area_get_height(dsc->letter_coords),
            dsc->rotation,
            LV_SCALE_NONE,
            LV_SCALE_NONE,
            &pivot);
        lv_area_move(letter_area, dsc->letter_coords->x1, dsc->letter_coords->y1);
    }

    if(!lv_area_intersect(cliped_area, &t->clip_area, letter_area)) {
        return false;
    }

    return true;
}

static void draw_letter_bitmap(lv_draw_task_t * t, const lv_draw_glyph_dsc_t * dsc, vg_lite_buffer_t * src_buf)
{
    LV_PROFILER_DRAW_BEGIN;

    lv_area_t image_area;
    lv_area_t clip_area;
    if(!draw_letter_clip_areas(t, dsc, &image_area, &clip_area)) {
        LV_PROFILER_DRAW_END;
        return;
    }

    lv_draw_vg_lite_unit_t * u = (lv_draw_vg_lite_unit_t *)t->draw_unit;

    vg_lite_matrix_t matrix = u->global_matrix;
    convert_letter_matrix(&matrix, dsc);

    const vg_lite_color_t color = lv_vg_lite_color(dsc->color, dsc->opa, true);

    const int32_t clip_offset_x = clip_area.x1 - image_area.x1;
    const int32_t clip_offset_y = clip_area.y1 - image_area.y1;

    /* If rotation is not required, blit directly */
    if(!dsc->rotation
#if LV_VG_LITE_DISABLE_BLIT_RECT_OFFSET
       /**
        * For some hardware, the rect.x/y parameters of vg_lite_blit_rect do not work correctly,
        * so the fallback is to vg_lite_draw_pattern for processing.
        */
       && (clip_offset_x == 0 && clip_offset_y == 0)
#endif
      ) {
        vg_lite_rectangle_t rect = {
            .x = clip_offset_x,
            .y = clip_offset_y,
            .width = lv_area_get_width(&clip_area),
            .height = lv_area_get_height(&clip_area)
        };

        /* add offset for clipped area */
        if(rect.x || rect.y) {
            vg_lite_translate(rect.x, rect.y, &matrix);
        }

        lv_vg_lite_blit_rect(
            &u->target_buffer,
            src_buf,
            &rect,
            &matrix,
            VG_LITE_BLEND_SRC_OVER,
            color,
            VG_LITE_FILTER_LINEAR);
    }
    else {
        lv_vg_lite_path_t * path = lv_vg_lite_path_get(u, VG_LITE_S16);
        lv_vg_lite_path_append_rect(
            path,
            image_area.x1, image_area.y1,
            lv_area_get_width(&image_area), lv_area_get_height(&image_area),
            0);
        lv_vg_lite_path_end(path);
        lv_vg_lite_path_set_bounding_box_area(path, &clip_area);

        vg_lite_matrix_t path_matrix = u->global_matrix;

        lv_vg_lite_draw_pattern(
            &u->target_buffer,
            lv_vg_lite_path_get_path(path),
            VG_LITE_FILL_EVEN_ODD,
            &path_matrix,
            src_buf,
            &matrix,
            VG_LITE_BLEND_SRC_OVER,
            VG_LITE_PATTERN_COLOR,
            0,
            color,
            VG_LITE_FILTER_LINEAR);

        lv_vg_lite_path_drop(u, path);
    }

    /* Check if the data has cache and add it to the pending list */
    if(dsc->g->entry) {
        /* Increment the cache reference count */
        lv_cache_entry_acquire_data(dsc->g->entry);
        lv_vg_lite_pending_add(u->letter_pending, dsc->g);
    }

    LV_PROFILER_DRAW_END;
}

static void bitmap_cache_release_cb(void * entry, void * user_data)
{
    LV_UNUSED(user_data);
    lv_font_glyph_dsc_t * g_dsc = entry;
    lv_font_glyph_release_draw_data(g_dsc);
}

#if LV_USE_FREETYPE

static void draw_letter_outline(lv_draw_task_t * t, const lv_draw_glyph_dsc_t * dsc)
{
    LV_PROFILER_DRAW_BEGIN;

    lv_area_t letter_area;
    lv_area_t path_clip_area;
    if(!draw_letter_clip_areas(t, dsc, &letter_area, &path_clip_area)) {
        LV_PROFILER_DRAW_END;
        return;
    }

    lv_draw_vg_lite_unit_t * u = (lv_draw_vg_lite_unit_t *)t->draw_unit;

    /* vg-lite bounding_box will crop the pixels on the edge, so +1px is needed here */
    path_clip_area.x2++;
    path_clip_area.y2++;

    lv_vg_lite_path_t * outline = (lv_vg_lite_path_t *)dsc->glyph_data;
    const lv_point_t glyph_pos = {
        dsc->letter_coords->x1 - dsc->g->ofs_x,
        dsc->letter_coords->y1 + dsc->g->box_h + dsc->g->ofs_y
    };
    /* scale size */
    const float scale = FT_F26DOT6_TO_PATH_SCALE(lv_freetype_outline_get_scale(dsc->g->resolved_font));

    const bool has_rotation_with_cliped = dsc->rotation && !lv_area_is_in(&letter_area, &t->clip_area, false);

    /* calc convert matrix */
    vg_lite_matrix_t matrix;
    vg_lite_identity(&matrix);

    if(!has_rotation_with_cliped && dsc->rotation) {
        vg_lite_translate(glyph_pos.x + dsc->pivot.x, glyph_pos.y, &matrix);
        vg_lite_rotate(dsc->rotation / 10.0f, &matrix);
        vg_lite_translate(-dsc->pivot.x, 0, &matrix);
    }
    else {
        vg_lite_translate(glyph_pos.x, glyph_pos.y, &matrix);
    }

    vg_lite_scale(scale, scale, &matrix);

    /* matrix for drawing, different from matrix for calculating the bounding box */
    vg_lite_matrix_t draw_matrix = u->global_matrix;
    lv_vg_lite_matrix_multiply(&draw_matrix, &matrix);

    /* calc inverse matrix */
    vg_lite_matrix_t result;
    if(!lv_vg_lite_matrix_inverse(&result, &matrix)) {
        LV_LOG_ERROR("no inverse matrix");
        lv_vg_lite_matrix_dump_info(&matrix);
        LV_PROFILER_DRAW_END;
        return;
    }

    const lv_point_precise_t p1 = { path_clip_area.x1, path_clip_area.y1 };
    const lv_point_precise_t p1_res = lv_vg_lite_matrix_transform_point(&result, &p1);
    const lv_point_precise_t p2 = { path_clip_area.x2, path_clip_area.y2 };
    const lv_point_precise_t p2_res = lv_vg_lite_matrix_transform_point(&result, &p2);

    if(has_rotation_with_cliped) {
        /**
         * When intersecting the clipping region,
         * rotate the path contents without rotating the bounding box for cropping
         */
        vg_lite_matrix_t internal_matrix;
        vg_lite_identity(&internal_matrix);
        const float pivot_x = (dsc->pivot.x + dsc->g->ofs_x) / scale;
        const float pivot_y = dsc->g->box_h + dsc->g->ofs_y;
        vg_lite_translate(pivot_x, pivot_y, &internal_matrix);
        vg_lite_rotate(dsc->rotation / 10.0f, &internal_matrix);
        vg_lite_translate(-pivot_x, -pivot_y, &internal_matrix);

        lv_vg_lite_path_t * outline_transformed = lv_vg_lite_path_get(u, VG_LITE_FP32);
        lv_vg_lite_path_set_transform(outline_transformed, &internal_matrix);
        lv_vg_lite_path_for_each_data(lv_vg_lite_path_get_path(outline), outline_iter_cb, outline_transformed);
        lv_vg_lite_path_set_bounding_box(outline_transformed, p1_res.x, p2_res.y, p2_res.x, p1_res.y);

        lv_vg_lite_draw(
            &u->target_buffer,
            lv_vg_lite_path_get_path(outline_transformed),
            VG_LITE_FILL_NON_ZERO,
            &draw_matrix,
            VG_LITE_BLEND_SRC_OVER,
            lv_vg_lite_color(dsc->color, dsc->opa, true));

        lv_vg_lite_path_drop(u, outline_transformed);

        LV_PROFILER_DRAW_END;
        return;
    }

    if(dsc->rotation) {
        /* The bounding rectangle before scaling relative to the original coordinates of the path */
        lv_area_t box_area;
        box_area.x1 = dsc->g->ofs_x;
        box_area.y1 = -dsc->g->box_h - dsc->g->ofs_y;
        lv_area_set_width(&box_area, dsc->g->box_w);
        lv_area_set_height(&box_area, dsc->g->box_h);

        /* Workaround for loss of rotation precision */
        lv_area_increase(&box_area, 5, 5);

        /* Scale the path area to fit the original path data */
        lv_vg_lite_path_set_bounding_box(outline,
                                         box_area.x1 / scale,
                                         box_area.y1 / scale,
                                         box_area.x2 / scale,
                                         box_area.y2 / scale);
    }
    else {
        lv_vg_lite_path_set_bounding_box(outline, p1_res.x, p2_res.y, p2_res.x, p1_res.y);
    }

    lv_vg_lite_draw(
        &u->target_buffer,
        lv_vg_lite_path_get_path(outline),
        VG_LITE_FILL_NON_ZERO,
        &draw_matrix,
        VG_LITE_BLEND_SRC_OVER,
        lv_vg_lite_color(dsc->color, dsc->opa, true));

    LV_PROFILER_DRAW_END;
}

static void vg_lite_outline_push(const lv_freetype_outline_event_param_t * param)
{
    LV_PROFILER_DRAW_BEGIN;
    lv_vg_lite_path_t * outline = param->outline;
    LV_ASSERT_NULL(outline);

    lv_freetype_outline_type_t type = param->type;
    switch(type) {

        /**
         * Reverse the Y-axis coordinate direction to achieve
         * the conversion from Cartesian coordinate system to LCD coordinate system
         */
        case LV_FREETYPE_OUTLINE_END:
            lv_vg_lite_path_end(outline);
            break;
        case LV_FREETYPE_OUTLINE_MOVE_TO:
            lv_vg_lite_path_move_to(outline, param->to.x, -param->to.y);
            break;
        case LV_FREETYPE_OUTLINE_LINE_TO:
            lv_vg_lite_path_line_to(outline, param->to.x, -param->to.y);
            break;
        case LV_FREETYPE_OUTLINE_CUBIC_TO:
            lv_vg_lite_path_cubic_to(outline, param->control1.x, -param->control1.y,
                                     param->control2.x, -param->control2.y,
                                     param->to.x, -param->to.y);
            break;
        case LV_FREETYPE_OUTLINE_CONIC_TO:
            lv_vg_lite_path_quad_to(outline, param->control1.x, -param->control1.y,
                                    param->to.x, -param->to.y);
            break;
        default:
            LV_LOG_ERROR("unknown point type: %d", type);
            LV_ASSERT(false);
            break;
    }
    LV_PROFILER_DRAW_END;
}

static void freetype_outline_event_cb(lv_event_t * e)
{
    LV_PROFILER_DRAW_BEGIN;
    lv_event_code_t code = lv_event_get_code(e);
    lv_freetype_outline_event_param_t * param = lv_event_get_param(e);
    switch(code) {
        case LV_EVENT_CREATE:
            param->outline = lv_vg_lite_path_create(PATH_DATA_COORD_FORMAT);
            break;
        case LV_EVENT_DELETE:
            lv_vg_lite_path_destroy(param->outline);
            break;
        case LV_EVENT_INSERT:
            vg_lite_outline_push(param);
            break;
        default:
            LV_LOG_WARN("unknown event code: %d", code);
            break;
    }
    LV_PROFILER_DRAW_END;
}

static void outline_iter_cb(void * user_data, uint8_t op_code, const float * data, uint32_t len)
{
    LV_UNUSED(len);
    typedef struct {
        float x;
        float y;
    } point_t;

    lv_vg_lite_path_t * path = user_data;
    const point_t * pt = (point_t *)data;

    switch(op_code) {
        case VLC_OP_MOVE:
            lv_vg_lite_path_move_to(path, pt->x, pt->y);
            break;
        case VLC_OP_LINE:
            lv_vg_lite_path_line_to(path, pt->x, pt->y);
            break;
        case VLC_OP_QUAD:
            lv_vg_lite_path_quad_to(path, pt[0].x, pt[0].y, pt[1].x, pt[1].y);
            break;
        case VLC_OP_CUBIC:
            lv_vg_lite_path_cubic_to(path, pt[0].x, pt[0].y, pt[1].x, pt[1].y, pt[2].x, pt[2].y);
            break;
        case VLC_OP_CLOSE:
            lv_vg_lite_path_close(path);
            break;
        case VLC_OP_END:
            lv_vg_lite_path_end(path);
            break;
        default:
            LV_ASSERT_FORMAT_MSG(false, "unknown op_code: %d", op_code);
            break;
    }
}

#endif /* LV_USE_FREETYPE */

#endif /*LV_USE_DRAW_VG_LITE*/
