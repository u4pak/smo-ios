// Synthetic follow-camera objects exercise the actual guarded touch-camera adapter.
#include <assert.h>
#include <math.h>
#include "../hle_odyssey_camera.c"

module_t g_mod[MAX_MOD];
int g_nmod;
static void (*callbacks[OBS_MAX])(const uint64_t *);
static int have[OBS_MAX];
static uint64_t clock_ns = UINT64_C(1000000000);
void gate_obs_set(int id, void (*callback)(const uint64_t *)) { callbacks[id] = callback; }
int gate_obs_present(int id) { return have[id]; }
uint64_t now_ns(void) { return clock_ns; }

static _Alignas(8) uint8_t poser[0x300], other[0x300], vertical[0x100], param[0x20], limit[0x10];
static uint64_t regs[31];
static void put_pointer(uint8_t *p, unsigned offset, const void *value) { uintptr_t v = (uintptr_t)value; memcpy(p + offset, &v, sizeof v); }
static int near(double a, double b) { return fabs(a - b) < .0001; }
// One camera frame the way the game runs it: the poser applies its yaw step
// (entry, the store after its last call), and the pitch nerve runs.
static float frame(uint8_t *object, float game_step) {
    clock_ns += UINT64_C(16666667);
    put_float(object, POSER_YAW_STEP, game_step);
    regs[0] = (uintptr_t)object; callbacks[OBS_ODYSSEY_CAMERA_YAW_ENTER](regs);
    float applied = get_float(object, POSER_YAW_STEP);
    regs[19] = (uintptr_t)object; callbacks[OBS_ODYSSEY_CAMERA_YAW_EXIT](regs);
    assert(get_float(object, POSER_YAW_STEP) == game_step);   // the game's own step survives every frame
    regs[0] = (uintptr_t)vertical; callbacks[OBS_ODYSSEY_CAMERA_PITCH](regs);
    return applied - game_step;
}
static void fixture(module_t *m) {
    uint8_t *b = m->base;
    memset(b, 0, m->size);
    memcpy(b + POSER_NAME, "22CameraPoserFollowLimit", 25);
    put_pointer(b, POSER_TYPEINFO + 8, b + POSER_NAME);
    put_pointer(b, POSER_VTABLE + 8, b + POSER_TYPEINFO);
    put_pointer(b, POSER_APPLY_SLOT, b + POSER_APPLY);
    put_pointer(b, VERTICAL_NERVE_SLOT, b + VERTICAL_NERVE);
    static const uint32_t thunk[] = {0xf9400020, 0x17fffbd3};
    memcpy(b + VERTICAL_NERVE, thunk, sizeof thunk);
    for (unsigned i = 0; i < sizeof k_gates / sizeof *k_gates; i++) {
        const camera_gate *p = &k_gates[i];
        uint32_t branch = 0x14000000u | ((uint32_t)(m->patch_off - p->offset) >> 2);
        memcpy(b + p->offset, &branch, 4);
        memcpy(b + p->offset + 4, p->words + 1, sizeof p->words - 4);
        have[p->gate] = 1;
    }
}
int main(void) {
    module_t *m = g_mod; g_nmod = 1;
    strcpy(m->name, "main"); strcpy(m->build_id, k_build);
    m->size = 0x1500000; m->text_end = 0xa2c000; m->data_off = 0x119a000; m->patch_off = 0x14c0000;
    m->base = calloc(1, m->size); assert(m->base);
    uint8_t *b = m->base;
    // Nothing installed: every swipe stays on the stick.
    assert(!dm_odyssey_camera_live() && !dm_odyssey_camera_turn(5, 0));
    // The opt-out, a damaged gate, a moved vtable slot and another build all fail closed.
    fixture(m); setenv("DM_ODYSSEY_TOUCH_CAMERA", "0", 1); dm_odyssey_camera_install(); assert(!atomic_load(&g_ready));
    unsetenv("DM_ODYSSEY_TOUCH_CAMERA");
    b[0xbd2b0 + 4] ^= 1; dm_odyssey_camera_install(); assert(!atomic_load(&g_ready)); b[0xbd2b0 + 4] ^= 1;
    put_pointer(b, POSER_APPLY_SLOT, b + 0xbd2b4); dm_odyssey_camera_install(); assert(!atomic_load(&g_ready));
    fixture(m); have[OBS_ODYSSEY_CAMERA_PITCH] = 0; dm_odyssey_camera_install(); assert(!atomic_load(&g_ready));
    fixture(m); m->build_id[0] = '0'; dm_odyssey_camera_install(); assert(!atomic_load(&g_ready)); m->build_id[0] = 'b';
    dm_odyssey_camera_install(); assert(atomic_load(&g_ready));
    // Installed but no follow camera has run yet: refused.
    assert(!dm_odyssey_camera_live() && !dm_odyssey_camera_turn(5, 0));

    put_pointer(poser, 0, b + POSER_VTABLE + 0x10); put_pointer(poser, POSER_VERTICAL, vertical);
    put_pointer(other, 0, b + POSER_VTABLE + 0x18); put_pointer(other, POSER_VERTICAL, vertical);
    put_pointer(vertical, PITCH_PARAM, param); put_pointer(vertical, PITCH_LIMIT, limit);
    put_float(vertical, PITCH_TARGET, 20); put_float(vertical, PITCH_CURRENT, 18); put_float(vertical, PITCH_AUTO, 10);
    assert(frame(poser, 0) == 0 && dm_odyssey_camera_live());

    // Swipe right: the yaw step goes negative like the stick's, and the whole turn
    // lands within a few frames, most of it in the first, with no game smoothing.
    assert(dm_odyssey_camera_turn(10, 0));
    float first = frame(poser, .5f), total = first;
    assert(near(first, -10 * DM_ODYSSEY_CAMERA_TAKE));
    for (int i = 0; i < 8; i++) total += frame(poser, .5f);
    assert(near(total, -10) && frame(poser, .5f) == 0);
    // Swipe left and up: positive yaw, pitch target and current move together
    // (keeping the game's own 10% chase intact) and the auto reference follows.
    assert(dm_odyssey_camera_turn(-4, 6));
    for (int i = 0; i < 12; i++) frame(poser, 0);
    assert(near(get_float(vertical, PITCH_TARGET), 14) && near(get_float(vertical, PITCH_CURRENT), 12) &&
           near(get_float(vertical, PITCH_AUTO), 4));
    // Pitch stops at the nerve's limits: the default floor of -35 degrees, then a
    // parameter ceiling, then an area limit that is tighter still.
    assert(dm_odyssey_camera_turn(0, 100));
    for (int i = 0; i < 12; i++) frame(poser, 0);
    assert(get_float(vertical, PITCH_TARGET) == -35 && get_float(vertical, PITCH_CURRENT) == -35);
    param[0x10] = 1; put_float(param, 4, -50); put_float(param, 8, 40);
    assert(dm_odyssey_camera_turn(0, -200));
    for (int i = 0; i < 12; i++) frame(poser, 0);
    assert(get_float(vertical, PITCH_TARGET) == 40);
    limit[0] = 1; put_float(limit, 4, -10); put_float(limit, 8, 30);
    assert(dm_odyssey_camera_turn(0, -1));
    for (int i = 0; i < 12; i++) frame(poser, 0);
    assert(get_float(vertical, PITCH_TARGET) == 30 && get_float(vertical, PITCH_CURRENT) <= 30);
    // A step the game rewrote mid-call is the game's: left alone.
    assert(dm_odyssey_camera_turn(3, 0));
    clock_ns += UINT64_C(16666667);
    put_float(poser, POSER_YAW_STEP, 1); regs[0] = (uintptr_t)poser; callbacks[OBS_ODYSSEY_CAMERA_YAW_ENTER](regs);
    put_float(poser, POSER_YAW_STEP, 7); regs[19] = (uintptr_t)poser; callbacks[OBS_ODYSSEY_CAMERA_YAW_EXIT](regs);
    assert(get_float(poser, POSER_YAW_STEP) == 7);
    for (int i = 0; i < 12; i++) frame(poser, 0);
    // Another poser class is never touched, and its updates do not keep the camera live.
    uint8_t before[sizeof other]; memcpy(before, other, sizeof before);
    assert(dm_odyssey_camera_turn(5, 5));
    clock_ns += DM_ODYSSEY_CAMERA_LIVE_NS + 1;
    regs[0] = (uintptr_t)other; callbacks[OBS_ODYSSEY_CAMERA_YAW_ENTER](regs);
    regs[19] = (uintptr_t)other; callbacks[OBS_ODYSSEY_CAMERA_YAW_EXIT](regs);
    assert(!memcmp(before, other, sizeof before) && !dm_odyssey_camera_live());
    // Once the follow camera stops (another camera, a pause), turns are refused and
    // anything still queued is dropped instead of jumping when it comes back.
    assert(!dm_odyssey_camera_turn(5, 0));
    float pitch = get_float(vertical, PITCH_TARGET);
    assert(frame(poser, 0) == 0 && get_float(vertical, PITCH_TARGET) == pitch);
    // A pitch controller that is not this poser's, or one that stopped running, takes nothing.
    assert(dm_odyssey_camera_turn(0, 5));
    put_pointer(poser, POSER_VERTICAL, other);
    for (int i = 0; i < 6; i++) frame(poser, 0);
    assert(get_float(vertical, PITCH_TARGET) == pitch);
    put_pointer(poser, POSER_VERTICAL, vertical);
    for (int i = 0; i < 6; i++) frame(poser, 0);
    assert(get_float(vertical, PITCH_TARGET) == pitch);
    assert(!dm_odyssey_camera_turn(NAN, 0) && !dm_odyssey_camera_turn(0, INFINITY));
    dm_odyssey_camera_stats stats; dm_odyssey_camera_snapshot(&stats);
    assert(stats.installed && stats.yaw_updates > 60 && stats.pitch_updates > 60 && stats.turns_queued == 8 && stats.turns_refused == 3);
    free(b);
    puts("PASS: touch camera: fail-closed install, direct yaw applied once, pitch inside the nerve's limits, exact poser scope, stale turns dropped");
    return 0;
}
