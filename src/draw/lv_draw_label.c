/**
 * @file lv_draw_label.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include "lv_draw_label_private.h"
#if LV_USE_TXT_BATCH_RENDER
#include "../widgets/label/lv_label_private.h"
#endif
#include "lv_draw_private.h"
#include "../misc/lv_area_private.h"
#include "lv_draw_vector_private.h"
#include "lv_draw_rect_private.h"
#include "../core/lv_obj.h"
#include "../misc/lv_math.h"
#include "../core/lv_obj_event.h"
#include "../misc/lv_bidi_private.h"
#include "../misc/lv_text_private.h"
#include "../misc/lv_assert.h"
#include "../stdlib/lv_mem.h"
#include "../stdlib/lv_string.h"
#include "../core/lv_global.h"

/*********************
 *      DEFINES
 *********************/
#define LABEL_RECOLOR_PAR_LENGTH 6
#define LV_LABEL_HINT_UPDATE_TH 1024 /*Update the "hint" if the label's y coordinates have changed more then this*/

#define font_draw_buf_handlers &(LV_GLOBAL_DEFAULT()->font_draw_buf_handlers)

/**********************
 *      TYPEDEFS
 **********************/
enum {
    RECOLOR_CMD_STATE_WAIT_FOR_PARAMETER,
    RECOLOR_CMD_STATE_PARAMETER,
    RECOLOR_CMD_STATE_TEXT_INPUT,
};
typedef unsigned char cmd_state_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/
static uint8_t hex_char_to_num(char hex);

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *  GLOBAL VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void lv_draw_letter_dsc_init(lv_draw_letter_dsc_t * dsc)
{
    lv_memzero(dsc, sizeof(lv_draw_letter_dsc_t));
    dsc->opa = LV_OPA_COVER;
    dsc->color = lv_color_black();
    dsc->font = LV_FONT_DEFAULT;
    dsc->rotation = 0;
    dsc->scale_x = LV_SCALE_NONE;
    dsc->scale_y = LV_SCALE_NONE;
    dsc->base.dsc_size = sizeof(lv_draw_letter_dsc_t);
}

void lv_draw_label_dsc_init(lv_draw_label_dsc_t * dsc)
{
    lv_memzero(dsc, sizeof(lv_draw_label_dsc_t));
    dsc->opa = LV_OPA_COVER;
    dsc->color = lv_color_black();
    dsc->text_length = LV_TEXT_LEN_MAX;
    dsc->font = LV_FONT_DEFAULT;
    dsc->sel_start = LV_DRAW_LABEL_NO_TXT_SEL;
    dsc->sel_end = LV_DRAW_LABEL_NO_TXT_SEL;
    dsc->sel_color = lv_color_black();
    dsc->sel_bg_color = lv_palette_main(LV_PALETTE_BLUE);
    dsc->bidi_dir = LV_BASE_DIR_LTR;
    dsc->base.dsc_size = sizeof(lv_draw_label_dsc_t);
}

lv_draw_label_dsc_t * lv_draw_task_get_label_dsc(lv_draw_task_t * task)
{
    return task->type == LV_DRAW_TASK_TYPE_LABEL ? (lv_draw_label_dsc_t *)task->draw_dsc : NULL;
}

void lv_draw_glyph_dsc_init(lv_draw_glyph_dsc_t * dsc)
{
    lv_memzero(dsc, sizeof(lv_draw_glyph_dsc_t));
}

void LV_ATTRIBUTE_FAST_MEM lv_draw_label(lv_layer_t * layer, const lv_draw_label_dsc_t * dsc,
                                         const lv_area_t * coords)
{
    if(dsc->opa <= LV_OPA_MIN) return;
    if(dsc->text == NULL || dsc->text[0] == '\0') return;
    if(dsc->font == NULL) {
        LV_LOG_WARN("dsc->font == NULL");
        return;
    }
    if(coords->x1 > coords->x2) {
        /* Attempting to draw a label too small (negative width), cancel to avoid error */
        return;
    }
    LV_PROFILER_DRAW_BEGIN;

    if(dsc->base.drop_shadow_opa) {
        lv_layer_t * ds_layer = lv_draw_layer_create_drop_shadow(layer, &dsc->base, coords);
        LV_ASSERT_NULL(ds_layer);
        lv_draw_label_dsc_t ds_dsc = *dsc;
        ds_dsc.base.drop_shadow_opa = 0; /*Disable drop shadow so rendering below will render plain label*/
        lv_draw_label(ds_layer, &ds_dsc, coords);
        lv_draw_layer_finish_drop_shadow(ds_layer, &dsc->base);
    }


    lv_draw_task_t * t = lv_draw_add_task(layer, coords, LV_DRAW_TASK_TYPE_LABEL);

    lv_memcpy(t->draw_dsc, dsc, sizeof(*dsc));

    /*The text is stored in a local variable so malloc memory for it*/
    if(dsc->text_local) {
        lv_draw_label_dsc_t * new_dsc = t->draw_dsc;
        new_dsc->text = lv_strndup(dsc->text, dsc->text_length);
        LV_ASSERT_MALLOC(new_dsc->text);
    }

    lv_draw_finalize_task_creation(layer, t);
    LV_PROFILER_DRAW_END;
}

void LV_ATTRIBUTE_FAST_MEM lv_draw_character(lv_layer_t * layer, lv_draw_label_dsc_t * dsc,
                                             const lv_point_t * point, uint32_t unicode_letter)
{
    if(dsc->opa <= LV_OPA_MIN) return;
    if(dsc->font == NULL) {
        LV_LOG_WARN("dsc->font == NULL");
        return;
    }

    if(lv_text_is_marker(unicode_letter)) return;

    LV_PROFILER_DRAW_BEGIN;

    lv_font_glyph_dsc_t g;

    lv_font_get_glyph_dsc(dsc->font, &g, unicode_letter, 0);

    lv_area_t a;
    a.x1 = point->x;
    a.y1 = point->y;
    a.x2 = a.x1 + g.adv_w;
    a.y2 = a.y1 + lv_font_get_line_height(g.resolved_font ? g.resolved_font : dsc->font);

    /*lv_draw_label needs UTF8 text so convert the Unicode character to an UTF8 string */
    uint32_t letter_buf[2];
    letter_buf[0] = lv_text_unicode_to_encoded(unicode_letter);
    letter_buf[1] = '\0';

    const char * letter_buf_char = (const char *)letter_buf;

#if LV_BIG_ENDIAN_SYSTEM
    while(*letter_buf_char == 0) ++letter_buf_char;
#endif

    dsc->text = letter_buf_char;
    dsc->text_local = 1;

    lv_draw_label(layer, dsc, &a);
    LV_PROFILER_DRAW_END;
}

void LV_ATTRIBUTE_FAST_MEM lv_draw_letter(lv_layer_t * layer, lv_draw_letter_dsc_t * dsc, const lv_point_t * point)
{
    if(dsc->opa <= LV_OPA_MIN) return;
    if(dsc->font == NULL) {
        LV_LOG_WARN("dsc->font == NULL");
        return;
    }

    const lv_font_t * font = dsc->font;

    LV_PROFILER_DRAW_BEGIN;
    lv_font_glyph_dsc_t g;

    lv_font_get_glyph_dsc(font, &g, dsc->unicode, 0);

    font = g.resolved_font ? g.resolved_font : dsc->font;

    lv_area_t a;
    a.x1 = point->x;
    a.y1 = point->y;
    a.x2 = a.x1 + g.adv_w;
    a.y2 = a.y1 + lv_font_get_line_height(font);

    dsc->pivot.x = g.adv_w / 2 ;
    dsc->pivot.y = font->line_height - font->base_line;

    lv_draw_task_t * t = lv_draw_add_task(layer, &a, LV_DRAW_TASK_TYPE_LETTER);

    lv_memcpy(t->draw_dsc, dsc, sizeof(*dsc));

    lv_draw_finalize_task_creation(layer, t);
    LV_PROFILER_DRAW_END;
}

#if LV_USE_TXT_BATCH_RENDER
#define LV_LABEL_GET_ALIGN_X_POS(x, align, w, content_w) if(align == LV_TEXT_ALIGN_CENTER) { \
                                                            x += ((w - (int32_t)content_w) / 2); \
                                                        }else if(align == LV_TEXT_ALIGN_RIGHT) { \
                                                            x += (w - (int32_t)content_w); \
                                                        }

