/**
 * @file lv_nuttx_button.c
 *
 */

/*********************
 *      INCLUDES
 *********************/

#include "lv_nuttx_button.h"

#if LV_USE_NUTTX

#if LV_USE_NUTTX_BUTTONS

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdint.h>
#include "../../lvgl_private.h"

#ifdef __NuttX__
    #include <sys/ioctl.h>
    #include <debug.h>
    #include <nuttx/input/buttons.h>
#else
    #include "mock/nuttx_input_buttons.h"
#endif

/**********************
 *      TYPEDEFS
 **********************/

typedef struct {
    int fd;
    btn_buttonset_t supported;
    btn_buttonset_t keymask;
    btn_buttonset_t last_sample;
    bool has_last_sample;
    lv_indev_state_t last_state;
    uint32_t last_key;
} lv_nuttx_button_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void button_read(lv_indev_t * drv, lv_indev_data_t * data);
static void button_delete_cb(lv_event_t * e);
static lv_indev_t * button_init(int fd, btn_buttonset_t supported);

/**********************
 *   STATIC FUNCTIONS
 **********************/

static uint32_t button_default_key(btn_buttonset_t sample,
                                   btn_buttonset_t keymask)
{
    LV_UNUSED(sample);
    LV_UNUSED(keymask);

    /* Use ENTER as the default test mapping so a focused object can emit
     * visible PRESSED/RELEASED/CLICKED style events in LVGL.
     */

    return LV_KEY_ENTER;
}

static bool button_read_sample(int fd, btn_buttonset_t * sample)
{
    ssize_t nbytes = read(fd, sample, sizeof(*sample));

    if(nbytes == sizeof(*sample)) {
        return true;
    }

    if(nbytes == -1 && errno != EAGAIN) {
        LV_LOG_WARN("buttons read failed: %s", strerror(errno));
    }
    else if(nbytes > 0) {
        LV_LOG_WARN("buttons unexpected read size: %d", (int)nbytes);
    }

    return false;
}

static void button_conv_sample(lv_nuttx_button_t * button,
                               lv_indev_data_t * data,
                               btn_buttonset_t sample)
{
    data->key = button_default_key(sample, button->keymask);
    data->state = (sample & button->keymask) != 0 ?
                  LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;

  /* 记住最近一次成功上报给 LVGL 的键值/状态；下一次如果暂时没有新样本，
   * read_cb 仍可返回一个连续、合法的上次状态。
   */

  button->last_key = data->key;
  button->last_state = data->state;
}

static void button_read(lv_indev_t * drv, lv_indev_data_t * data)
{
    lv_nuttx_button_t * button = drv->driver_data;
    btn_buttonset_t sample;

    if(button->has_last_sample) {
        sample = button->last_sample;
        button->has_last_sample = false;
    }
    else {
        if(!button_read_sample(button->fd, &sample)) {
            data->key = button->last_key;
            data->state = button->last_state;
            return;
        }
    }

    button_conv_sample(button, data, sample);

  /* 再预读一条，若队列里还有积压事件，就借助 continue_reading 让
   * LVGL 立刻再次调用 read_cb，把这一批样本更快取空。
   */

    if(button_read_sample(button->fd, &sample)) {
        button->last_sample = sample;
        button->has_last_sample = true;
        data->continue_reading = true;
    }
}

static void button_delete_cb(lv_event_t * e)
{
    lv_indev_t * indev = lv_event_get_user_data(e);
    lv_nuttx_button_t * button = lv_indev_get_driver_data(indev);

    if(button) {
        lv_indev_set_driver_data(indev, NULL);
        lv_indev_set_read_cb(indev, NULL);

        if(button->fd >= 0) {
            close(button->fd);
            button->fd = -1;
        }

        lv_free(button);
        LV_LOG_USER("done");
    }
}

static lv_indev_t * button_init(int fd, btn_buttonset_t supported)
{
    lv_nuttx_button_t * button;
    lv_indev_t * indev;

    button = lv_malloc_zeroed(sizeof(lv_nuttx_button_t));
    LV_ASSERT_MALLOC(button);

    if(button == NULL) {
        return NULL;
    }

    indev = lv_indev_create();
    if(indev == NULL) {
        LV_LOG_ERROR("indev create failed");
        lv_free(button);
        return NULL;
    }

    button->fd = fd;
    button->supported = supported;
    button->keymask = supported & ((~supported) + 1);
    button->last_state = LV_INDEV_STATE_RELEASED;

    /* 还没读到任何真实样本前，用一个稳定的默认键值兜底。 */

    button->last_key = LV_KEY_ENTER;

    lv_indev_set_type(indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(indev, button_read);
    lv_indev_set_driver_data(indev, button);
    lv_indev_add_event_cb(indev, button_delete_cb, LV_EVENT_DELETE, indev);

    return indev;
}

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

lv_indev_t * lv_nuttx_button_create(const char * dev_path)
{
    lv_indev_t * indev;
    struct btn_pollevents_s pollevents;
    btn_buttonset_t supported = 0;
    int fd;
    int ret;

    LV_ASSERT_NULL(dev_path);
    LV_LOG_USER("buttons %s opening", dev_path);

    fd = open(dev_path, O_RDONLY | O_NONBLOCK);
    if(fd < 0) {
        LV_LOG_ERROR("Error: cannot open buttons device");
        return NULL;
    }

    ret = ioctl(fd, BTNIOC_SUPPORTED, (unsigned long)((uintptr_t)&supported));
    if(ret < 0 || supported == 0) {
        LV_LOG_ERROR("Error: cannot query supported buttons");
        close(fd);
        return NULL;
    }

    LV_LOG_USER("buttons %s open success supported=0x%08x",
                dev_path, (unsigned int)supported);

    /* 明确告诉 /dev/buttons：这个 fd 关心全部 press/release 事件，
     * 不依赖 upper-half 的默认值。
     */

    pollevents.bp_press = supported;
    pollevents.bp_release = supported;
    ret = ioctl(fd, BTNIOC_POLLEVENTS,
                (unsigned long)((uintptr_t)&pollevents));
    if(ret < 0) {
        LV_LOG_WARN("buttons set pollevents failed: %s", strerror(errno));
    }

    indev = button_init(fd, supported);
    if(indev == NULL) {
        close(fd);
    }

    return indev;
}

#endif /* LV_USE_NUTTX_BUTTONS */

#endif /* LV_USE_NUTTX */
