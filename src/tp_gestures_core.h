/*
 * Trackpad gesture engine (pure logic, no Zephyr dependencies so it can be
 * unit-tested on a PC). Input: absolute finger positions from a round
 * Cirque pad, 0..1024 on both axes, ~100 frames/s while touching.
 *
 * Absolute positions jitter by a few units even when the finger is still,
 * so everything here works on a FILTERED position:
 *   - adaptive low-pass: heavy smoothing for tiny changes (noise), none
 *     for real fast movement (no lag)
 *   - rest lock: the cursor doesn't move until the finger has really moved
 *     `rest-threshold` away, and locks again once it stays put
 * Taps use the distance from the touch start (not path length), so jitter
 * can't turn a tap into a move.
 *
 * Output (through tpg_emit_fn):
 *   cursor motion   REL_X / REL_Y deltas
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
    int32_t smoothing_pct;    /* strength of the noise filter for small movements, 0..90 */
    int32_t rest_threshold;   /* pad units the finger must move before the cursor follows */
    /* taps */
    int32_t tap_ms;     /* max touch time for a tap */
    int32_t tap_travel; /* max distance from the touch start (pad units) for a tap */
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
#define TPG_STILL_FRAMES 10 /* frames used to decide the finger has stopped */

struct tpg {
    enum tpg_state state;
    int64_t t0, t_last;
    /* filtered position, FP, user view */
    int32_t fx, fy;
    int32_t sx, sy;         /* touch start (filtered), FP */
    int32_t max_disp;       /* farthest distance from the start, pad units */
    /* rest lock */
    bool moving;
    int32_t ax, ay;         /* where the cursor is anchored while resting, FP */
    int32_t wx, wy;         /* position at the start of the stillness window, FP */
    int32_t wframes;
    /* ring */
    int32_t start_r;
    int32_t last_ang, ang_acc; /* tenths of degrees */
    int32_t wheel_rem;
    int32_t buf_dx, buf_dy;    /* FP motion held back while deciding ring vs cursor */
    /* output remainders, FP */
    int32_t rem_x, rem_y;
    /* velocity, FP pad units per frame */
    int32_t vx, vy;
    /* glide */
    bool gliding;
    int32_t gvx, gvy, grx, gry, gticks;
    /* stats for debugging */
    int32_t frames;
    int32_t min_x, max_x, min_y, max_y; /* raw (user view) extents seen since boot */
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

/* distance in pad units between two FP points */
static inline int32_t tpg_dist(int32_t dxf, int32_t dyf) {
    int32_t dx = dxf / TPG_FP, dy = dyf / TPG_FP;
    return tpg_isqrt(dx * dx + dy * dy);
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

/* radius (pad units) and angle (tenths of degrees) of a filtered position */
static inline void tpg_polar(int32_t fx, int32_t fy, int32_t *r, int32_t *ang) {
    int32_t ux = fx / TPG_FP - 512, uy = fy / TPG_FP - 512;
    *r = tpg_isqrt(ux * ux + uy * uy);
    *ang = tpg_atan2_d10(uy, ux);
}

/* ---- output ---- */

static inline void tpg_cursor(struct tpg *s, const struct tpg_cfg *c, int32_t dxf, int32_t dyf,
                              tpg_emit_fn emit, void *ctx) {
    s->rem_x += dxf * c->cursor_scale_pct / 100;
    s->rem_y += dyf * c->cursor_scale_pct / 100;
    int32_t ox = tpg_take(&s->rem_x), oy = tpg_take(&s->rem_y);
    if (ox || oy) {
        emit(ctx, TPG_OUT_MOVE, ox, oy);
    }
    s->vx = (s->vx + dxf) / 2;
    s->vy = (s->vy + dyf) / 2;
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
    s->min_x = s->min_y = 1024;
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
    if (x < s->min_x) s->min_x = x;
    if (x > s->max_x) s->max_x = x;
    if (y < s->min_y) s->min_y = y;
    if (y > s->max_y) s->max_y = y;

    int32_t xf = x * TPG_FP, yf = y * TPG_FP;

    if (s->state == TPG_IDLE) {
        s->gliding = false;
        s->t0 = s->t_last = now;
        s->fx = s->sx = s->ax = s->wx = xf;
        s->fy = s->sy = s->ay = s->wy = yf;
        s->max_disp = 0;
        s->moving = false;
        s->wframes = 0;
        s->buf_dx = s->buf_dy = 0;
        s->wheel_rem = 0;
        s->rem_x = s->rem_y = 0;
        s->vx = s->vy = 0;
        s->frames = 1;
        tpg_polar(xf, yf, &s->start_r, &s->last_ang);
        s->ang_acc = 0;
        s->state = (c->units_per_turn > 0 && s->start_r >= c->ring_inner) ? TPG_RING_PENDING
                                                                           : TPG_MOVE_PENDING;
        return;
    }
    s->t_last = now;
    s->frames++;

    /* adaptive low-pass: alpha 0.1..1 (of 256) depending on how far the raw
     * position is from the filtered one. Small = noise -> heavy filtering. */
    int32_t jump = tpg_dist(xf - s->fx, yf - s->fy);
    int32_t lo = 256 - c->smoothing_pct * 256 / 100; /* alpha for noise-sized changes */
    int32_t alpha = lo + (256 - lo) * jump / 24;      /* full speed at >= 24 units/frame */
    if (alpha > 256) alpha = 256;
    int32_t nfx = s->fx + (xf - s->fx) * alpha / 256;
    int32_t nfy = s->fy + (yf - s->fy) * alpha / 256;
    int32_t dxf = nfx - s->fx, dyf = nfy - s->fy;
    s->fx = nfx;
    s->fy = nfy;

    int32_t disp = tpg_dist(s->fx - s->sx, s->fy - s->sy);
    if (disp > s->max_disp) s->max_disp = disp;

    int32_t r, ang;
    tpg_polar(s->fx, s->fy, &r, &ang);
    int32_t dang = tpg_wrap_d10(ang - s->last_ang);
    s->last_ang = ang;

    switch (s->state) {
    case TPG_MOVE_PENDING:
    case TPG_MOVE:
        if (s->max_disp > c->tap_travel) {
            s->state = TPG_MOVE;
        }
        /* rest lock: ignore jitter until the finger really moves */
        if (!s->moving) {
            if (tpg_dist(s->fx - s->ax, s->fy - s->ay) <= c->rest_threshold) {
                /* settle the anchor onto the true resting point (the first
                 * sample of a touch is the noisiest) */
                s->ax += (s->fx - s->ax) / 32;
                s->ay += (s->fy - s->ay) / 32;
                s->vx = s->vy = 0;
                break;
            }
            s->moving = true;
            s->wx = s->fx;
            s->wy = s->fy;
            s->wframes = 0;
            dxf = s->fx - s->ax; /* catch up from the anchor, nothing is lost */
            dyf = s->fy - s->ay;
        } else if (++s->wframes >= TPG_STILL_FRAMES) {
            /* stayed within a few units for a while: finger has stopped */
            if (tpg_dist(s->fx - s->wx, s->fy - s->wy) <= c->rest_threshold / 2 + 1) {
                s->moving = false;
                s->ax = s->fx;
                s->ay = s->fy;
            }
            s->wx = s->fx;
            s->wy = s->fy;
            s->wframes = 0;
        }
        tpg_cursor(s, c, dxf, dyf, emit, ctx);
        break;
    case TPG_RING_PENDING: {
        s->buf_dx += dxf;
        s->buf_dy += dyf;
        s->ang_acc += dang;
        int32_t arc = abs(s->ang_acc) * r / 573; /* angle(tenths) * r, 573 = 10*180/pi */
        if (abs(s->ang_acc) >= c->ring_start_deg * 10 && r >= c->ring_inner * 3 / 4) {
            s->state = TPG_RING;
            tpg_wheel(s, c, s->ang_acc, emit, ctx);
        } else if (r < c->ring_inner * 3 / 4 ||
                   (disp > c->tap_travel * 2 && arc < disp / 2)) {
            /* went inward, or moved across rather than around the ring: cursor */
            s->state = s->max_disp > c->tap_travel ? TPG_MOVE : TPG_MOVE_PENDING;
            s->moving = true;
            s->wx = s->fx;
            s->wy = s->fy;
            s->wframes = 0;
            tpg_cursor(s, c, s->buf_dx, s->buf_dy, emit, ctx);
        }
        break;
    }
    case TPG_RING:
        tpg_wheel(s, c, dang, emit, ctx);
        break;
    default:
        break;
    }
}

static inline bool tpg_in_right_zone(const struct tpg *s, const struct tpg_cfg *c) {
    if (c->right_click_clock <= 0 || s->start_r < c->ring_inner) {
        return false;
    }
    int32_t r, ang;
    tpg_polar(s->sx, s->sy, &r, &ang);
    /* clock position -> angle, 12 o'clock = straight up = 270 deg (y down) */
    int32_t zone = (2700 + c->right_click_clock * 300) % 3600;
    return abs(tpg_wrap_d10(ang - zone)) <= c->right_click_width_deg * 10;
}

/* finger lifted; returns true if a glide was started */
static inline bool tpg_lift(struct tpg *s, const struct tpg_cfg *c, tpg_emit_fn emit, void *ctx) {
    bool glide = false;
    bool short_touch = (s->t_last - s->t0) <= c->tap_ms;
    if ((s->state == TPG_MOVE_PENDING || s->state == TPG_RING_PENDING) && short_touch &&
        s->max_disp <= c->tap_travel) {
        int btn = tpg_in_right_zone(s, c) ? 1 : 0;
        emit(ctx, TPG_OUT_BUTTON, btn, 1);
        emit(ctx, TPG_OUT_BUTTON, btn, 0);
    } else if (s->state == TPG_MOVE && s->moving && c->glide_decay > 0 &&
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
