/**
 * @file lv_libjpu.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include "../../draw/lv_image_decoder_private.h"
#include "../../../lvgl.h"
#if LV_USE_LIBJPU

#include "lv_libjpu.h"
#include <stdio.h>
#include <string.h>
#include "../../core/lv_global.h"
#include "jpu_decode.h"

/*********************
 *      DEFINES
 *********************/

#define DECODER_NAME    "JPU"

#define image_cache_draw_buf_handlers &(LV_GLOBAL_DEFAULT()->image_cache_draw_buf_handlers)

#define JPEG_SIGNATURE 0xFFD8FF
#define IS_JPEG_SIGNATURE(x) (((x) & 0x00FFFFFF) == JPEG_SIGNATURE)

/**********************
 *   STATIC PROTOTYPES
 **********************/
static lv_result_t decoder_info(lv_image_decoder_t * decoder, lv_image_decoder_dsc_t * dsc, lv_image_header_t * header);
static lv_result_t decoder_open(lv_image_decoder_t * decoder, lv_image_decoder_dsc_t * dsc);
static void decoder_close(lv_image_decoder_t * decoder, lv_image_decoder_dsc_t * dsc);
static lv_color_format_t jpu_fmt_to_lv_cf(jpu_output_format_t fmt);

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *  GLOBAL FUNCTIONS
 **********************/

/**
 * Register the JPU decoder functions in LVGL
 */
void lv_libjpu_init(void)
{
    lv_image_decoder_t * dec = lv_image_decoder_create();
    lv_image_decoder_set_info_cb(dec, decoder_info);
    lv_image_decoder_set_open_cb(dec, decoder_open);
    lv_image_decoder_set_close_cb(dec, decoder_close);

    dec->name = DECODER_NAME;
}

