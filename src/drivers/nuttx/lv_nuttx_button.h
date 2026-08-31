/**
 * @file lv_nuttx_button.h
 *
 */

/*********************
 *      INCLUDES
 *********************/

#ifndef LV_NUTTX_BUTTON_H
#define LV_NUTTX_BUTTON_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include "../../indev/lv_indev.h"

#if LV_USE_NUTTX

#if LV_USE_NUTTX_BUTTONS

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Initialize keypad indev with the specified NuttX buttons device.
 * @param dev_path      path of button device
 */
lv_indev_t * lv_nuttx_button_create(const char * dev_path);

#endif /* LV_USE_NUTTX_BUTTONS */

#endif /* LV_USE_NUTTX */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LV_NUTTX_BUTTON_H */
