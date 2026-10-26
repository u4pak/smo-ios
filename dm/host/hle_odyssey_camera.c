// Odyssey 1.4.1: swipe turns reach the follow camera without the stick path.
// The game reads camera input only as a stick: a dead zone of 0.3, then a yaw
// buffer that releases 10% per frame and a pitch target the current pitch
// follows at 10% per frame. Offline observation gates retain signed text; the
// callbacks only touch a CameraPoserFollowLimit (exact vptr) and the
// CameraAngleVerticalCtrl that poser owns.
//   Yaw: entering the poser's apply function (main+0xbd2b0) adds the turn to its
//   yaw step (+0x1ec), so the game's own area limits still clamp it; after its
//   last call (main+0xbd42c) the game's step goes back, so the turn is applied
//   exactly once.
//   Pitch: entering the controller's NrvUserCtrl moves target (+0xc4) and
//   current (+0xc0) pitch together, and the auto-pitch reference (+0xd8) with
//   them, inside the same limits the nerve applies to stick input.
#include "dm.h"
#include "odyssey_camera.h"
#include <math.h>
#include <stdatomic.h>

static const char k_build[] = "b424be150a8e7d78701cbe7a439d9ebf";
enum {
    POSER_VTABLE = 0x11c6478, POSER_TYPEINFO = 0x11c65c0, POSER_NAME = 0xeb2070,
    POSER_APPLY_SLOT = 0x11c64b0, POSER_APPLY = 0xbd2b0,
    VERTICAL_NERVE_SLOT = 0x11c6248, VERTICAL_NERVE = 0xb9d50,
};
// Field offsets in the receiving objects.
enum { POSER_VERTICAL = 0x158, POSER_YAW_STEP = 0x1ec,
       PITCH_CURRENT = 0xc0, PITCH_TARGET = 0xc4, PITCH_AUTO = 0xd8, PITCH_PARAM = 0x88, PITCH_LIMIT = 0x90 };

static uintptr_t g_vptr;
static _Atomic int g_ready;
static _Atomic uint64_t g_seen_ns;            // last follow-camera update
static _Atomic uintptr_t g_vertical;          // that poser's pitch controller
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static double g_yaw, g_pitch;                 // queued degrees, game sign
static unsigned g_pitch_age;                  // yaw updates since the pitch controller last ran
static dm_odyssey_camera_stats g_stats;
static _Thread_local uint8_t *t_poser;        // the apply call in progress on this thread
static _Thread_local float t_step, t_written;

static float get_float(const uint8_t *p, unsigned offset) { float v; memcpy(&v, p + offset, sizeof v); return v; }
static void put_float(uint8_t *p, unsigned offset, float v) { memcpy(p + offset, &v, sizeof v); }
static double take(double *queued) {
    double part = fabs(*queued) <= DM_ODYSSEY_CAMERA_FLUSH ? *queued : *queued * DM_ODYSSEY_CAMERA_TAKE;
    *queued -= part;
    return part;
}

int dm_odyssey_camera_live(void) {
    uint64_t seen = atomic_load(&g_seen_ns);
    return atomic_load(&g_ready) && seen && now_ns() - seen <= DM_ODYSSEY_CAMERA_LIVE_NS;
}
int dm_odyssey_camera_turn(double right, double up) {
    if (!isfinite(right) || !isfinite(up)) return 0;
    int live = dm_odyssey_camera_live();
    pthread_mutex_lock(&g_lock);
    if (live) {
        // The game turns the camera with a negated stick X and a negated stick Y.
        g_yaw -= right; g_pitch -= up;
        ++g_stats.turns_queued;
    } else {
        g_yaw = g_pitch = 0;
        ++g_stats.turns_refused;
    }
    pthread_mutex_unlock(&g_lock);
    return live;
}

static void yaw_enter(const uint64_t *regs) {
    uint8_t *p = (uint8_t *)(uintptr_t)regs[0];
    t_poser = NULL;
    if (!p || *(const uintptr_t *)p != g_vptr) return;
    uint64_t now = now_ns(), seen = atomic_exchange(&g_seen_ns, now);
    atomic_store(&g_vertical, *(const uintptr_t *)(p + POSER_VERTICAL));
    pthread_mutex_lock(&g_lock);
    // A turn queued before the camera paused, or a pitch no controller took, is dropped.
    if (!seen || now - seen > DM_ODYSSEY_CAMERA_LIVE_NS) g_yaw = g_pitch = 0;
    if (++g_pitch_age > 2) g_pitch = 0;
    double yaw = take(&g_yaw);
    ++g_stats.yaw_updates; g_stats.yaw_applied += yaw;
    pthread_mutex_unlock(&g_lock);
    if (yaw == 0) return;
    t_step = get_float(p, POSER_YAW_STEP);
    t_written = t_step + (float)yaw;
    if (!isfinite(t_written)) return;
    put_float(p, POSER_YAW_STEP, t_written);
    t_poser = p;
}
static void yaw_exit(const uint64_t *regs) {
    uint8_t *p = t_poser;
    t_poser = NULL;
    // The apply function only reads the step; anything else that wrote it wins.
    if (p && p == (uint8_t *)(uintptr_t)regs[19] && get_float(p, POSER_YAW_STEP) == t_written)
        put_float(p, POSER_YAW_STEP, t_step);
}
// The nerve's limits for stick input (main+0xb8fe4..0xb9068).
static float pitch_limit(const uint8_t *c, float v) {
    const uint8_t *param = *(const uint8_t *const *)(c + PITCH_PARAM), *limit = *(const uint8_t *const *)(c + PITCH_LIMIT);
    float lo = param[0x10] ? fmaxf(get_float(param, 4), -35.f) : -35.f;
    float hi = param[0x10] ? fminf(get_float(param, 8), 85.f) : 85.f;
    if (limit[0] && lo < get_float(limit, 4)) lo = get_float(limit, 4);
    if (limit[0] && hi > get_float(limit, 8)) hi = get_float(limit, 8);
    lo = fmaxf(lo, -87.5f); hi = fminf(hi, 87.5f);
    return v < lo ? lo : v > hi ? hi : v;
}
static void pitch_enter(const uint64_t *regs) {
    uint8_t *c = (uint8_t *)(uintptr_t)regs[0];
    if (!c || (uintptr_t)c != atomic_load(&g_vertical)) return;
    pthread_mutex_lock(&g_lock);
    g_pitch_age = 0;
    double pitch = take(&g_pitch);
    ++g_stats.pitch_updates;
    pthread_mutex_unlock(&g_lock);
    if (pitch == 0) return;
    float target = get_float(c, PITCH_TARGET), moved = pitch_limit(c, target + (float)pitch), delta = moved - target;
    if (!isfinite(moved) || delta == 0) return;
    put_float(c, PITCH_TARGET, moved);
    put_float(c, PITCH_CURRENT, pitch_limit(c, get_float(c, PITCH_CURRENT) + delta));
    put_float(c, PITCH_AUTO, pitch_limit(c, get_float(c, PITCH_AUTO) + delta));
    pthread_mutex_lock(&g_lock); g_stats.pitch_applied += delta; pthread_mutex_unlock(&g_lock);
}

