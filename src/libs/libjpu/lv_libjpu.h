/**
 * @file lv_libjpu.h
 *
 */

#ifndef LV_LIBJPU_H
#define LV_LIBJPU_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/
#include "../../lv_conf_internal.h"
#if LV_USE_LIBJPU

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Register the JPU (CODAJ12 hardware) JPEG decoder functions in LVGL.
 *
 * The decoder reads a JPEG file/buffer and decodes it through the /dev/jpu
 * kernel driver. The kernel outputs the JPEG's native YUV format directly
 * (NV12 for 420, YUY2 for 422, I444 for 444, I400 for 400). No format
 * conversion is performed; the LVGL draw buffer is the JPU frame buffer.
 */
void lv_libjpu_init(void);

void lv_libjpu_deinit(void);

/**********************
 *      MACROS
 **********************/

#endif /*LV_USE_LIBJPU*/

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /*LV_LIBJPU_H*/
