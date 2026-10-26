// Canonical portable touch geometry, palette and deliberate settings gesture.
// Derived from Odyssey's accepted DmGamepad layout; units scale with height.
#pragma once
#include <math.h>
#include <stdint.h>

#define DM_TC_VERSION 8
#define DM_TC_ZL_X 44.0
#define DM_TC_ZL_Y 40.0
#define DM_TC_ZL_R 26.0
#define DM_TC_L_X 44.0
#define DM_TC_L_Y 108.0
#define DM_TC_L_R 24.0
#define DM_TC_DROP_TIP 1.45
#define DM_TC_CORNER_IN 0.5
#define DM_TC_FACE_X 81.0
#define DM_TC_PAD_Y 82.0
#define DM_TC_FACE_GAP 50.0
#define DM_TC_FACE_R 27.0
#define DM_TC_PILL_DX 60.0
#define DM_TC_PILL_HIT_PAD 8.0
#define DM_TC_GEAR_GAP 5.0
#define DM_TC_PILL_Y 22.0
#define DM_TC_PILL_W 50.0
#define DM_TC_PILL_H 28.0
#define DM_TC_STICK_X 81.0
#define DM_TC_STICK_R 77.0
#define DM_TC_KNOB_R 30.0
#define DM_TC_STICK_GRAB 1.4
#define DM_TC_FULL 0.8
#define DM_TC_DEAD 0.1
#define DM_TC_LOW_X 231.0
#define DM_TC_LOW_Y 64.0
#define DM_TC_DPAD_R 48.0
#define DM_TC_ARM_W 17.0
#define DM_TC_ARM_IN 11.0
#define DM_TC_DPAD_GRAB 1.3
#define DM_TC_RSWIPE 0.5
// v8: the right half is a swipe-to-look surface in touch mode. Swipes turn
// Odyssey's follow camera directly (hle_odyssey_camera.c), DM_TC_LOOK_DEGREES per
// point at unit 1 times the sensitivity setting, with no stick dead zone or
// smoothing in between. Other cameras read only a stick, so there each swipe's
// distance becomes a turn budget that the right stick drains (v7): every swipe
// turns the camera in proportion to how far it went, however short or fast.
#define DM_TC_LOOK_DEGREES 0.3              // degrees per point (at unit 1) at sensitivity 1
#define DM_TC_LOOK_SENS_MIN 0.25
#define DM_TC_LOOK_SENS_MAX 3.0
#define DM_TC_UI_SLOP 10.0                  // points a tappable-screen touch moves before it is a drag
#define DM_TC_BACK_SIZE 52.0                // points: the Back button's touch square, top left
#define DM_TC_BACK_ALPHA 0.7
#define DM_TC_UNKNOWN_SHOW 2.5              // seconds an unrecognized screen keeps the controls hidden
// On the moped the diamond becomes a big pedal (Y, accelerate) with a smaller
// up-arrow button (B, jump) beside it, inside the diamond's footprint.
#define DM_TC_PEDAL_DX 22.0                 // pedal centre right of the diamond centre (unit 1)
#define DM_TC_PEDAL_W 72.0
#define DM_TC_PEDAL_H 136.0
#define DM_TC_PEDAL_TILT 24.0               // degrees the pedal leans back when pressed
#define DM_TC_JUMP_DX 52.0                  // jump button centre left of / below the diamond centre
#define DM_TC_JUMP_DY 34.0
#define DM_TC_JUMP_R 25.0
// Footer actions (Lists, Reset, Random, zoom) where the hidden controller guide was.
#define DM_TC_ACTION_H 40.0                 // points
#define DM_TC_ACTION_GAP 10.0
#define DM_TC_ACTION_ALPHA 0.72
#define DM_TC_LOOK_SPEED 450.0              // points (at unit 1) the stick drains per second at full deflection
#define DM_TC_LOOK_DRAIN 0.10               // seconds: the remaining budget eases out over this time constant
#define DM_TC_LOOK_FLOOR 0.3                // minimum deflection while turning, past the game's stick dead zone
#define DM_TC_LOOK_DONE 0.5                 // points (at unit 1) of budget treated as spent
#define DM_TC_LOOK_MAX_STEP 0.05            // seconds: longest step, after a stall
#define DM_TC_CLICK_X 102.0
#define DM_TC_CLICK_R 17.0
#define DM_TC_IDLE_ALPHA 0.06
#define DM_TC_PRESSED_ALPHA 0.18
#define DM_TC_STROKE_ALPHA 0.18
#define DM_TC_TEXT_ALPHA 0.32
#define DM_TC_PRESSED_TEXT_ALPHA 0.46
#define DM_TC_RING_ALPHA 0.025
#define DM_TC_SHADOW_ALPHA 0.08
#define DM_TC_GEAR_SIZE 44.0
#define DM_TC_GEAR_SYMBOL "gearshape"
#define DM_TC_GEAR_ICON_SIZE 20.0
#define DM_TC_GEAR_ALPHA 0.38
#define DM_TC_GEAR_REGULAR_WEIGHT 1
#define DM_TC_GEAR_HOLD_NS UINT64_C(450000000)
#define DM_TC_GEAR_SLOP 12.0

