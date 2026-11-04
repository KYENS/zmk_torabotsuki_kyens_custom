/*
 * Trackpad gesture engine (pure logic, no Zephyr dependencies so it can be
 * unit-tested on a PC). Input: absolute finger positions from a round
 * Cirque pad, 0..1024 on both axes, ~100 frames/s while touching.
 *
 * Output (through tpg_emit_fn):
 *   cursor motion   REL_X / REL_Y deltas, smoothed, scaled
 *   circular scroll REL_WHEEL, when a touch starts on the edge ring and
 *                   slides around it (clockwise = scroll down)
 *   taps            left click; a tap on the right-click edge zone = right click
 *   glide           cursor keeps moving after a fast flick, then slows down
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

enum tpg_out {
    TPG_OUT_MOVE,   /* a = dx, b = dy */
    TPG_OUT_WHEEL,  /* a = wheel units (+ = up) */
    TPG_OUT_BUTTON, /* a = button (0 left, 1 right), b = 1 press / 0 release */
};

typedef void (*tpg_emit_fn)(void *ctx, enum tpg_out kind, int32_t a, int32_t b);

struct tpg_cfg {
    /* orientation: flip pad axes so that x grows to the user's right and y downward.
     * All output (cursor, glide, zones, circle direction) is in the user's view. */
    bool invert_x, invert_y;
    /* cursor */
    int32_t cursor_scale_pct; /* 100 = 1 output count per pad unit (1024 units across) */
    int32_t smoothing_pct;    /* weight of newest delta, 100 = off */
    /* taps */
    int32_t tap_ms;     /* max touch time for a tap */
    int32_t tap_travel; /* max movement (pad units) for a tap */
    /* right click: tap on the edge ring around this clock position */
    int32_t right_click_clock;     /* 1..12 as seen by the user, 0 = off */
    int32_t right_click_width_deg; /* zone is clock +/- this */
    /* circular scroll */
    int32_t ring_inner;      /* touches starting at least this far from centre (pad units, max 512) */
    int32_t ring_start_deg;  /* sliding this far around the ring starts scrolling */
    int32_t units_per_turn;  /* wheel units per full circle (15 units = 1 notch) */
    bool circle_invert;      /* true: clockwise = scroll up */
    /* glide */
    int32_t glide_decay;     /* per-mille kept per tick, 0 = no glide */
    int32_t glide_min_speed; /* pad units per frame (x10) needed at lift */
    int32_t glide_stop_speed;/* x10 */
    int32_t glide_max_ticks;
};

enum tpg_state { TPG_IDLE, TPG_MOVE_PENDING, TPG_MOVE, TPG_RING_PENDING, TPG_RING };

#define TPG_FP 256

struct tpg {
    enum tpg_state state;
    int64_t t0, t_last;
    int32_t lx, ly;       /* last position */
    int32_t sx, sy;       /* touch start */
    int32_t travel;
    int32_t buf_dx, buf_dy; /* motion held back while deciding ring vs cursor */
    int32_t last_ang;     /* tenths of degrees, user view */
    int32_t ang_acc;
    int32_t wheel_rem;
    /* smoothing */
    int32_t prev_dx, prev_dy, rem_x, rem_y;
    /* velocity, FP pad units per frame */
    int32_t vx, vy;
    /* glide */
    bool gliding;
    int32_t gvx, gvy, grx, gry, gticks;
};

/* ---- helpers ---- */

/* atan2 in tenths of degrees, 0..3599, y down (so increasing = clockwise on screen) */
static inline int32_t tpg_atan2_d10(int32_t y, int32_t x) {
    if (x == 0 && y == 0) {
        return 0;
    }
    int32_t ax = abs(x), ay = abs(y);
    int32_t mx = ax > ay ? ax : ay, mn = ax > ay ? ay : ax;
    int64_t t = ((int64_t)mn << 12) / mx; /* 0..4096 */
    /* atan(t) deg ~= 45t + t(1-t)(14.02 + 3.80t), t in [0,1] */
    int64_t a = 450 * t; /* tenths * 4096 */
    a += t * (4096 - t) / 4096 * (1402 * 4096 + 380 * t) / (4096 * 10);
    int32_t deg = (int32_t)(a / 4096); /* 0..450 tenths */
    if (ay > ax) {
        deg = 900 - deg;
    }
    if (x < 0) {
        deg = 1800 - deg;
    }
    if (y < 0) {
        deg = 3600 - deg;
    }
    return deg % 3600;
}