void lv_draw_label_decor_line(const lv_draw_label_dsc_t * dsc, lv_label_t *label, 
                                  lv_point_t *label_pos, lv_text_align_t align,
                                  int32_t label_w, lv_draw_task_t *t, lv_draw_glyph_cb_t cb, lv_area_t *clip_area)
{
    int32_t offset_y = clip_area->y1 - label_pos->y;
    uint32_t line_start_id, line_height;
    lv_label_line_t *line;
    const lv_font_t * font = dsc->font;
    line_height = lv_font_get_line_height(font) + dsc->line_space;
    if(offset_y > 0) {
        line_start_id = offset_y/line_height;
        line = (label->line_info.line + line_start_id);
    } else {
        line_start_id = 0;
        line = label->line_info.line;
    }
    lv_draw_fill_dsc_t fill_dsc;
    lv_draw_fill_dsc_init(&fill_dsc);
    fill_dsc.opa = dsc->opa;
    fill_dsc.color = dsc->color;
    int32_t underline_width = font->underline_thickness ? font->underline_thickness : 1;
    int32_t pos_line_y = label_pos->y + (line_start_id * line_height);
    int32_t under_line_pos;
    if (dsc->decor & LV_TEXT_DECOR_UNDERLINE) {
        under_line_pos = font->line_height - font->base_line - font->underline_position;
    } else {
        under_line_pos = (font->line_height - font->base_line) * 2 / 3 + font->underline_thickness / 2;
    }
    pos_line_y = pos_line_y + under_line_pos;
    if (pos_line_y < clip_area->y1)  {
        pos_line_y += line_height;
        line ++;
    }
    while ((clip_area->y1 <= pos_line_y) && (pos_line_y <= clip_area->y2)) {
        lv_area_t fill_area;
        fill_area.x1 = label_pos->x;
        LV_LABEL_GET_ALIGN_X_POS(fill_area.x1, align, label_w, line->pixel_w)
        fill_area.x2 = fill_area.x1 + line->pixel_w - 1;
        fill_area.y1 = pos_line_y;
        fill_area.y2 = fill_area.y1 + underline_width - 1;
        cb(t, NULL, &fill_dsc, &fill_area);
        line ++;
        pos_line_y += line_height;
    }
}

lv_draw_unit_path_node *lv_split_one_line_text_path(lv_label_t * label, lv_draw_unit_path_manage * path_mng, lv_draw_unit_t *u, lv_area_t *area)
{
    lv_char_path_node *current_node = path_mng->head, *next_node, *prev_node = NULL;
    lv_draw_unit_path_manage mng;
    lv_event_param_check_path_capa check_param;
    lv_memset(&mng, 0 , sizeof(lv_draw_unit_path_manage));
    check_param.path_mng = &mng;
    check_param.path_w = 0;
    check_param.path_h = 0;
    check_param.display_h = 0;
    check_param.ret = false;

    lv_event_param_build_path  build_para;
    build_para.path_mng = &mng;

    lv_draw_unit_path_node * d;
    lv_draw_unit_path_node * d_head = NULL;

    while (current_node) {
        next_node = current_node->next;
        current_node->next = NULL;
        if(mng.head) {
            mng.tail->next = current_node;
        } else {
            mng.head = current_node;
        }
        mng.tail = current_node;
        mng.total_size += current_node->param_size;
        lv_draw_unit_send_event_to_unit(u, LV_EVENT_CHECK_PATH_CAPA, (void *)&check_param);
        if(check_param.ret) {
            prev_node = current_node;
            current_node = next_node;
        } else {
            if (NULL == prev_node) {
                /* first char */
                mng.tail->next = next_node;
                goto build_error;
            } else {
                mng.tail->next = next_node;
                mng.total_size -= mng.tail->param_size;
                prev_node->next = NULL;
                mng.tail = prev_node;

                d = (lv_draw_unit_path_node *)lv_ll_ins_tail(&(label->draw_unit_path));
                if (!d) {
                    prev_node->next = current_node;
                    goto build_error;
                }
                build_para.ret = NULL;
                lv_draw_unit_send_event_to_unit(u, LV_EVENT_BUILD_PATH, (void *)&build_para);
                prev_node->next = current_node;
                prev_node = NULL;
                if (NULL == build_para.ret) {
                    lv_ll_remove(&(label->draw_unit_path), d);
                    lv_free(d);
                    goto build_error;
                }
                if (NULL == d_head) {
                    d_head = d;
                }
                d->unit_path = build_para.ret;
                d->area = *area;
                lv_memset(&mng, 0 , sizeof(lv_draw_unit_path_manage));
            }
        }
    }
    if (mng.head) {
        d = (lv_draw_unit_path_node *)lv_ll_ins_tail(&(label->draw_unit_path));
        if (!d) {
            goto build_error;
        }
        build_para.ret = NULL;
        lv_draw_unit_send_event_to_unit(u, LV_EVENT_BUILD_PATH, (void *)&build_para);
        if (!build_para.ret) {
            lv_ll_remove(&(label->draw_unit_path), d);
            lv_free(d);
            goto build_error;
        }
        if (NULL == d_head) {
            d_head = d;
        }
        d->unit_path = build_para.ret;
        d->area = *area;
    }

    return d_head;
build_error:
    void *cur = d_head;
    void *next;
    while(cur) {
        next = lv_ll_get_next(&(label->draw_unit_path), cur);
        lv_draw_unit_send_event_to_unit(u, LV_EVENT_DELETE_UNIT_PATH, ((lv_draw_unit_path_node *)cur)->unit_path);
        lv_ll_remove(&(label->draw_unit_path), cur);
        lv_free(cur);
        cur = next;
    }
    return NULL;
}

void lv_draw_get_sel_pos(lv_label_t * label, const lv_draw_label_dsc_t * dsc, lv_point_t * offset, lv_point_t * start, lv_point_t * end)
{
    uint32_t sel_start = dsc->sel_start;
    uint32_t sel_end = dsc->sel_end;
    if(sel_start > sel_end) {
        uint32_t tmp = sel_start;
        sel_start = sel_end;
        sel_end = tmp;
    }
    lv_label_get_letter_pos((const lv_obj_t *)label, sel_start, start);
    lv_label_get_letter_pos((const lv_obj_t *)label, sel_end, end);
    start->x += offset->x;
    start->y += offset->y;
    end->x += offset->x;
    /* end->x is the start pos of end char,but the end char is not in the sleect area.
       So we need decrease 1 */
    end->x --;
    end->y += offset->y;
}

void *lv_draw_sel_area_unit_path(lv_draw_unit_path_node *d, lv_draw_unit_t *u, lv_event_param_draw_path *draw_param, lv_label_t *label)
{
    lv_draw_unit_path_node *d_one_line_next = d;
    draw_param->path_node = d;
    lv_draw_unit_send_event_to_unit(u, LV_EVENT_DRAW_BUILD_PATH, (void *)draw_param);
    while(1) {
        d_one_line_next = (lv_draw_unit_path_node*)lv_ll_get_next(&(label->draw_unit_path), d_one_line_next);
        if ((d_one_line_next) && (d->area.y1 == d_one_line_next->area.y1)) {
            draw_param->path_node = d_one_line_next;
            lv_draw_unit_send_event_to_unit(u, LV_EVENT_DRAW_BUILD_PATH, (void *)draw_param);
            d = d_one_line_next;
        } else {
            break;
        }
    }
    return d;
}