typedef struct { double x, y; } dm_tc_point;
typedef struct { double x, y, width, height; } dm_tc_rect;
typedef struct { double top, left, bottom, right; } dm_tc_insets;
typedef struct {
    double unit;
    dm_tc_rect safe, corner, gear;
    dm_tc_point face, left_stick, dpad;
} dm_tc_layout;

static inline dm_tc_layout dm_tc_make_layout(double width, double height, dm_tc_insets i) {
    dm_tc_layout l = {0};
    if (!isfinite(width) || !isfinite(height) || width <= 0 || height <= 0) return l;
    i.top = isfinite(i.top) ? fmax(0, fmin(i.top, height)) : 0;
    i.bottom = isfinite(i.bottom) ? fmax(0, fmin(i.bottom, height-i.top)) : 0;
    i.left = isfinite(i.left) ? fmax(0, fmin(i.left, width)) : 0;
    i.right = isfinite(i.right) ? fmax(0, fmin(i.right, width-i.left)) : 0;
    l.safe = (dm_tc_rect){i.left, i.top, width-i.left-i.right, height-i.top-i.bottom};
    // Narrow/intermediate scenes must not grow controls from height alone.
    // Normal landscape phone/tablet geometry retains the accepted scale.
    l.unit = fmin(fmin(height / 430.0, 1.3), l.safe.width / 600.0);
    l.corner = (dm_tc_rect){i.left*DM_TC_CORNER_IN, i.top,
        width-(i.left+i.right)*DM_TC_CORNER_IN, height-i.top-i.bottom};
    double bottom = l.corner.y+l.corner.height;
    l.face = (dm_tc_point){l.corner.x+l.corner.width-DM_TC_FACE_X*l.unit, bottom-DM_TC_PAD_Y*l.unit};
    l.left_stick = (dm_tc_point){l.corner.x+DM_TC_STICK_X*l.unit, bottom-DM_TC_PAD_Y*l.unit};
    l.dpad = (dm_tc_point){l.corner.x+DM_TC_LOW_X*l.unit, bottom-DM_TC_LOW_Y*l.unit};
    l.gear = (dm_tc_rect){l.safe.x+l.safe.width/2-DM_TC_GEAR_SIZE/2,
        l.safe.y+fmax(DM_TC_PILL_Y*l.unit-DM_TC_GEAR_SIZE/2, 0), DM_TC_GEAR_SIZE, DM_TC_GEAR_SIZE};
    return l;
}
static inline dm_tc_point dm_tc_face_position(dm_tc_layout l, int index) {
    double gap = DM_TC_FACE_GAP*l.unit;
    return (dm_tc_point){l.face.x+(index == 0 ? gap : index == 3 ? -gap : 0),
                         l.face.y+(index == 1 ? gap : index == 2 ? -gap : 0)};
}
static inline dm_tc_point dm_tc_shoulder(dm_tc_layout l, int right, int trigger) {
    return (dm_tc_point){right ? l.safe.x+l.safe.width-(trigger?DM_TC_ZL_X:DM_TC_L_X)*l.unit :
        l.safe.x+(trigger?DM_TC_ZL_X:DM_TC_L_X)*l.unit, l.safe.y+(trigger?DM_TC_ZL_Y:DM_TC_L_Y)*l.unit};
}
static inline dm_tc_point dm_tc_click(dm_tc_layout l, int right) {
    return (dm_tc_point){right ? l.safe.x+l.safe.width-DM_TC_CLICK_X*l.unit : l.safe.x+DM_TC_CLICK_X*l.unit,
        l.safe.y+DM_TC_ZL_Y*l.unit};
}
static inline dm_tc_rect dm_tc_pill(dm_tc_layout l, int plus) {
    double w=DM_TC_PILL_W*l.unit, h=DM_TC_PILL_H*l.unit;
    double dx=fmax(DM_TC_PILL_DX*l.unit, DM_TC_GEAR_SIZE/2+w/2+DM_TC_PILL_HIT_PAD*l.unit+DM_TC_GEAR_GAP);
    return (dm_tc_rect){l.safe.x+l.safe.width/2+(plus?1:-1)*dx-w/2,
        l.safe.y+DM_TC_PILL_Y*l.unit-h/2, w, h};
}
static inline dm_tc_rect dm_tc_pill_hit(dm_tc_layout l, int plus) {
    dm_tc_rect r=dm_tc_pill(l,plus); double pad=DM_TC_PILL_HIT_PAD*l.unit;
    return (dm_tc_rect){r.x-pad,r.y-pad,r.width+2*pad,r.height+2*pad};
}
static inline dm_tc_rect dm_tc_pedal(dm_tc_layout l) {
    double w = DM_TC_PEDAL_W*l.unit, h = DM_TC_PEDAL_H*l.unit;
    return (dm_tc_rect){l.face.x+DM_TC_PEDAL_DX*l.unit-w/2, l.face.y-h/2, w, h};
}
static inline dm_tc_point dm_tc_jump(dm_tc_layout l) {
    return (dm_tc_point){l.face.x-DM_TC_JUMP_DX*l.unit, l.face.y+DM_TC_JUMP_DY*l.unit};
}
static inline int dm_tc_contains(dm_tc_rect r, dm_tc_point p) {
    return p.x >= r.x && p.x < r.x+r.width && p.y >= r.y && p.y < r.y+r.height;
}