typedef struct { uint32_t offset, gate, words[3]; } camera_gate;
static const camera_gate k_gates[] = {
    {0xbd2b0, OBS_ODYSSEY_CAMERA_YAW_ENTER, {0xfc1c0fe8, 0xa90157f6, 0xa9024ff4}},
    {0xbd42c, OBS_ODYSSEY_CAMERA_YAW_EXIT, {0xbd01c260, 0xa9437bfd, 0xa9424ff4}},
    {0xb8ca0, OBS_ODYSSEY_CAMERA_PITCH, {0xd10143ff, 0x6d0123e9, 0xf90013f5}},
};
static int gate_guard(const module_t *m, const camera_gate *gate) {
    if (!gate_obs_present(gate->gate) || gate->offset > m->text_end ||
        sizeof gate->words > m->text_end - gate->offset) return 0;
    uint32_t branch;
    memcpy(&branch, m->base + gate->offset, sizeof branch);
    int64_t displacement = ((int64_t)(int32_t)(branch << 6)) >> 4;
    int64_t target = (int64_t)gate->offset + displacement;
    return (branch & 0xfc000000u) == 0x14000000u && target >= m->patch_off &&
        target >= 0 && (uint64_t)target < m->size &&
        !memcmp(m->base + gate->offset + 4, gate->words + 1, sizeof gate->words - 4);
}
void dm_odyssey_camera_install(void) {
    const char *option = getenv("DM_ODYSSEY_TOUCH_CAMERA");
    if ((option && strcmp(option, "1")) || atomic_load(&g_ready)) return;
    module_t *m = NULL;
    for (int i = 0; i < g_nmod; i++) if (!strcmp(g_mod[i].name, "main")) { m = &g_mod[i]; break; }
    if (!m || strcmp(m->build_id, k_build) || !m->base || m->size <= POSER_TYPEINFO + 0x10) return;
    const uint8_t *b = m->base;
    static const char name[] = "22CameraPoserFollowLimit";
    static const uint32_t nerve[] = {0xf9400020, 0x17fffbd3};   // ldr x0, [x1]; b NrvUserCtrl's exe (main+0xb8ca0)
    if (memcmp(b + POSER_NAME, name, sizeof name) ||
        *(const uintptr_t *)(b + POSER_TYPEINFO + 8) != (uintptr_t)(b + POSER_NAME) ||
        *(const uintptr_t *)(b + POSER_VTABLE + 8) != (uintptr_t)(b + POSER_TYPEINFO) ||
        *(const uintptr_t *)(b + POSER_APPLY_SLOT) != (uintptr_t)(b + POSER_APPLY) ||
        *(const uintptr_t *)(b + VERTICAL_NERVE_SLOT) != (uintptr_t)(b + VERTICAL_NERVE) ||
        memcmp(b + VERTICAL_NERVE, nerve, sizeof nerve)) {
        LOG("[RED] Odyssey touch camera: follow camera layout guard failed; swipes keep the stick");
        return;
    }
    for (unsigned i = 0; i < sizeof k_gates / sizeof *k_gates; i++) if (!gate_guard(m, &k_gates[i])) {
        LOG("[RED] Odyssey touch camera: observation guard failed; swipes keep the stick");
        return;
    }
    g_vptr = (uintptr_t)(b + POSER_VTABLE + 0x10);
    gate_obs_set(OBS_ODYSSEY_CAMERA_YAW_ENTER, yaw_enter);
    gate_obs_set(OBS_ODYSSEY_CAMERA_YAW_EXIT, yaw_exit);
    gate_obs_set(OBS_ODYSSEY_CAMERA_PITCH, pitch_enter);
    atomic_store(&g_ready, 1);
    LOG("Odyssey touch camera: exact 1.4.1 follow-camera yaw/pitch adapters installed");
}
void dm_odyssey_camera_snapshot(dm_odyssey_camera_stats *out) {
    if (!out) return;
    pthread_mutex_lock(&g_lock); *out = g_stats; pthread_mutex_unlock(&g_lock);
    out->installed = atomic_load(&g_ready);
}