/* return the last node in one line */
void * lv_draw_sel_area_path_handle(lv_draw_unit_path_node *d, lv_point_t * sel_start_pos, lv_point_t * sel_end_pos,
                                  const lv_draw_label_dsc_t * dsc, lv_label_t *label, lv_point_t *label_pos, lv_text_align_t align,
                                  int32_t label_w, lv_event_param_draw_path *draw_param,
                                  lv_draw_glyph_cb_t cb)
{
    const lv_font_t * font;
    int32_t line_height;
    int32_t y, x, d_real_x1, d_real_x2, d_real_y1, d_real_y2;
    lv_area_t sel_area_in_one_line, temp_area, node_sel_area;
    lv_label_line_t *line;
    lv_draw_fill_dsc_t sel_fill_dsc;
    lv_draw_unit_path_node *d_one_line_last = NULL;
    lv_draw_task_t * t = draw_param->t;
    lv_event_param_set_scissor_area scissor_param;
    scissor_param.t = t;
    scissor_param.scissor_area = &temp_area;

    draw_param->path_node = d;
    d_real_x1 = d->area.x1 + label_pos->x;
    d_real_x2 = d->area.x2 + label_pos->x;
    d_real_y1 = d->area.y1 + label_pos->y;
    d_real_y2 = d->area.y2 + label_pos->y;
    if ((d_real_y1 > sel_end_pos->y) || (d_real_y2 <= sel_start_pos->y)) {
        /* It is outside of sel area and do nothing */
        draw_param->draw_letter_dsc->color = dsc->color;
        d_one_line_last = lv_draw_sel_area_unit_path(d, t->draw_unit, draw_param, label);
    } else {
        bool color_same;
        if ((dsc->sel_color.red == dsc->color.red)
            && (dsc->sel_color.green == dsc->color.green)
            && (dsc->sel_color.blue == dsc->color.blue)) {
            color_same = true;
        } else {
            color_same = false;
        }
        font = dsc->font;
        line_height = lv_font_get_line_height(font) + dsc->line_space;
        uint32_t line_start_id = d->area.y1/line_height;
        line = (label->line_info.line + line_start_id);
        y = d_real_y1;

        /* the lines before the select start line */
        if ((!color_same) && (y < sel_start_pos->y)) {
            node_sel_area.x1 = d_real_x1;
            node_sel_area.y1 = d_real_y1;
            node_sel_area.x2 = d_real_x2;
            node_sel_area.y2 = sel_start_pos->y - 1;
            if (lv_area_intersect(&temp_area, &node_sel_area, &t->clip_area)) {
                lv_draw_unit_send_event_to_unit(t->draw_unit, LV_EVENT_SET_SCISSOR_AREA, (void *)&scissor_param);
                draw_param->draw_letter_dsc->color = dsc->color;
                d_one_line_last = lv_draw_sel_area_unit_path(d, t->draw_unit, draw_param, label);
            }
            line += ((sel_start_pos->y - y)/line_height);
            y = sel_start_pos->y;
        }
        lv_draw_fill_dsc_init(&sel_fill_dsc);
        sel_fill_dsc.opa = dsc->opa;
        sel_fill_dsc.color = dsc->sel_bg_color;

        /* the select start line */
        if (y == sel_start_pos->y) {
            x = label_pos->x;
            LV_LABEL_GET_ALIGN_X_POS(x, align, label_w, line->pixel_w)
            sel_area_in_one_line.x1 = sel_start_pos->x - dsc->letter_space / 2;
            sel_area_in_one_line.y1 = y;
            if (sel_start_pos->y == sel_end_pos->y) {
                /* start and end is in the same line, sel area is start to end*/
                sel_area_in_one_line.x2 = sel_end_pos->x - dsc->letter_space / 2;
            } else {
                /* start and end is not in the same line, sel area is start to line end*/
                sel_area_in_one_line.x2 = x + line->pixel_w - 1 + (dsc->letter_space + 1) / 2;
            }
            sel_area_in_one_line.y2 = sel_area_in_one_line.y1 + line_height - 1;
            if (lv_area_intersect(&temp_area, &sel_area_in_one_line, &t->clip_area)) {
                lv_draw_unit_send_event_to_unit(t->draw_unit, LV_EVENT_SET_SCISSOR_AREA, (void *)&scissor_param);
                cb(t, NULL, &sel_fill_dsc, &sel_area_in_one_line);
                if (!color_same) {
                    draw_param->draw_letter_dsc->color = dsc->sel_color;
                    d_one_line_last = lv_draw_sel_area_unit_path(d, t->draw_unit, draw_param, label);
                }
            }
            if (!color_same) {
                if (x < sel_area_in_one_line.x1 ) {
                    node_sel_area.x1 = x;
                    node_sel_area.y1 = sel_area_in_one_line.y1;
                    node_sel_area.x2 = sel_area_in_one_line.x1 - 1;
                    node_sel_area.y2 = sel_area_in_one_line.y2;
                    if (lv_area_intersect(&temp_area, &node_sel_area, &t->clip_area)) {
                        lv_draw_unit_send_event_to_unit(t->draw_unit, LV_EVENT_SET_SCISSOR_AREA, (void *)&scissor_param);
                        draw_param->draw_letter_dsc->color = dsc->color;
                        d_one_line_last = lv_draw_sel_area_unit_path(d, t->draw_unit, draw_param, label);
                    }
                }
                if ((x + line->pixel_w - 1) > sel_area_in_one_line.x2 ) {
                    node_sel_area.x1 = sel_area_in_one_line.x2 + 1;
                    node_sel_area.y1 = sel_area_in_one_line.y1;
                    node_sel_area.x2 = x + line->pixel_w;
                    node_sel_area.y2 = sel_area_in_one_line.y2;
                    if (lv_area_intersect(&temp_area, &node_sel_area, &t->clip_area)) {
                        lv_draw_unit_send_event_to_unit(t->draw_unit, LV_EVENT_SET_SCISSOR_AREA, (void *)&scissor_param);
                        draw_param->draw_letter_dsc->color = dsc->color;
                        d_one_line_last = lv_draw_sel_area_unit_path(d, t->draw_unit, draw_param, label);
                    }
                }
            }
            line ++;
            y += line_height;
        }

        /* the lines between the select start line and the select end line */
        int32_t start_y = y;
        while ((y < d_real_y2) && (y < sel_end_pos->y)) {
            x = label_pos->x;
            LV_LABEL_GET_ALIGN_X_POS(x, align, label_w, line->pixel_w)
            sel_area_in_one_line.x1 = x - dsc->letter_space / 2;
            sel_area_in_one_line.x2 = x + line->pixel_w - 1 + (dsc->letter_space + 1) / 2;
            sel_area_in_one_line.y1 = y;
            sel_area_in_one_line.y2 = sel_area_in_one_line.y1 + line_height - 1;
            if (lv_area_intersect(&temp_area, &sel_area_in_one_line, &t->clip_area)) {
                lv_draw_unit_send_event_to_unit(t->draw_unit, LV_EVENT_SET_SCISSOR_AREA, (void *)&scissor_param);
                cb(t, NULL, &sel_fill_dsc, &sel_area_in_one_line);
            }
            y += line_height;
            line ++;
        }
        if ((!color_same) && (start_y != y)) {
            sel_area_in_one_line.x1 = d_real_x1;
            sel_area_in_one_line.x2 = d_real_x2;
            sel_area_in_one_line.y1 = start_y;
            sel_area_in_one_line.y2 = y - 1;
            if (lv_area_intersect(&temp_area, &sel_area_in_one_line, &t->clip_area)) {
                lv_draw_unit_send_event_to_unit(t->draw_unit, LV_EVENT_SET_SCISSOR_AREA, (void *)&scissor_param);
                draw_param->draw_letter_dsc->color = dsc->sel_color;
                d_one_line_last = lv_draw_sel_area_unit_path(d, t->draw_unit, draw_param, label);
            }
        }

        /* the select end line */
        if ((y < d_real_y2) && (y == sel_end_pos->y)) {
            x = label_pos->x;
            LV_LABEL_GET_ALIGN_X_POS(x, align, label_w, line->pixel_w)
            sel_area_in_one_line.x1 = x - dsc->letter_space / 2;
            sel_area_in_one_line.x2 = sel_end_pos->x - (dsc->letter_space / 2);
            sel_area_in_one_line.y1 = y;
            sel_area_in_one_line.y2 = sel_area_in_one_line.y1 + line_height - 1;
            if (lv_area_intersect(&temp_area, &sel_area_in_one_line, &t->clip_area)) {
                lv_draw_unit_send_event_to_unit(t->draw_unit, LV_EVENT_SET_SCISSOR_AREA, (void *)&scissor_param);
                cb(t, NULL, &sel_fill_dsc, &sel_area_in_one_line);
                if (!color_same) {
                    draw_param->draw_letter_dsc->color = dsc->sel_color;
                    d_one_line_last = lv_draw_sel_area_unit_path(d, t->draw_unit, draw_param, label);
                }
            }
            sel_area_in_one_line.x1 = sel_area_in_one_line.x2;
            sel_area_in_one_line.x2 = x + line->pixel_w;
            if (lv_area_intersect(&temp_area, &sel_area_in_one_line, &t->clip_area)) {
                lv_draw_unit_send_event_to_unit(t->draw_unit, LV_EVENT_SET_SCISSOR_AREA, (void *)&scissor_param);
                draw_param->draw_letter_dsc->color = dsc->color;
                d_one_line_last = lv_draw_sel_area_unit_path(d, t->draw_unit, draw_param, label);
            }
            y += line_height;
            line ++;
        }

        /* the lines after select end line */
        if (color_same) {
            scissor_param.scissor_area = &(t->clip_area);
            lv_draw_unit_send_event_to_unit(t->draw_unit, LV_EVENT_SET_SCISSOR_AREA, (void *)&scissor_param);
            draw_param->draw_letter_dsc->color = dsc->color;
            d_one_line_last = lv_draw_sel_area_unit_path(d, t->draw_unit, draw_param, label);
        } else {
            if (y < d_real_y2) {
                node_sel_area.x1 = d_real_x1;
                node_sel_area.y1 = y;
                node_sel_area.x2 = d_real_x2;
                node_sel_area.y2 = d_real_y2;
                if (lv_area_intersect(&temp_area, &node_sel_area, &t->clip_area)) {
                    lv_draw_unit_send_event_to_unit(t->draw_unit, LV_EVENT_SET_SCISSOR_AREA, (void *)&scissor_param);
                    draw_param->draw_letter_dsc->color = dsc->color;
                    d_one_line_last = lv_draw_sel_area_unit_path(d, t->draw_unit, draw_param, label);
                }
            }
            scissor_param.scissor_area = &(t->clip_area);
            lv_draw_unit_send_event_to_unit(t->draw_unit, LV_EVENT_SET_SCISSOR_AREA, (void *)&scissor_param);
        }
    }
    return d_one_line_last;
}
#endif