typedef struct { double dx, dy; dm_tc_point last; uint64_t tick_ns; unsigned touching; } dm_tc_look;
static inline int dm_tc_look_active(const dm_tc_look *l) { return l->touching || hypot(l->dx, l->dy) >= DM_TC_LOOK_DONE; }
// A new swipe adds to any budget still draining from the previous one.
static inline void dm_tc_look_begin(dm_tc_look *l, dm_tc_point p, uint64_t now) {
    if (!isfinite(p.x) || !isfinite(p.y)) return;
    if (!dm_tc_look_active(l)) { l->dx = l->dy = 0; l->tick_ns = now; }
    l->last = p; l->touching = 1;
}
static inline double dm_tc_look_sensitivity(double s) {
    return isfinite(s) && s > 0 ? fmin(fmax(s, DM_TC_LOOK_SENS_MIN), DM_TC_LOOK_SENS_MAX) : 1;
}
// The distance (points at unit 1) since the last sample, down positive; 0 when
// there is nothing to take. The caller sends it to the camera or to the budget.
static inline int dm_tc_look_take(dm_tc_look *l, dm_tc_point p, double unit, double *dx, double *dy) {
    *dx = *dy = 0;
    if (!l->touching || !isfinite(p.x) || !isfinite(p.y) || !(unit > 0)) return 0;
    *dx = (p.x - l->last.x) / unit; *dy = (p.y - l->last.y) / unit; l->last = p;
    return 1;
}
static inline void dm_tc_look_move(dm_tc_look *l, dm_tc_point p, double unit) {
    double dx, dy;
    if (dm_tc_look_take(l, p, unit, &dx, &dy)) { l->dx += dx; l->dy += dy; }
}
static inline void dm_tc_look_release(dm_tc_look *l) { l->touching = 0; }   // the budget keeps draining
static inline void dm_tc_look_cancel(dm_tc_look *l) { *l = (dm_tc_look){0}; }
// Advance to now and return the right-stick deflection in [-1, 1], up positive.
static inline void dm_tc_look_step(dm_tc_look *l, uint64_t now, double *x, double *y) {
    *x = *y = 0;
    double dt = now > l->tick_ns ? fmin((double)(now - l->tick_ns) / 1e9, DM_TC_LOOK_MAX_STEP) : 0;
    l->tick_ns = now > l->tick_ns ? now : l->tick_ns;
    double left = hypot(l->dx, l->dy);
    if (!isfinite(left) || left < DM_TC_LOOK_DONE) { l->dx = l->dy = 0; return; }
    double out = fmin(fmax(left / (DM_TC_LOOK_SPEED * DM_TC_LOOK_DRAIN), DM_TC_LOOK_FLOOR), 1);
    double used = fmin(out * DM_TC_LOOK_SPEED * dt, left);
    *x = l->dx / left * out; *y = -l->dy / left * out;
    l->dx -= l->dx / left * used; l->dy -= l->dy / left * used;
}

