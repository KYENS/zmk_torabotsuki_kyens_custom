/*
 * ZMK input processor wrapping the trackpad gesture engine
 * (tp_gestures_core.h). The Cirque driver runs in absolute mode and sends
 * ABS_X / ABS_Y while a finger is down. This processor consumes those and
 * produces cursor motion, circular-edge scrolling, tap clicks (left, or
 * right on the right-click edge zone) and glide.
 *
 * It must be FIRST in every input-processors list that receives the
 * trackpad. Output is injected back into the same input device using
 * marker codes (REL_RX/RY, REL_RZ, BTN_8/9) that this processor turns into
 * REL_X/Y, REL_WHEEL and BTN_0/1, so later processors (acceleration,
 * scroll mapping) see ordinary pointer events.
 */

#define DT_DRV_COMPAT zmk_input_processor_tp_gestures

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/logging/log.h>
#include <drivers/input_processor.h>

#include "tp_gestures_core.h"

LOG_MODULE_REGISTER(tp_gestures, CONFIG_ZMK_LOG_LEVEL);

struct tp_gestures_config {
    struct tpg_cfg core;
    uint16_t lift_ms;
    uint16_t glide_tick_ms;
};

struct tp_gestures_data {
    const struct tp_gestures_config *cfg;
    const struct device *src;
    struct k_mutex lock;
    struct tpg st;
    int32_t pending_x;
    bool have_x;
    struct k_work_delayable lift_work;
    struct k_work_delayable glide_work;
};

static void emit(void *ctx, enum tpg_out kind, int32_t a, int32_t b) {
    struct tp_gestures_data *data = ctx;
    const struct device *src = data->src;
    if (!src) {
        return;
    }
    switch (kind) {
    case TPG_OUT_MOVE:
        input_report_rel(src, INPUT_REL_RX, a, false, K_NO_WAIT);
        input_report_rel(src, INPUT_REL_RY, b, true, K_NO_WAIT);
        break;
    case TPG_OUT_WHEEL:
        input_report_rel(src, INPUT_REL_RZ, a, true, K_NO_WAIT);
        break;
    case TPG_OUT_BUTTON:
        input_report_key(src, INPUT_BTN_8 + a, b, true, K_NO_WAIT);
        break;
    }
}

static void glide_work_cb(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct tp_gestures_data *data = CONTAINER_OF(dwork, struct tp_gestures_data, glide_work);
    k_mutex_lock(&data->lock, K_FOREVER);
    bool more = tpg_glide_tick(&data->st, &data->cfg->core, emit, data);
    k_mutex_unlock(&data->lock);
    if (more) {
        k_work_reschedule(&data->glide_work, K_MSEC(data->cfg->glide_tick_ms));
    }
}

static void lift_work_cb(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct tp_gestures_data *data = CONTAINER_OF(dwork, struct tp_gestures_data, lift_work);
    k_mutex_lock(&data->lock, K_FOREVER);
    bool glide = data->st.state != TPG_IDLE && tpg_lift(&data->st, &data->cfg->core, emit, data);
    k_mutex_unlock(&data->lock);
    if (glide) {
        k_work_reschedule(&data->glide_work, K_MSEC(data->cfg->glide_tick_ms));
    }
}