static inline int32_t tpg_wrap_d10(int32_t d) {
    while (d > 1800) d -= 3600;
    while (d < -1800) d += 3600;
    return d;
}

static inline int32_t tpg_isqrt(int32_t v) {
    int32_t r = 0, b = 1 << 30;
    if (v <= 0) return 0;
    while (b > v) b >>= 2;
    while (b) {
        if (v >= r + b) { v -= r + b; r = (r >> 1) + b; } else { r >>= 1; }
        b >>= 2;
    }
    return r;
}

/* position relative to the centre; x, y are already in the user's view */
static inline void tpg_user_vec(const struct tpg_cfg *c, int32_t x, int32_t y, int32_t *ux, int32_t *uy) {
    (void)c;
    *ux = x - 512;
    *uy = y - 512;
}

static inline int32_t tpg_take(int32_t *acc) {
    int32_t w = *acc / TPG_FP;
    *acc -= w * TPG_FP;
    return w;
}

static inline int32_t tpg_speed(int32_t vx, int32_t vy) {
    int32_t ax = abs(vx), ay = abs(vy);
    return ax > ay ? ax + ay / 2 : ay + ax / 2;
}

/* ---- cursor output ---- */

static inline void tpg_cursor(struct tpg *s, const struct tpg_cfg *c, int32_t dx, int32_t dy,
                              tpg_emit_fn emit, void *ctx) {
    int32_t w = c->smoothing_pct;
    if ((dx > 0 && s->prev_dx < 0) || (dx < 0 && s->prev_dx > 0)) { s->prev_dx = 0; s->rem_x = 0; }
    if ((dy > 0 && s->prev_dy < 0) || (dy < 0 && s->prev_dy > 0)) { s->prev_dy = 0; s->rem_y = 0; }
    int32_t sx = (dx * w + s->prev_dx * (100 - w)) * TPG_FP / 100;
    int32_t sy = (dy * w + s->prev_dy * (100 - w)) * TPG_FP / 100;
    s->prev_dx = dx;
    s->prev_dy = dy;
    s->rem_x += sx * c->cursor_scale_pct / 100;
    s->rem_y += sy * c->cursor_scale_pct / 100;
    int32_t ox = tpg_take(&s->rem_x), oy = tpg_take(&s->rem_y);
    if (ox || oy) {
        emit(ctx, TPG_OUT_MOVE, ox, oy);
    }
    s->vx = (s->vx + dx * TPG_FP) / 2;
    s->vy = (s->vy + dy * TPG_FP) / 2;
}

static inline void tpg_wheel(struct tpg *s, const struct tpg_cfg *c, int32_t dang_d10,
                             tpg_emit_fn emit, void *ctx) {
    /* clockwise (positive angle) = scroll down (negative wheel) */
    int32_t dir = c->circle_invert ? 1 : -1;
    s->wheel_rem += dir * dang_d10 * c->units_per_turn * TPG_FP / 3600;
    int32_t u = tpg_take(&s->wheel_rem);
    if (u) {
        emit(ctx, TPG_OUT_WHEEL, u, 0);
    }
}

/* ---- public API ---- */

static inline void tpg_init(struct tpg *s) {
    *s = (struct tpg){0};
}