// A gear-owned touch remains consumed until it ends, even after invalidation.
// The host must test the exclusive gear rectangle before ANY gameplay hit test,
// pass only conflicting gear touches as blockers. A resting gameplay thumb
// may stay down; presentation clears owned input. Invalidate on background,
// rotation, controller changes and settings dismissal.
typedef struct {
    uint64_t generation, began_ns;
    dm_tc_point origin;
    unsigned tracking, blocked, opened;
} dm_tc_gear;
static inline void dm_tc_gear_cancel(dm_tc_gear *g) {
    ++g->generation; g->tracking=0; g->blocked=1; g->opened=0;
}
static inline uint64_t dm_tc_gear_begin(dm_tc_gear *g, dm_tc_point p, uint64_t now, unsigned other_touches) {
    ++g->generation; g->origin=p; g->began_ns=now;
    g->tracking=1; g->blocked=!!other_touches || !isfinite(p.x) || !isfinite(p.y); g->opened=0; return g->generation;
}
static inline void dm_tc_gear_block(dm_tc_gear *g) { g->blocked=1; }
static inline void dm_tc_gear_move(dm_tc_gear *g, dm_tc_point p) {
    if (!isfinite(p.x) || !isfinite(p.y) || hypot(p.x-g->origin.x, p.y-g->origin.y)>DM_TC_GEAR_SLOP) g->blocked=1;
}
static inline int dm_tc_gear_ready(dm_tc_gear *g, uint64_t token, uint64_t now, unsigned other_touches) {
    if (other_touches) g->blocked=1;
    if (token != g->generation || !g->tracking || g->blocked || g->opened ||
        now < g->began_ns || now-g->began_ns<DM_TC_GEAR_HOLD_NS) return 0;
    g->opened=1; return 1;
}

// v5: a normal gear tap activates on release. Hosts retain exclusive touch
// ownership, inspect actual/coalesced samples, and cancel on lifecycle changes.
static inline int dm_tc_gear_tap(dm_tc_gear *g,uint64_t token,dm_tc_point released,dm_tc_rect hit) {
    dm_tc_gear_move(g,released);
    if (token!=g->generation||!g->tracking||g->blocked||g->opened||!dm_tc_contains(hit,released)) return 0;
    g->tracking=0;g->opened=1;return 1;
}