void lv_libjpu_deinit(void)
{
    lv_image_decoder_t * dec = NULL;
    while((dec = lv_image_decoder_get_next(dec)) != NULL) {
        if(dec->info_cb == decoder_info) {
            lv_image_decoder_delete(dec);
            break;
        }
    }
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/**
 * Map the JPU native output format to an LVGL color format.
 */
static lv_color_format_t jpu_fmt_to_lv_cf(jpu_output_format_t fmt)
{
    switch(fmt) {
        case JPU_OUTPUT_NV12: return LV_COLOR_FORMAT_NV12;
        case JPU_OUTPUT_YUY2: return LV_COLOR_FORMAT_YUY2;
        case JPU_OUTPUT_NV24: return LV_COLOR_FORMAT_NV24;
        case JPU_OUTPUT_I400: return LV_COLOR_FORMAT_I400;
        default:               return LV_COLOR_FORMAT_UNKNOWN;
    }
}

/**
 * Get info about a JPEG image
 */
static lv_result_t decoder_info(lv_image_decoder_t * decoder, lv_image_decoder_dsc_t * dsc, lv_image_header_t * header)
{
    LV_UNUSED(decoder);
    lv_image_src_t src_type = dsc->src_type;

    if(src_type == LV_IMAGE_SRC_FILE) {
        const char * src = dsc->src;
        uint32_t jpg_signature = 0;
        uint32_t rn;
        lv_fs_read(&dsc->file, &jpg_signature, sizeof(jpg_signature), &rn);

        if(rn != sizeof(jpg_signature)) {
            LV_LOG_WARN("file: %s signature len = %" LV_PRIu32 " error", src, rn);
            return LV_RESULT_INVALID;
        }

        const char * ext = lv_fs_get_ext(src);
        bool is_jpeg_ext = (lv_strcmp(ext, "jpg") == 0)
                           || (lv_strcmp(ext, "jpeg") == 0);

        if(!IS_JPEG_SIGNATURE(jpg_signature)) {
            if(is_jpeg_ext) {
                LV_LOG_WARN("file: %s signature = 0X%" LV_PRIX32 " error", src, jpg_signature);
            }
            return LV_RESULT_INVALID;
        }

        jpu_jpeg_info_t jpg_info;

        if(jpu_get_jpeg_info(&jpg_signature, sizeof(jpg_signature), &jpg_info) != 0) {
            uint8_t * data = NULL;
            uint32_t data_size;
            data = lv_fs_load_with_alloc(src, &data_size);
            if(data == NULL) {
                return LV_RESULT_INVALID;
            }
            bool ok = (jpu_get_jpeg_info(data, data_size, &jpg_info) == 0);
            lv_free(data);
            if(!ok) {
                return LV_RESULT_INVALID;
            }
        }

        /* The actual color format is determined at decode time; set a
         * placeholder so LVGL can route the image to this decoder. */
        header->cf = LV_COLOR_FORMAT_NV12;
        header->w = jpg_info.width;
        header->h = jpg_info.height;

        return LV_RESULT_OK;
    }

    return LV_RESULT_INVALID;
}

/**
 * Open a JPEG image and return the decoded image.
 *
 * The JPU kernel driver decodes the JPEG to its native YUV sampling and
 * writes the padded frame buffer directly into the LVGL draw buffer.
 * No format conversion or intermediate copy is performed.
 */
static lv_result_t decoder_open(lv_image_decoder_t * decoder, lv_image_decoder_dsc_t * dsc)
{
    LV_UNUSED(decoder);

    uint8_t * data = NULL;
    uint32_t data_size;

    if(dsc->src_type == LV_IMAGE_SRC_FILE) {
        data = lv_fs_load_with_alloc(dsc->src, &data_size);
    }
    else if(dsc->src_type == LV_IMAGE_SRC_VARIABLE) {
        const lv_image_dsc_t * var = (const lv_image_dsc_t *)dsc->src;
        data = (uint8_t *)var->data;
        data_size = var->data_size;
    }

    if(data == NULL) {
        LV_LOG_WARN("no JPEG data");
        return LV_RESULT_INVALID;
    }

    /* Parse the JPEG SOF marker to obtain picture dimensions, chroma
     * sampling and the padded geometry before decoding. This lets us create
     * the LVGL draw buffer with the correct color format and size, then pass
     * its data pointer directly to the kernel, achieving zero-copy. */
    jpu_jpeg_info_t jpg_info;
    if(jpu_get_jpeg_info(data, data_size, &jpg_info) != 0) {
        LV_LOG_WARN("cannot parse JPEG dimensions");
        if(dsc->src_type == LV_IMAGE_SRC_FILE) {
            lv_free(data);
        }
        return LV_RESULT_INVALID;
    }

    /* The kernel outputs the JPEG's original format without conversion:
     *   420 → NV12, 422 → YUY2, 444 → NV24, 400 → I400 */
    lv_color_format_t cf = jpu_fmt_to_lv_cf(jpg_info.format);
    if(cf == LV_COLOR_FORMAT_UNKNOWN) {
        LV_LOG_WARN("unsupported JPEG sampling");
        if(dsc->src_type == LV_IMAGE_SRC_FILE) {
            lv_free(data);
        }
        return LV_RESULT_INVALID;
    }

    /* Create the draw buffer directly from the geometry reported by
     * jpu_get_jpeg_info(). The kernel writes its padded frame buffer
     * (stride × decoded_height bytes) into this allocation. */
    lv_draw_buf_t * decoded = lv_draw_buf_create_ex(image_cache_draw_buf_handlers,
                                                    jpg_info.decoded_width,
                                                    jpg_info.decoded_height,
                                                    cf, jpg_info.stride);
    if(decoded == NULL) {
        LV_LOG_WARN("draw buf create failed");
        if(dsc->src_type == LV_IMAGE_SRC_FILE) {
            lv_free(data);
        }
        return LV_RESULT_INVALID;
    }

    /* Decode directly into the draw buffer - no intermediate copy. */
    jpu_decoded_image_t img;
    if(jpu_decode_jpeg(data, data_size,
                      decoded->data, decoded->data_size, &img) != 0) {
        LV_LOG_WARN("JPU decode failed");
        lv_draw_buf_destroy(decoded);
        if(dsc->src_type == LV_IMAGE_SRC_FILE) {
            lv_free(data);
        }
        return LV_RESULT_INVALID;
    }

    if(dsc->src_type == LV_IMAGE_SRC_FILE) {
        lv_free(data);
    }

    /* Update the draw buffer header with the actual decoded geometry.
     * The crop rect in the renderer uses dsc->header.h (picture height from
     * decoder_info), so only the visible area is drawn. */
    decoded->header.cf = jpu_fmt_to_lv_cf(img.format);
    decoded->header.w = img.decoded_width;
    decoded->header.h = img.decoded_height;
    decoded->header.stride = img.luma_stride;

    dsc->decoded = decoded;

    if(dsc->args.no_cache) return LV_RESULT_OK;

    if(!lv_image_cache_is_enabled()) return LV_RESULT_OK;

    lv_image_cache_data_t search_key;
    search_key.src_type = dsc->src_type;
    search_key.src = dsc->src;
    search_key.slot.size = decoded->data_size;

    lv_cache_entry_t * entry = lv_image_decoder_add_to_cache(decoder, &search_key, decoded, NULL);

    if(entry == NULL) {
        lv_draw_buf_destroy(decoded);
        return LV_RESULT_INVALID;
    }
    dsc->cache_entry = entry;
    return LV_RESULT_OK;
}

/**
 * Free the allocated resources
 */
static void decoder_close(lv_image_decoder_t * decoder, lv_image_decoder_dsc_t * dsc)
{
    LV_UNUSED(decoder);

    if(dsc->args.no_cache ||
       !lv_image_cache_is_enabled()) lv_draw_buf_destroy((lv_draw_buf_t *)dsc->decoded);
}

#endif /*LV_USE_LIBJPU*/