/* one absolute frame while the finger is down */
static inline void tpg_frame(struct tpg *s, const struct tpg_cfg *c, int32_t x, int32_t y,
                             int64_t now, tpg_emit_fn emit, void *ctx) {
    /* pad coordinates -> the user's view (x right, y down) */
    if (c->invert_x) {
        x = 1024 - x;
    }
    if (c->invert_y) {
        y = 1024 - y;
    }
    int32_t ux, uy;
    tpg_user_vec(c, x, y, &ux, &uy);
    int32_t r = tpg_isqrt(ux * ux + uy * uy);
    int32_t ang = tpg_atan2_d10(uy, ux);

    if (s->state == TPG_IDLE) {
        s->gliding = false;
        s->t0 = now;
        s->sx = s->lx = x;
        s->sy = s->ly = y;
        s->travel = 0;
        s->buf_dx = s->buf_dy = 0;
        s->ang_acc = 0;
        s->last_ang = ang;
        s->wheel_rem = 0;
        s->prev_dx = s->prev_dy = s->rem_x = s->rem_y = 0;
        s->vx = s->vy = 0;
        s->state = (c->units_per_turn > 0 && r >= c->ring_inner) ? TPG_RING_PENDING : TPG_MOVE_PENDING;
        s->t_last = now;
        return;
    }

    int32_t dx = x - s->lx, dy = y - s->ly;
    s->lx = x;
    s->ly = y;
    s->t_last = now;
    s->travel += abs(dx) + abs(dy);
    int32_t dang = tpg_wrap_d10(ang - s->last_ang);
    s->last_ang = ang;

    switch (s->state) {
    case TPG_MOVE_PENDING:
        if (s->travel > c->tap_travel) {
            s->state = TPG_MOVE;
        }
        /* fall through: cursor follows immediately */
    case TPG_MOVE:
        tpg_cursor(s, c, dx, dy, emit, ctx);
        break;
    case TPG_RING_PENDING:
        s->buf_dx += dx;
        s->buf_dy += dy;
        s->ang_acc += dang;
        if (abs(s->ang_acc) >= c->ring_start_deg * 10 && r >= c->ring_inner * 3 / 4) {
            s->state = TPG_RING;
            tpg_wheel(s, c, s->ang_acc, emit, ctx);
        } else if (r < c->ring_inner * 3 / 4 ||
                   (s->travel > c->tap_travel * 3 &&
                    abs(s->ang_acc) * r / 573 < s->travel / 2)) {
            /* went inward, or moved mostly across rather than around the ring
             * (arc length ~= angle * radius; 573 = 10 * 180 / pi): cursor motion */
            s->state = s->travel > c->tap_travel ? TPG_MOVE : TPG_MOVE_PENDING;
            tpg_cursor(s, c, s->buf_dx, s->buf_dy, emit, ctx);
        }
        break;
    case TPG_RING:
        tpg_wheel(s, c, dang, emit, ctx);
        break;
    default:
        break;
    }
}

static inline bool tpg_in_right_zone(const struct tpg_cfg *c, int32_t x, int32_t y) {
    if (c->right_click_clock <= 0) {
        return false;
    }
    int32_t ux, uy;
    tpg_user_vec(c, x, y, &ux, &uy);
    if (tpg_isqrt(ux * ux + uy * uy) < c->ring_inner) {
        return false;
    }
    /* clock position -> angle, 12 o'clock = straight up = 270 deg (y down) */
    int32_t zone = (2700 + c->right_click_clock * 300) % 3600;
    return abs(tpg_wrap_d10(tpg_atan2_d10(uy, ux) - zone)) <= c->right_click_width_deg * 10;
}

/* finger lifted; returns true if a glide was started */
static inline bool tpg_lift(struct tpg *s, const struct tpg_cfg *c, tpg_emit_fn emit, void *ctx) {
    bool glide = false;
    bool short_touch = (s->t_last - s->t0) <= c->tap_ms;
    if ((s->state == TPG_MOVE_PENDING || s->state == TPG_RING_PENDING) && short_touch &&
        s->travel <= c->tap_travel) {
        int btn = tpg_in_right_zone(c, s->sx, s->sy) ? 1 : 0;
        emit(ctx, TPG_OUT_BUTTON, btn, 1);
        emit(ctx, TPG_OUT_BUTTON, btn, 0);
    } else if (s->state == TPG_MOVE && c->glide_decay > 0 &&
               tpg_speed(s->vx, s->vy) * 10 >= c->glide_min_speed * TPG_FP) {
        s->gliding = true;
        s->gvx = s->vx;
        s->gvy = s->vy;
        s->grx = s->rem_x;
        s->gry = s->rem_y;
        s->gticks = 0;
        glide = true;
    }
    s->state = TPG_IDLE;
    return glide;
}

/* one glide step; returns true while the glide continues */
static inline bool tpg_glide_tick(struct tpg *s, const struct tpg_cfg *c, tpg_emit_fn emit, void *ctx) {
    if (!s->gliding) {
        return false;
    }
    s->gvx = s->gvx * c->glide_decay / 1000;
    s->gvy = s->gvy * c->glide_decay / 1000;
    if (tpg_speed(s->gvx, s->gvy) * 10 < c->glide_stop_speed * TPG_FP ||
        ++s->gticks > c->glide_max_ticks) {
        s->gliding = false;
        return false;
    }
    s->grx += s->gvx * c->cursor_scale_pct / 100;
    s->gry += s->gvy * c->cursor_scale_pct / 100;
    int32_t ox = tpg_take(&s->grx), oy = tpg_take(&s->gry);
    if (ox || oy) {
        emit(ctx, TPG_OUT_MOVE, ox, oy);
    }
    return true;
}