void lv_draw_label_iterate_characters(lv_draw_task_t * t, const lv_draw_label_dsc_t * dsc,
                                      const lv_area_t * coords,
                                      lv_draw_glyph_cb_t cb)
{
    lv_draw_dsc_base_t * base_dsc = t->draw_dsc;
    const lv_font_t * font = dsc->font;
    int32_t w;

    lv_area_t clipped_area;
    bool clip_ok = lv_area_intersect(&clipped_area, coords, &t->clip_area);
    if(!clip_ok) return;

    lv_text_align_t align = dsc->align;
    lv_base_dir_t base_dir = dsc->bidi_dir;

    lv_bidi_calculate_align(&align, &base_dir, dsc->text);

    if((dsc->flag & LV_TEXT_FLAG_EXPAND) == 0) {
        /*Normally use the label's width as width*/
        w = lv_area_get_width(coords);
    }
    else {
        /*If EXPAND is enabled then not limit the text's width to the object's width*/
        if(base_dsc->obj && !lv_obj_has_flag(base_dsc->obj, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS)) {
            w = dsc->text_size.x;
        }
        else {
#if LV_USE_TXT_BATCH_RENDER
            w = dsc->text_size.x;
#else
            lv_text_attributes_t attributes = {0};

            attributes.letter_space = dsc->letter_space;
            attributes.line_space = dsc->line_space;
            attributes.max_width = LV_COORD_MAX;
            attributes.text_flags = dsc->flag;

            lv_point_t p;
            lv_text_get_size_attributes(&p, dsc->text, dsc->font, &attributes);
            w = p.x;
#endif
        }
    }

    int32_t line_height_font = lv_font_get_line_height(font);
    int32_t line_height = line_height_font + dsc->line_space;

    /*Init variables for the first line*/
    int32_t line_width = 0;
    lv_point_t pos;
    lv_point_set(&pos, coords->x1, coords->y1);

    int32_t x_ofs = 0;
    int32_t y_ofs = 0;
    x_ofs = dsc->ofs_x;
    y_ofs = dsc->ofs_y;
    pos.y += y_ofs;

    uint32_t line_start     = 0;
    int32_t last_line_start = -1;

#if LV_USE_TXT_BATCH_RENDER
    lv_label_t * label = (lv_label_t *)base_dsc->obj;
    lv_label_line_t *line;
    lv_draw_unit_path_node *d;
    lv_event_param_build_path  build_para;
    const char * bidi_txt = NULL;
    lv_event_param_draw_path draw_param, *draw_param_ptr;
    lv_area_t path_area, temp_area;
    int32_t draw_finished_y = INT32_MIN;
    int32_t clip_area_y1 = t->clip_area.y1;
    if ((0 == dsc->rotation) && (label) && (label->enable_batch_render) && (label->line_info.line) && (label->is_outline_font)) {
        lv_event_param_set_scissor_area scissor_param;
        scissor_param.t = t;
        scissor_param.scissor_area = &(t->clip_area);
        lv_draw_unit_send_event_to_unit(t->draw_unit, LV_EVENT_SET_SCISSOR_AREA, (void *)&scissor_param);
        lv_point_t label_pos;
        label_pos.x = coords->x1 + x_ofs;
        label_pos.y = coords->y1 + y_ofs;

        lv_draw_glyph_dsc_t draw_letter_dsc;
        lv_draw_glyph_dsc_init(&draw_letter_dsc);
        draw_letter_dsc.opa = dsc->opa;
        label->draw_unit = t->draw_unit;

        draw_param.t = t;
        draw_param.draw_letter_dsc = &draw_letter_dsc;
        draw_param.trans.x = label_pos.x;
        draw_param.trans.y = label_pos.y;
        draw_param.uploaded_path = true;
        draw_param_ptr = &draw_param;

        lv_point_t sel_start_pos,sel_end_pos;
        bool sel_valid = false;
        int32_t label_w = lv_area_get_width(coords);

        if (dsc->sel_start != LV_DRAW_LABEL_NO_TXT_SEL && dsc->sel_end != LV_DRAW_LABEL_NO_TXT_SEL) {
            lv_draw_get_sel_pos(label, dsc, &label_pos, &sel_start_pos, &sel_end_pos);
            if ((sel_start_pos.x != sel_end_pos.x) || (sel_start_pos.y != sel_end_pos.y)) {
                sel_valid = true;
            }
        }
        if (label->first_draw) {
            int32_t  pos_x = 0, pos_y = 0;
            int32_t path_rect_y = pos_y;
            lv_display_t *display = lv_obj_get_display((const lv_obj_t * )label);
            int32_t display_h = lv_display_get_vertical_resolution(display);
            lv_draw_unit_path_manage    pre_line_path_mng;
            lv_memset(&(label->path_mng), 0 ,sizeof(lv_draw_unit_path_manage));
            lv_memset(&pre_line_path_mng, 0 ,sizeof(lv_draw_unit_path_manage));

            label->first_draw = false;
            lv_font_glyph_dsc_t glyph_dsc;
            line = label->line_info.line;

            int32_t x_edge_pos = INT32_MAX;

            uint32_t next_char_offset;

            /*Write out all lines*/
            while(dsc->text[line_start] != '\0') {
                LV_LABEL_GET_ALIGN_X_POS(pos_x, align, label_w, line->pixel_w)

                int32_t line_start_x = pos_x;

                x_edge_pos = LV_MIN(pos_x, x_edge_pos);

                /*Write all letter of a line*/
                next_char_offset = 0;
#if LV_USE_BIDI
                size_t bidi_size = line->byte_cnt;
                bidi_txt = lv_malloc(bidi_size + 1);
                LV_ASSERT_MALLOC(bidi_txt);

                /**
                 * has_bided = 1: already executed lv_bidi_process_paragraph.
                 * has_bided = 0: has not been executed lv_bidi_process_paragraph.*/
                if(dsc->has_bided) {
                    lv_memcpy(bidi_txt, &dsc->text[line_start], bidi_size);
                }
                else {
                    lv_bidi_process_paragraph(dsc->text + line_start, bidi_txt, bidi_size, base_dir, NULL, 0);
                }
#else
                bidi_txt = dsc->text + line_start;
#endif


                lv_event_param_path path_param;
                while(next_char_offset < line->byte_cnt) {
                    uint32_t letter;
                    uint32_t letter_next;
#if LV_USE_FONT_PLACEHOLDER
                    lv_area_t bg_coords;
                    bool build_placeholder = false;
#endif
                    lv_text_encoded_letter_next_2(bidi_txt, &letter, &letter_next, &next_char_offset);

                    if (lv_text_is_marker(letter)) {
                        continue;
                    }
                    lv_font_get_glyph_dsc(font, &glyph_dsc, letter, letter_next);

                    if ((glyph_dsc.resolved_font) && (glyph_dsc.format == LV_FONT_GLYPH_FORMAT_VECTOR)) {
                        lv_event_param_add_char param;
                        param.glyph_dsc = &glyph_dsc;
                        /*
                        1.lv_draw_unit_draw_letter:
                            lv_area_t letter_coords;
                            letter_coords.x1 = pos->x + g.ofs_x;
                            letter_coords.x2 = letter_coords.x1 + g.box_w - 1;
                            letter_coords.y1 = pos->y + (font->line_height - font->base_line) - g.box_h - g.ofs_y;
                            letter_coords.y2 = letter_coords.y1 + g.box_h - 1;
                        2.draw_letter_outline:
                            const lv_point_t glyph_pos = {
                                dsc->letter_coords->x1 - dsc->g->ofs_x,
                                dsc->letter_coords->y1 + dsc->g->box_h + dsc->g->ofs_y
                            };
                        */
                        /* This param.pos is a translate vector, not actual position */
                        param.pos.x = pos_x;
                        param.pos.y = pos_y + (font->line_height - font->base_line);

                        param.path_mng = &(label->path_mng);
                        param.ret = LV_EVENT_ADD_CHAR_FAIL;
                        lv_draw_unit_send_event_to_unit(t->draw_unit, LV_EVENT_ADD_CHAR_PATH, (void *)&param);
                        if (LV_EVENT_ADD_CHAR_FAIL == param.ret) {
                            goto build_path_fail;
                        } else if (LV_EVENT_ADD_CHAR_OUTLINE_NULL == param.ret) {
#if LV_USE_FONT_PLACEHOLDER
                            build_placeholder = true;
#endif
                        }
                    } else if (!glyph_dsc.resolved_font) {
#if LV_USE_FONT_PLACEHOLDER
                        build_placeholder = true;
#endif
                    } else {
                        label->is_outline_font = 0;
                        goto build_path_fail;
                    }
#if LV_USE_FONT_PLACEHOLDER
                    if (build_placeholder) {
                        bg_coords.x1 = pos_x - dsc->letter_space / 2;
                        bg_coords.y1 = pos_y;
                        bg_coords.x2 = pos_x + glyph_dsc.adv_w - 1 + (dsc->letter_space + 1) / 2;
                        bg_coords.y2 = bg_coords.y1 + line_height - 1;

                        path_param.cmd = LV_PATH_RECT_BORDER;
                        lv_event_param_path_rect border;
                        border.area = &bg_coords;
                        border.path_mng = &(label->path_mng);
                        path_param.param = (void *)&border;
                        path_param.ret = false;
                        lv_draw_unit_send_event_to_unit(t->draw_unit, LV_EVENT_PATH_CMD, (void *)&path_param);
                        if (false == path_param.ret) {
                            goto build_path_fail;
                        }
                    }
#endif
                    if(glyph_dsc.adv_w > 0) {
                        pos_x += glyph_dsc.adv_w + dsc->letter_space;
                    }
                }

#if LV_USE_BIDI
                lv_free(bidi_txt);
                bidi_txt = NULL;
#endif
                lv_event_param_check_path_capa check_param;
                check_param.path_mng = &(label->path_mng);
                check_param.path_w = dsc->text_size.x;
                check_param.path_h = pos_y + line_height - path_rect_y;
                check_param.display_h = display_h;
                check_param.ret = false;

                lv_draw_unit_send_event_to_unit(t->draw_unit, LV_EVENT_CHECK_PATH_CAPA, (void *)&check_param);
                if(check_param.ret) {
                    lv_memcpy(&pre_line_path_mng, &(label->path_mng), sizeof(lv_draw_unit_path_manage));
                } else {
                    bool split_one_line = true;
                    if (pre_line_path_mng.total_size > 0) {
                        d = (lv_draw_unit_path_node *)lv_ll_ins_tail(&(label->draw_unit_path));
                        if (d) {
                            lv_char_path_node *tmpnode = pre_line_path_mng.tail->next;
                            pre_line_path_mng.tail->next = NULL;
                            /* pos.y is the current line top and it is the previous line bottom */
                            build_para.path_mng = &pre_line_path_mng;
                            build_para.ret = NULL;
                            lv_draw_unit_send_event_to_unit(t->draw_unit, LV_EVENT_BUILD_PATH, (void *)&build_para);
                            if (build_para.ret) {
                                d->unit_path = build_para.ret;
                                d->area.x1 = x_edge_pos;
                                d->area.x2 = x_edge_pos + dsc->text_size.x - 1;
                                d->area.y1 = path_rect_y;
                                d->area.y2 = pos_y - 1;

                                label->path_mng.total_size -= pre_line_path_mng.total_size;
                                label->path_mng.head = tmpnode;

                                path_area.x1 = d->area.x1 + label_pos.x;
                                path_area.x2 = d->area.x2 + label_pos.x;
                                path_area.y1 = d->area.y1 + label_pos.y;
                                path_area.y2 = d->area.y2 + label_pos.y;
                                if (lv_area_intersect(&temp_area, &path_area, &t->clip_area)) {
                                    if (!sel_valid) {
                                        draw_letter_dsc.color = dsc->color;
                                        draw_param_ptr->path_node = d;
                                        lv_draw_unit_send_event_to_unit(t->draw_unit, LV_EVENT_DRAW_BUILD_PATH, (void *)draw_param_ptr);
                                    } else {
                                        lv_draw_sel_area_path_handle(d, &sel_start_pos, &sel_end_pos, dsc, label, &label_pos, align,
                                                                    label_w, draw_param_ptr, cb);
                                    }
                                    if (dsc->decor != LV_TEXT_DECOR_NONE) {
                                        lv_draw_label_decor_line(dsc, label, &label_pos, align, label_w, t, cb, &temp_area);
                                    }
                                }

                                draw_finished_y = path_area.y2;

                                lv_draw_unit_send_event_to_unit(t->draw_unit, LV_EVENT_DELETE_PATH_MNG, (void *)&pre_line_path_mng);
                                path_rect_y = pos_y;
                            } else {
                                LV_LOG_WARN("path create fail");
                                pre_line_path_mng.tail->next = tmpnode;
                                lv_ll_remove(&(label->draw_unit_path), d);
                                lv_free(d);
                                goto build_path_fail;
                            }
                        } else {
                            LV_LOG_WARN("path node null");
                            goto build_path_fail;
                        }
                        check_param.path_h = line_height;
                        check_param.ret = false;
                        lv_draw_unit_send_event_to_unit(t->draw_unit, LV_EVENT_CHECK_PATH_CAPA, (void *)&check_param);
                        if(check_param.ret) {
                            lv_memcpy(&pre_line_path_mng, &(label->path_mng), sizeof(lv_draw_unit_path_manage));
                            split_one_line = false;
                        }
                    } else if (0 == pre_line_path_mng.total_size) {
                        path_rect_y = pos_y;
                    }
                    if (split_one_line) {
                        /* the current line path too long,split it */
                        LV_LOG_WARN("split current line path");
                        lv_area_t line_area;
                        line_area.x1 = line_start_x;
                        line_area.x2 = line_area.x1 + line->pixel_w - 1;
                        line_area.y1 = path_rect_y;
                        line_area.y2 = path_rect_y + line_height - 1;
                        d = lv_split_one_line_text_path(label, &(label->path_mng), t->draw_unit, &line_area);
                        if (d) {
                            path_area.x1 = d->area.x1 + label_pos.x;
                            path_area.x2 = d->area.x2 + label_pos.x;
                            path_area.y1 = d->area.y1 + label_pos.y;
                            path_area.y2 = d->area.y2 + label_pos.y;
                            if (lv_area_intersect(&temp_area, &path_area, &t->clip_area)) {
                                if (!sel_valid) {
                                    while (d) {
                                        draw_letter_dsc.color = dsc->color;
                                        draw_param_ptr->path_node = d;
                                        lv_draw_unit_send_event_to_unit(t->draw_unit, LV_EVENT_DRAW_BUILD_PATH, (void *)draw_param_ptr);
                                        d = (lv_draw_unit_path_node *)lv_ll_get_next(&(label->draw_unit_path), d);
                                    }
                                } else {
                                        lv_draw_sel_area_path_handle(d, &sel_start_pos, &sel_end_pos, dsc, label, &label_pos, align,
                                                                    label_w, draw_param_ptr, cb);
                                }
                                if (dsc->decor != LV_TEXT_DECOR_NONE) {
                                    lv_draw_label_decor_line(dsc, label, &label_pos, align, label_w, t, cb, &temp_area);
                                }
                            }
                            draw_finished_y = path_area.y2;
                            path_rect_y += line_height;
                            lv_draw_unit_send_event_to_unit(t->draw_unit, LV_EVENT_DELETE_PATH_MNG, (void *)&label->path_mng);
                        } else {
                            goto build_path_fail;
                        }
                    }
                }

                line_start += line->byte_cnt;
                pos_x = 0;

                /*Go the next line position*/
                pos_y += line_height;
                line ++;
            }

            if (label->path_mng.head) {
                d = (lv_draw_unit_path_node *)lv_ll_ins_tail(&(label->draw_unit_path));
                if (d) {
                    build_para.path_mng = &label->path_mng;
                    build_para.ret = NULL;
                    lv_draw_unit_send_event_to_unit(t->draw_unit, LV_EVENT_BUILD_PATH, (void *)&build_para);
                    if (build_para.ret) {
                        d->unit_path = build_para.ret;
                        d->area.x1 = x_edge_pos;
                        d->area.x2 = x_edge_pos + dsc->text_size.x - 1;
                        d->area.y1 = path_rect_y;
                        d->area.y2 = pos_y - 1;

                        path_area.x1 = d->area.x1 + label_pos.x;
                        path_area.x2 = d->area.x2 + label_pos.x;
                        path_area.y1 = d->area.y1 + label_pos.y;
                        path_area.y2 = d->area.y2 + label_pos.y;
                        if (lv_area_intersect(&temp_area, &path_area, &t->clip_area)) {
                            if (!sel_valid) {
                                draw_letter_dsc.color = dsc->color;
                                draw_param_ptr->path_node = d;
                                lv_draw_unit_send_event_to_unit(t->draw_unit, LV_EVENT_DRAW_BUILD_PATH, (void *)draw_param_ptr);
                            } else {
                                lv_draw_sel_area_path_handle(d, &sel_start_pos, &sel_end_pos, dsc, label, &label_pos, align,
                                                            label_w, draw_param_ptr, cb);
                            }
                            if (dsc->decor != LV_TEXT_DECOR_NONE) {
                                lv_draw_label_decor_line(dsc, label, &label_pos, align, label_w, t, cb, &temp_area);
                            }
                        }
                        lv_draw_unit_send_event_to_unit(t->draw_unit, LV_EVENT_DELETE_PATH_MNG, (void *)&(label->path_mng));
                    } else {
                        LV_LOG_WARN("last path create fail");
                        lv_ll_remove(&(label->draw_unit_path), d);
                        lv_free(d);
                        goto build_path_fail;
                    }
                } else {
                    LV_LOG_WARN("last path no node mem");
                    goto build_path_fail;
                }
            }
            lv_draw_unit_send_event_to_unit(t->draw_unit, LV_EVENT_RESTORE_SCISSOR_AREA, (void *)(t->draw_unit));
            return;
        } else if (!lv_ll_is_empty(&(label->draw_unit_path))) {
            int32_t tran_x = label_pos.x;
            int32_t tran_y = label_pos.y;

            draw_param.trans.x = tran_x;
            draw_param.trans.y = tran_y;
            lv_draw_unit_path_node* d = (lv_draw_unit_path_node*)lv_ll_get_head(&(label->draw_unit_path));
            while(d) {
                path_area.x1 = d->area.x1 + tran_x;
                path_area.x2 = d->area.x2 + tran_x;
                path_area.y1 = d->area.y1 + tran_y;
                path_area.y2 = d->area.y2 + tran_y;
                if (lv_area_intersect(&temp_area, &path_area, &t->clip_area)) {
                    if (!sel_valid) {
                        draw_letter_dsc.color = dsc->color;
                        draw_param_ptr->path_node = d;
                        lv_draw_unit_send_event_to_unit(t->draw_unit, LV_EVENT_DRAW_BUILD_PATH, (void *)draw_param_ptr);
                    } else {
                        /* todo one line */
                        d = lv_draw_sel_area_path_handle(d, &sel_start_pos, &sel_end_pos, dsc, label, &label_pos, align,
                                    label_w, draw_param_ptr, cb);
                    }
                }
                d = (lv_draw_unit_path_node*)lv_ll_get_next(&(label->draw_unit_path), d);
            }
            if (dsc->decor != LV_TEXT_DECOR_NONE) {
                lv_draw_label_decor_line(dsc, label, &label_pos, align, label_w, t, cb, &t->clip_area);
            }
            lv_draw_unit_send_event_to_unit(t->draw_unit, LV_EVENT_RESTORE_SCISSOR_AREA, (void *)(t->draw_unit));
            return;
        } else {
            lv_draw_unit_send_event_to_unit(t->draw_unit, LV_EVENT_RESTORE_SCISSOR_AREA, (void *)(t->draw_unit));
            goto _lv_draw_;
        }
    }
build_path_fail:
    lv_draw_unit_send_event_to_unit(t->draw_unit, LV_EVENT_RESTORE_SCISSOR_AREA, (void *)(t->draw_unit));
#if LV_USE_BIDI
    if (bidi_txt) {
        lv_free(bidi_txt);
    }
#endif
    if (label) {
        lv_draw_unit_send_event_to_unit(t->draw_unit, LV_EVENT_DELETE_PATH_MNG, (void *)&(label->path_mng));
        d = (lv_draw_unit_path_node*)lv_ll_get_head(&(label->draw_unit_path));
        while(d){
            lv_draw_unit_send_event_to_unit(t->draw_unit, LV_EVENT_DELETE_UNIT_PATH, d->unit_path);
            d = (lv_draw_unit_path_node*)lv_ll_get_next(&(label->draw_unit_path), d);
        }
        lv_ll_clear(&(label->draw_unit_path));
        label->draw_unit = NULL;
    }

    if ((draw_finished_y > t->clip_area.y1) && (draw_finished_y < t->clip_area.y2)) {
        /* The lines which has been done is not need to draw. If opa is valid, the result is error */
        t->clip_area.y1 = draw_finished_y;
    } else if ((draw_finished_y >= t->clip_area.y2)){
        /* All of line in clip_area has been draw,just return */
        return;
    }
    line_start = 0;

_lv_draw_:
#endif
    /*Check the hint to use the cached info*/
    if(dsc->hint && y_ofs == 0 && coords->y1 < 0) {
        /*If the label changed too much recalculate the hint.*/
        if(LV_ABS(dsc->hint->coord_y - coords->y1) > LV_LABEL_HINT_UPDATE_TH - 2 * line_height) {
            dsc->hint->line_start = -1;
        }
        last_line_start = dsc->hint->line_start;
    }

    /*Use the hint if it's valid*/
    if(dsc->hint && last_line_start >= 0) {
        line_start = last_line_start;
        pos.y += dsc->hint->y;
    }

    uint32_t remaining_len = dsc->text_length;
    lv_text_attributes_t attributes = {0};
    attributes.letter_space = dsc->letter_space;
    attributes.text_flags = dsc->flag;
    attributes.max_width = w;

    uint32_t line_end = line_start + lv_text_get_next_line(&dsc->text[line_start], remaining_len, font, NULL, &attributes);

    /*Go the first visible line*/
    while(pos.y + line_height_font < t->clip_area.y1) {
        /*Go to next line*/
        remaining_len -= line_end - line_start;
        line_start = line_end;
        line_end += lv_text_get_next_line(&dsc->text[line_start], remaining_len, font, NULL, &attributes);
        pos.y += line_height;

        /*Save at the threshold coordinate*/
        if(dsc->hint && pos.y >= -LV_LABEL_HINT_UPDATE_TH && dsc->hint->line_start < 0) {
            dsc->hint->line_start = line_start;
            dsc->hint->y          = pos.y - coords->y1;
            dsc->hint->coord_y    = coords->y1;
        }

#if LV_USE_TXT_BATCH_RENDER
        if(dsc->text[line_start] == '\0'){
            t->clip_area.y1 = clip_area_y1;
            return;
        }
#else
        if(dsc->text[line_start] == '\0') return;
#endif
    }

    /*Align to middle*/
    if(align == LV_TEXT_ALIGN_CENTER) {
        line_width = lv_text_get_width(&dsc->text[line_start], line_end - line_start, font, &attributes);
        pos.x += (lv_area_get_width(coords) - line_width) / 2;

    }
    /*Align to the right*/
    else if(align == LV_TEXT_ALIGN_RIGHT) {
        line_width = lv_text_get_width(&dsc->text[line_start], line_end - line_start, font, &attributes);
        pos.x += lv_area_get_width(coords) - line_width;
    }

    uint32_t sel_start = dsc->sel_start;
    uint32_t sel_end = dsc->sel_end;
    if(sel_start > sel_end) {
        uint32_t tmp = sel_start;
        sel_start = sel_end;
        sel_end = tmp;
    }

    lv_area_t bg_coords;
    lv_draw_glyph_dsc_t draw_letter_dsc;
    lv_font_glyph_dsc_t glyph_dsc;
    lv_draw_glyph_dsc_init(&draw_letter_dsc);
    draw_letter_dsc.opa = dsc->opa;
    draw_letter_dsc.bg_coords = &bg_coords;
    draw_letter_dsc.color = dsc->color;
    draw_letter_dsc.rotation = dsc->rotation;
    draw_letter_dsc.g = &glyph_dsc;

    /* Set letter outline stroke attributes */
    draw_letter_dsc.outline_stroke_width = dsc->outline_stroke_width;
    draw_letter_dsc.outline_stroke_opa = dsc->outline_stroke_opa;
    draw_letter_dsc.outline_stroke_color = dsc->outline_stroke_color;

    lv_draw_fill_dsc_t fill_dsc;
    lv_draw_fill_dsc_init(&fill_dsc);
    fill_dsc.opa = dsc->opa;
    int32_t underline_width = font->underline_thickness ? font->underline_thickness : 1;
    int32_t line_start_x;
    uint32_t next_char_offset;
    uint32_t recolor_command_start_index = 0;
    int32_t letter_w;

    cmd_state_t recolor_cmd_state = RECOLOR_CMD_STATE_WAIT_FOR_PARAMETER;
    lv_color_t recolor = lv_color_black(); /* Holds the selected color inside the recolor command */
    uint8_t is_first_space_after_cmd = 0;

    /*Write out all lines*/
    while(remaining_len && dsc->text[line_start] != '\0') {
        pos.x += x_ofs;
        line_start_x = pos.x;

        /*Write all letter of a line*/
        next_char_offset = 0;
#if LV_USE_BIDI
        size_t bidi_size = line_end - line_start;
        char * bidi_txt = lv_malloc(bidi_size + 1);
        LV_ASSERT_MALLOC(bidi_txt);

        /**
          * has_bided = 1: already executed lv_bidi_process_paragraph.
          * has_bided = 0: has not been executed lv_bidi_process_paragraph.*/
        if(dsc->has_bided) {
            lv_memcpy(bidi_txt, &dsc->text[line_start], bidi_size);
        }
        else {
            lv_bidi_process_paragraph(dsc->text + line_start, bidi_txt, bidi_size, base_dir, NULL, 0);
        }
#else
        const char * bidi_txt = dsc->text + line_start;
#endif

        while(next_char_offset < remaining_len && next_char_offset < line_end - line_start) {
            uint32_t logical_char_pos = 0;

            /* Check if the text selection is enabled */
            if(sel_start != LV_DRAW_LABEL_NO_TXT_SEL && sel_end != LV_DRAW_LABEL_NO_TXT_SEL) {
#if LV_USE_BIDI
                if(dsc->has_bided) {
                    logical_char_pos = lv_text_encoded_get_char_id(dsc->text, line_start + next_char_offset);
                }
                else {
                    logical_char_pos = lv_text_encoded_get_char_id(dsc->text, line_start);
                    uint32_t c_idx = lv_text_encoded_get_char_id(bidi_txt, next_char_offset);
                    logical_char_pos += lv_bidi_get_logical_pos(bidi_txt, NULL, line_end - line_start, base_dir, c_idx, NULL);
                }
#else
                logical_char_pos = lv_text_encoded_get_char_id(dsc->text, line_start + next_char_offset);
#endif
            }

            uint32_t letter;
            uint32_t letter_next;
            lv_text_encoded_letter_next_2(bidi_txt, &letter, &letter_next, &next_char_offset);

            /* If recolor is enabled */
            if((dsc->flag & LV_TEXT_FLAG_RECOLOR) != 0) {

                if(letter == (uint32_t)LV_TXT_COLOR_CMD[0]) {
                    /* Handle the recolor command marker depending of the current recolor state */

                    if(recolor_cmd_state == RECOLOR_CMD_STATE_WAIT_FOR_PARAMETER) {
                        recolor_command_start_index = next_char_offset;
                        recolor_cmd_state = RECOLOR_CMD_STATE_PARAMETER;
                        continue;
                    }
                    /*Other start char in parameter escaped cmd. char*/
                    else if(recolor_cmd_state == RECOLOR_CMD_STATE_PARAMETER) {
                        recolor_cmd_state = RECOLOR_CMD_STATE_WAIT_FOR_PARAMETER;
                    }
                    /* If letter is LV_TXT_COLOR_CMD and we were in the CMD_STATE_IN then the recolor close marked has been found */
                    else if(recolor_cmd_state == RECOLOR_CMD_STATE_TEXT_INPUT) {
                        recolor_cmd_state = RECOLOR_CMD_STATE_WAIT_FOR_PARAMETER;
                        continue;
                    }
                }

                /* Find the first space (aka ' ') after the recolor command parameter, we need to skip rendering it */
                if((recolor_cmd_state == RECOLOR_CMD_STATE_PARAMETER) && (letter == ' ') && (is_first_space_after_cmd == 0)) {
                    is_first_space_after_cmd = 1;
                }
                else {
                    is_first_space_after_cmd = 0;
                }

                /* Skip the color parameter and wait the space after it
                 * Once we have reach the space ' ', then we will extract the color information
                 * and store it into the recolor variable */
                if(recolor_cmd_state == RECOLOR_CMD_STATE_PARAMETER) {
                    /* Not an space? Continue with the next character */
                    if(letter != ' ') {
                        continue;
                    }

                    /*Get the recolor parameter*/
                    if((next_char_offset - recolor_command_start_index) == LABEL_RECOLOR_PAR_LENGTH + 1) {
                        /* Temporary buffer to hold the recolor information */
                        char buf[LABEL_RECOLOR_PAR_LENGTH + 1];
                        lv_memcpy(buf, &bidi_txt[recolor_command_start_index], LABEL_RECOLOR_PAR_LENGTH);
                        buf[LABEL_RECOLOR_PAR_LENGTH] = '\0';

                        uint8_t r, g, b;
                        r = (hex_char_to_num(buf[0]) << 4) + hex_char_to_num(buf[1]);
                        g = (hex_char_to_num(buf[2]) << 4) + hex_char_to_num(buf[3]);
                        b = (hex_char_to_num(buf[4]) << 4) + hex_char_to_num(buf[5]);

                        recolor = lv_color_make(r, g, b);
                    }
                    else {
                        recolor.red = dsc->color.red;
                        recolor.blue = dsc->color.blue;
                        recolor.green = dsc->color.green;
                    }

                    /*After the parameter the text is in the command*/
                    recolor_cmd_state = RECOLOR_CMD_STATE_TEXT_INPUT;
                }

                /* Don't draw the first space after the recolor command */
                if(is_first_space_after_cmd) {
                    continue;
                }
            }

            /* If we're in the CMD_STATE_IN state then we need to subtract the recolor command length */
            if(((dsc->flag & LV_TEXT_FLAG_RECOLOR) != 0) && (recolor_cmd_state == RECOLOR_CMD_STATE_TEXT_INPUT)) {
                logical_char_pos -= (LABEL_RECOLOR_PAR_LENGTH + 1);
            }

            lv_font_get_glyph_dsc(font, &glyph_dsc, letter, letter_next);
            letter_w = lv_text_is_marker(letter) ? 0 : glyph_dsc.adv_w;

            /*Always set the bg_coordinates for placeholder drawing*/
            bg_coords.x1 = pos.x - dsc->letter_space / 2;
            bg_coords.y1 = pos.y;
            bg_coords.x2 = pos.x + letter_w - 1 + (dsc->letter_space + 1) / 2;
            bg_coords.y2 = pos.y + line_height - 1;

            if(next_char_offset >= line_end - line_start) {
                if(dsc->decor & LV_TEXT_DECOR_UNDERLINE) {
                    lv_area_t fill_area;
                    fill_area.x1 = line_start_x;
                    fill_area.x2 = pos.x + letter_w - 1;
                    fill_area.y1 = pos.y + font->line_height - font->base_line - font->underline_position;
                    fill_area.y2 = fill_area.y1 + underline_width - 1;

                    fill_dsc.color = dsc->color;
                    cb(t, NULL, &fill_dsc, &fill_area);
                }
                if(dsc->decor & LV_TEXT_DECOR_STRIKETHROUGH) {
                    lv_area_t fill_area;
                    fill_area.x1 = line_start_x;
                    fill_area.x2 = pos.x + letter_w - 1;
                    fill_area.y1 = pos.y + (font->line_height - font->base_line) * 2 / 3 + font->underline_thickness / 2;
                    fill_area.y2 = fill_area.y1 + underline_width - 1;

                    fill_dsc.color = dsc->color;
                    cb(t, NULL, &fill_dsc, &fill_area);
                }
            }

            /* Handle text selection */
            if(sel_start != LV_DRAW_LABEL_NO_TXT_SEL && sel_end != LV_DRAW_LABEL_NO_TXT_SEL
               && logical_char_pos >= sel_start && logical_char_pos < sel_end) {
                draw_letter_dsc.color = dsc->sel_color;
                fill_dsc.color = dsc->sel_bg_color;
                cb(t, NULL, &fill_dsc, &bg_coords);
            }
            else if(recolor_cmd_state == RECOLOR_CMD_STATE_TEXT_INPUT) {
                draw_letter_dsc.color = recolor;
            }
            else {
                draw_letter_dsc.color = dsc->color;
            }

            lv_draw_unit_draw_letter(t, &draw_letter_dsc, &pos, font, letter, cb);

            if(letter_w > 0) {
                pos.x += letter_w + dsc->letter_space;
            }
        }

#if LV_USE_BIDI
        lv_free(bidi_txt);
        bidi_txt = NULL;
#endif

        lv_text_attributes_t text_attributes = {0};
        text_attributes.letter_space = dsc->letter_space;
        text_attributes.text_flags = dsc->flag;
        text_attributes.max_width = w;

        /*Go to next line*/
        remaining_len -= line_end - line_start;
        line_start = line_end;
        if(remaining_len) {
            line_end += lv_text_get_next_line(&dsc->text[line_start], remaining_len, font, NULL, &text_attributes);
        }

        pos.x = coords->x1;
        /*Align to middle*/
        if(align == LV_TEXT_ALIGN_CENTER) {
            line_width =
                lv_text_get_width(&dsc->text[line_start], line_end - line_start, font, &text_attributes);

            pos.x += (lv_area_get_width(coords) - line_width) / 2;
        }
        /*Align to the right*/
        else if(align == LV_TEXT_ALIGN_RIGHT) {
            line_width =
                lv_text_get_width(&dsc->text[line_start], line_end - line_start, font, &text_attributes);
            pos.x += lv_area_get_width(coords) - line_width;
        }

        /*Go the next line position*/
        pos.y += line_height;

        if(pos.y > t->clip_area.y2) break;
    }

    if(draw_letter_dsc._draw_buf) lv_draw_buf_destroy(draw_letter_dsc._draw_buf);

    LV_ASSERT_MEM_INTEGRITY();

#if LV_USE_TXT_BATCH_RENDER
    t->clip_area.y1 = clip_area_y1;
#endif
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/**
 * Convert a hexadecimal characters to a number (0..15)
 * @param hex Pointer to a hexadecimal character (0..9, A..F)
 * @return the numerical value of `hex` or 0 on error
 */
static uint8_t hex_char_to_num(char hex)
{
    if(hex >= '0' && hex <= '9') return hex - '0';
    if(hex >= 'a') hex -= 'a' - 'A'; /*Convert to upper case*/
    return 'A' <= hex && hex <= 'F' ? hex - 'A' + 10 : 0;
}

void lv_draw_unit_draw_letter(lv_draw_task_t * t, lv_draw_glyph_dsc_t * dsc,  const lv_point_t * pos,
                              const lv_font_t * font, uint32_t letter, lv_draw_glyph_cb_t cb)
{
    lv_font_glyph_dsc_t g;

    if(lv_text_is_marker(letter)) /*Markers are valid letters but should not be rendered.*/
        return;

    LV_PROFILER_DRAW_BEGIN;
    if(dsc->g == NULL) {
        dsc->g = &g;
        /*If the glyph dsc is not set then get it from the font*/
        bool g_ret = lv_font_get_glyph_dsc(font, &g, letter, 0);
        if(g_ret == false) {
            /*Add warning if the dsc is not found*/
            LV_LOG_WARN("lv_draw_letter: glyph dsc. not found for U+%" LV_PRIX32, letter);
        }
    }
    else {
        /*If the glyph dsc is set then use it*/
        g = *dsc->g;
    }

    /*Don't draw anything if the character is empty. E.g. space*/
    if((g.box_h == 0) || (g.box_w == 0)) {
        goto exit;
    }

    lv_area_t letter_coords;
    letter_coords.x1 = pos->x + g.ofs_x;
    letter_coords.x2 = letter_coords.x1 + g.box_w - 1;
    letter_coords.y1 = pos->y + (font->line_height - font->base_line) - g.box_h - g.ofs_y;
    letter_coords.y2 = letter_coords.y1 + g.box_h - 1;
    lv_area_move(&letter_coords, -dsc->pivot.x, -dsc->pivot.y);

    /*If the letter is completely out of mask don't draw it*/
    if(lv_area_is_out(&letter_coords, &t->clip_area, 0) &&
       dsc->bg_coords &&
       lv_area_is_out(dsc->bg_coords, &t->clip_area, 0)) {
        goto exit;
    }

    if(g.resolved_font) {
        lv_draw_buf_t * draw_buf = NULL;
        if(LV_FONT_GLYPH_FORMAT_NONE < g.format && g.format < LV_FONT_GLYPH_FORMAT_IMAGE) {
            /*Only check draw buf for bitmap glyph*/
            draw_buf = lv_draw_buf_reshape(dsc->_draw_buf, 0, g.box_w, g.box_h, LV_STRIDE_AUTO);
            if(draw_buf == NULL) {
                if(dsc->_draw_buf) lv_draw_buf_destroy(dsc->_draw_buf);

                uint32_t h = LV_ROUND_UP(g.box_h, 32); /*Assume a larger size to avoid many reallocations*/
                draw_buf = lv_draw_buf_create_ex(font_draw_buf_handlers, g.box_w, h, LV_COLOR_FORMAT_A8, LV_STRIDE_AUTO);
                LV_ASSERT_MALLOC(draw_buf);
                draw_buf->header.h = g.box_h;
                dsc->_draw_buf = draw_buf;
            }
        }

        dsc->format = g.format;

        if(g.format == LV_FONT_GLYPH_FORMAT_VECTOR) {

            /*Load the outline of the glyph, even if the function says bitmap*/
            dsc->glyph_data = (void *) lv_font_get_glyph_bitmap(dsc->g, draw_buf);
            dsc->format = dsc->glyph_data ? g.format : LV_FONT_GLYPH_FORMAT_NONE;
        }
    }
    else {
        dsc->format = LV_FONT_GLYPH_FORMAT_NONE;
    }

    dsc->letter_coords = &letter_coords;
    cb(t, dsc, NULL, NULL);

    lv_font_glyph_release_draw_data(dsc->g);

exit:
    if(dsc->g == &g) {
        /* If the glyph was created locally, we don't need to keep it */
        dsc->g = NULL;
    }
    LV_PROFILER_DRAW_END;
}