static int tp_gestures_handle_event(const struct device *dev, struct input_event *event,
                                    uint32_t param1, uint32_t param2,
                                    struct zmk_input_processor_state *state) {
    struct tp_gestures_data *data = dev->data;

    /* our own output coming back round: turn markers into real codes */
    if (event->type == INPUT_EV_REL) {
        switch (event->code) {
        case INPUT_REL_RX:
            event->code = INPUT_REL_X;
            return ZMK_INPUT_PROC_CONTINUE;
        case INPUT_REL_RY:
            event->code = INPUT_REL_Y;
            return ZMK_INPUT_PROC_CONTINUE;
        case INPUT_REL_RZ:
            event->code = INPUT_REL_WHEEL;
            return ZMK_INPUT_PROC_CONTINUE;
        default:
            return ZMK_INPUT_PROC_CONTINUE;
        }
    }
    if (event->type == INPUT_EV_KEY) {
        if (event->code == INPUT_BTN_8 || event->code == INPUT_BTN_9) {
            event->code = INPUT_BTN_0 + (event->code - INPUT_BTN_8);
        }
        return ZMK_INPUT_PROC_CONTINUE;
    }

    if (event->type != INPUT_EV_ABS) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    /* absolute frame from the trackpad */
    data->src = event->dev;
    if (event->code == INPUT_ABS_X) {
        data->pending_x = event->value;
        data->have_x = true;
        return ZMK_INPUT_PROC_STOP;
    }
    if (event->code == INPUT_ABS_Y && data->have_x) {
        data->have_x = false;
        k_work_cancel_delayable(&data->glide_work);
        k_mutex_lock(&data->lock, K_FOREVER);
        tpg_frame(&data->st, &data->cfg->core, data->pending_x, event->value, k_uptime_get(), emit,
                  data);
        k_mutex_unlock(&data->lock);
        /* no frame for lift-ms = the finger has left the pad */
        k_work_reschedule(&data->lift_work, K_MSEC(data->cfg->lift_ms));
    }
    return ZMK_INPUT_PROC_STOP;
}

static int tp_gestures_init(const struct device *dev) {
    struct tp_gestures_data *data = dev->data;
    data->cfg = dev->config;
    k_mutex_init(&data->lock);
    tpg_init(&data->st);
    k_work_init_delayable(&data->lift_work, lift_work_cb);
    k_work_init_delayable(&data->glide_work, glide_work_cb);
    return 0;
}

static const struct zmk_input_processor_driver_api tp_gestures_api = {
    .handle_event = tp_gestures_handle_event,
};

#define TPG_INST(n)                                                                                \
    static struct tp_gestures_data tpg_data_##n;                                                   \
    static const struct tp_gestures_config tpg_config_##n = {                                      \
        .core =                                                                                    \
            {                                                                                      \
                .invert_x = DT_INST_PROP(n, invert_x),                                             \
                .invert_y = DT_INST_PROP(n, invert_y),                                             \
                .cursor_scale_pct = DT_INST_PROP(n, cursor_scale),                                 \
                .smoothing_pct = DT_INST_PROP(n, smoothing),                                       \
                .tap_ms = DT_INST_PROP(n, tap_ms),                                                 \
                .tap_travel = DT_INST_PROP(n, tap_travel),                                         \
                .right_click_clock = DT_INST_PROP(n, right_click_clock),                           \
                .right_click_width_deg = DT_INST_PROP(n, right_click_width_deg),                   \
                .ring_inner = DT_INST_PROP(n, circle_ring_inner),                                  \
                .ring_start_deg = DT_INST_PROP(n, circle_start_deg),                               \
                .units_per_turn = DT_INST_PROP(n, circle_units_per_turn),                          \
                .circle_invert = DT_INST_PROP(n, circle_invert),                                   \
                .glide_decay = DT_INST_PROP(n, glide_decay),                                       \
                .glide_min_speed = DT_INST_PROP(n, glide_min_speed),                               \
                .glide_stop_speed = DT_INST_PROP(n, glide_stop_speed),                             \
                .glide_max_ticks = DT_INST_PROP(n, glide_max_ms) / DT_INST_PROP(n, glide_tick_ms), \
            },                                                                                     \
        .lift_ms = DT_INST_PROP(n, lift_ms),                                                       \
        .glide_tick_ms = DT_INST_PROP(n, glide_tick_ms),                                           \
    };                                                                                             \
    DEVICE_DT_INST_DEFINE(n, tp_gestures_init, NULL, &tpg_data_##n, &tpg_config_##n, POST_KERNEL,  \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &tp_gestures_api);

DT_INST_FOREACH_STATUS_OKAY(TPG_INST)
