// Synthetic pane trees and synthetic game objects drive the actual tap UI:
// guarded installation, the published targets and screen classes, each hooked
// screen taking only its own tap with the game's steps (recorded here), the
// one-shot result gates (decide, key repeat, the map's stick, the wardrobe's
// steps, footer actions), the cutscene, moped and pad-picture gates (by the
// caller's return address) and the footer.
#include <assert.h>
#include <stdarg.h>
#include "../hle_odyssey_ui.c"

module_t g_mod[MAX_MOD];
int g_nmod;
static uint64_t clock_ns = UINT64_C(1000000000);
static int hud_installed = 1, have[OBS_MAX];
static void (*gates[OBS_MAX])(uint64_t *);
static void (*observers[OBS_MAX])(const uint64_t *);
uint64_t now_ns(void) { return clock_ns; }
void dm_odyssey_display_hud_snapshot(dm_odyssey_display_hud_stats *out) { memset(out, 0, sizeof *out); out->installed = hud_installed; }
int gate_obs_present(int id) { return have[id]; }
void gate_ret_set(int id, void (*fn)(uint64_t *)) { gates[id] = fn; }
void gate_obs_set(int id, void (*fn)(const uint64_t *)) { observers[id] = fn; }

// ---- Panes ----
enum { PANE = 0xd8 };
typedef struct { _Alignas(8) uint8_t p[PANE]; } pane_t;
static void put_pointer(uint8_t *p, unsigned offset, const void *value) { uintptr_t v = (uintptr_t)value; memcpy(p + offset, &v, sizeof v); }
static void put_float(uint8_t *p, unsigned offset, float v) { memcpy(p + offset, &v, sizeof v); }
static uint8_t *pane(pane_t *storage, uint8_t *parent, const char *name, float x, float y, float w, float h) {
    uint8_t *p = storage->p;
    memset(p, 0, PANE);
    put_pointer(p, PANE_CHILDREN, p + PANE_CHILDREN); put_pointer(p, PANE_FIRST, p + PANE_CHILDREN);
    put_float(p, PANE_SIZE, w); put_float(p, PANE_SIZE + 4, h);
    put_float(p, PANE_MTX, 1); put_float(p, PANE_MTX + 0x14, 1); put_float(p, PANE_MTX + 0x28, 1);
    put_float(p, PANE_MTX + 0xc, x); put_float(p, PANE_MTX + 0x1c, y);
    p[PANE_FLAGS] = 1; p[PANE_GLOBAL_ALPHA] = 255;
    strcpy((char *)p + PANE_NAME, name);
    if (parent) {   // append to the intrusive child list
        uintptr_t last = pointer_at(parent, PANE_CHILDREN);
        put_pointer((uint8_t *)last, 8, p + 8);
        put_pointer(p, 8, (void *)last); put_pointer(p, PANE_NEXT, parent + PANE_CHILDREN);
        put_pointer(parent, PANE_CHILDREN, p + 8);
        put_pointer(p, PANE_PARENT, parent);
    }
    return p;
}
static double nx(float x) { return .5 + x / 1280; }
static double ny(float y) { return .5 - y / 720; }

enum { LAYOUT_ACTION = 0x10 };   // IUseLayoutAction, as action_of passes it
// ---- Layout actors: IUseLayout at +8 whose getLayoutKeeper leads to a root ----
typedef struct { _Alignas(8) uint8_t a[0x300]; uint8_t keeper[0x20], layout[0x20]; } actor_t;
static void *fake_keeper(void *iu) { actor_t *a = (actor_t *)((uint8_t *)iu - 8); return a->keeper; }
static void *fake_layout_vtable[2] = {NULL, (void *)fake_keeper};
static void actor(actor_t *a, uint8_t *root) {
    memset(a, 0, sizeof *a);
    put_pointer(a->a, 8, fake_layout_vtable);
    put_pointer(a->keeper, 0x10, a->layout); put_pointer(a->layout, 0x18, root);
}
static int row_of_action(void *action, actor_t *rows, int n) { for (int i = 0; i < n; i++) if (action == rows[i].a + LAYOUT_ACTION) return i; return -1; }

// ---- The game's functions, recorded ----
static char log_text[1024];
static void record(const char *f, ...) { char line[96]; va_list a; va_start(a, f); vsnprintf(line, sizeof line, f, a); va_end(a); strncat(log_text, line, sizeof log_text - strlen(log_text) - 1); }
static actor_t rows_a[8], world_rows[17];
static void fake_action(void *action, const char *name, const char *pane_name) {
    int r = row_of_action(action, rows_a, 8), w = row_of_action(action, world_rows, 17);
    assert(!pane_name); record("%s%c%d ", name, w >= 0 ? 'w' : 'r', w >= 0 ? w : r);
}
static void fake_reaction(void *layout, const char *name, const char *pane_name) { (void)layout; assert(!pane_name && !strcmp(name, "\xe6\xb1\xba\xe5\xae\x9a")); record("decide "); }
static const void *nerve_set;
static void fake_set_nerve(void *user, const void *nerve) { (void)user; nerve_set = nerve; record("nerve "); }
static void fake_mario(void *p) { (void)p; record("mario "); }
static void fake_left(void *list) { (void)list; record("left "); }
static void fake_right(void *list) { (void)list; record("right "); }
static void fake_world_text(void *row, const char *pane_name, uint32_t id) { assert(!strcmp(pane_name, "TxtContent")); record("text%d:%u ", row_of_action((uint8_t *)row + LAYOUT_ACTION, world_rows, 17), id); }
static float icon_at[4][2];
static actor_t icon_actors[4];
static ui_vec2 fake_local(void *iu) { for (int i = 0; i < 4; i++) if (iu == icon_actors[i].a + 8) return (ui_vec2){icon_at[i][0], icon_at[i][1]}; return (ui_vec2){1e6f, 1e6f}; }
static ui_vec2 fake_pane(void *iu, const char *name) { (void)iu; assert(!strcmp(name, "SelectPos")); return (ui_vec2){0, 10}; }
static float fake_min(void) { return 1; }
static float fake_max(void) { return 3; }
static void fake_select(void *map, void *arg) { (void)map; (void)arg; record("select "); }
static void fake_unselect(void *map, void *arg) { (void)map; (void)arg; record("unselect "); }
static float mode_cursor_y[2];
static actor_t mode_parts[2];
static void fake_trans_3d(ui_vec3 *out, void *iu, const char *name) {
    assert(!strcmp(name, "Cursor")); *out = (ui_vec3){0, iu == mode_parts[0].a + 8 ? mode_cursor_y[0] : mode_cursor_y[1], 0};
}
static void fake_mode_refresh(void *state, int animate) { (void)state; assert(!animate); record("refresh "); }
static int intent_asked(int gate) { uint64_t regs[40] = {0}; gates[gate](regs); return regs[16] && regs[0] == 1; }
static int stick_calls;
static ui_vec2 stick_seen;
static void fake_original(const void *nerve, void **keeper) {
    (void)nerve; (void)keeper; record("exe ");
    // An exe that asks for the left stick (as the map's sub_49D050 does) sees the drag.
    uint64_t regs[100] = {0}; gates[OBS_ODYSSEY_UI_LEFT_STICK](regs);
    if (regs[16]) { ++stick_calls; memcpy(&stick_seen.x, &regs[32], 4); memcpy(&stick_seen.y, &regs[34], 4); }
}
static int decide_asked(void) { uint64_t regs[40] = {0}; gates[OBS_ODYSSEY_UI_DECIDE](regs); return regs[16] && regs[0] == 1; }
static int repeat_asked(int down, void *ctrl) {
    uint64_t regs[40] = {0}; regs[0] = (uintptr_t)ctrl; gates[down ? OBS_ODYSSEY_UI_REPEAT_DOWN : OBS_ODYSSEY_UI_REPEAT_UP](regs);
    return regs[16] && regs[0] == 1;
}
static void update(int hook, void *object) {   // the nerve keeper calls the execute slot
    void *keeper = object;
    nerve_execute_fn slot = *(nerve_execute_fn *)(g_mod[0].base + k_hooks[hook].slot);
    log_text[0] = 0; nerve_set = NULL; stick_calls = 0;
    slot(g_base + 0x100, &keeper);
}
static void put_i32(uint8_t *p, unsigned offset, int32_t v) { memcpy(p + offset, &v, 4); }
static void tick(void) { clock_ns += UINT64_C(16666667); }

static void fixture(module_t *m) {
    uint8_t *b = m->base;
    memset(b, 0, m->size);
    for (unsigned i = 0; i < H_COUNT; i++) {
        const ui_hook *h = &k_hooks[i];
        uint8_t *name = b + 0xf00000 + 128 * i; strcpy((char *)name, h->name);
        put_pointer(b, h->slot, b + h->exec); put_pointer(b, h->slot - 8, b + h->typeinfo); put_pointer(b, h->typeinfo + 8, name);
        memcpy(b + h->exec, h->words, sizeof h->words);
    }
    for (unsigned i = 0; i < sizeof k_code / sizeof *k_code; i++) k_code[i].hash = fnv(b + k_code[i].offset, 64);
    put_pointer(b, MENU_DECIDE_PARTS, b + 0x120dc08); put_pointer(b, MAP_ADSORB, b + 0x12cb978);
    strcpy((char *)b + STR_WAIT, "Wait"); strcpy((char *)b + STR_SELECT, "Select"); strcpy((char *)b + STR_DECIDE, "\xe6\xb1\xba\xe5\xae\x9a");
    strcpy((char *)b + STR_SELECT_POS, "SelectPos"); strcpy((char *)b + STR_TXT_CONTENT, "TxtContent"); strcpy((char *)b + STR_CURSOR, "Cursor");
    static const int32_t title[6] = {0, 1, 2, 5, 6, 3}, pause[6] = {0, 1, 4, 5, 5, 6};
    memcpy(b + MENU_MAIN_TABLE, title, sizeof title); memcpy(b + MENU_PAUSE_TABLE, pause, sizeof pause);
    for (int g = OBS_ODYSSEY_UI_DECIDE; g <= OBS_ODYSSEY_UI_TRIGGER_LEFT; g++) have[g] = 1;
}

int main(void) {
    module_t *m = g_mod; g_nmod = 1;
    strcpy(m->name, "main"); strcpy(m->build_id, k_build);
    m->size = 0x1360000; m->text_end = 0xa2c000; m->data_off = 0x119a000;
    m->base = calloc(1, m->size); assert(m->base);
    uint8_t *b = m->base;
    // Fail closed: the opt-out, a changed function, a missing result gate, a moved slot, another build.
    fixture(m); setenv("DM_ODYSSEY_TAP_UI", "0", 1); dm_odyssey_ui_install(); assert(!atomic_load(&g_hooked));
    unsetenv("DM_ODYSSEY_TAP_UI");
    b[0x1c4790 + 7] ^= 1; dm_odyssey_ui_install(); assert(!atomic_load(&g_hooked));
    fixture(m); have[OBS_ODYSSEY_UI_LEFT_STICK] = 0; dm_odyssey_ui_install(); assert(!atomic_load(&g_hooked));
    fixture(m); have[OBS_ODYSSEY_UI_RIGHT_STICK] = 0; dm_odyssey_ui_install(); assert(!atomic_load(&g_hooked));
    fixture(m); b[0x6a4780 + 3] ^= 1; dm_odyssey_ui_install(); assert(!atomic_load(&g_hooked));   // the pad-picture draw moved
    fixture(m); put_pointer(b, k_hooks[H_MAP_WAIT].slot, b + 0x49de70); dm_odyssey_ui_install(); assert(!atomic_load(&g_hooked));
    fixture(m); m->build_id[0] = '0'; dm_odyssey_ui_install(); assert(!atomic_load(&g_hooked)); m->build_id[0] = 'b';
    assert(!dm_odyssey_ui_available() && !dm_odyssey_ui_tap(.2, .4));
    dm_odyssey_ui_install(); assert(atomic_load(&g_hooked) && dm_odyssey_ui_available());
    for (unsigned i = 0; i < H_COUNT; i++) {
        assert(*(nerve_execute_fn *)(b + k_hooks[i].slot) == k_replacements[i]);
        assert((uintptr_t)g_original[i] == (uintptr_t)(b + k_hooks[i].exec));
        g_original[i] = fake_original;
    }
    g_text_lo = 0; g_text_hi = UINTPTR_MAX;   // the fake keeper is host code
    g_fn.start_action = fake_action; g_fn.hit_reaction = fake_reaction; g_fn.set_nerve = fake_set_nerve; g_fn.menu_mario = fake_mario;
    g_fn.list_left = fake_left; g_fn.list_right = fake_right; g_fn.world_text = fake_world_text; g_fn.local_trans = fake_local;
    g_fn.pane_trans = fake_pane; g_fn.map_scale_min = fake_min; g_fn.map_scale_max = fake_max;
    g_fn.map_select = fake_select; g_fn.map_unselect = fake_unselect;
    g_fn.pane_trans_3d = fake_trans_3d; g_fn.mode_refresh = fake_mode_refresh;
    assert(dm_odyssey_ui_screen() == DM_UI_UNKNOWN && !dm_odyssey_ui_back_available() && !dm_odyssey_ui_back());

    // ---- Options: a CommonVerticalList showing item top + row - 1 ----
    static pane_t o_root, o_all, o_area, o_rows[5];
    uint8_t *oroot = pane(&o_root, NULL, "RootPane", 0, 0, 1280, 720), *oall = pane(&o_all, oroot, "All", 0, 0, 30, 40);
    uint8_t *area = pane(&o_area, oall, "ListArea", 0, -24, 760, 580);
    for (int i = 0; i < 5; i++) { char n[16]; snprintf(n, sizeof n, "ParList%02d", i); pane(&o_rows[i], area, n, 0, 295 - 54.f * i, 490, 50); }
    o_rows[0].p[PANE_GLOBAL_ALPHA] = 0;   // the hidden scroll row above the area
    dm_odyssey_ui_observe("OptionSelect", oroot);
    assert(dm_odyssey_ui_screen() == DM_UI_MENU && !dm_odyssey_ui_hit(nx(0), ny(295)) && dm_odyssey_ui_hit(nx(0), ny(241)));
    // Back answers the screen's next cancel or close check once (whichever it asks), then nothing.
    assert(dm_odyssey_ui_back_available() && dm_odyssey_ui_back());
    assert(intent_asked(OBS_ODYSSEY_UI_LIST_CLOSE) && !intent_asked(OBS_ODYSSEY_UI_CANCEL) && !intent_asked(OBS_ODYSSEY_UI_MAP_CLOSE));
    assert(dm_odyssey_ui_back()); clock_ns += DM_UI_ARM_NS + 1; assert(!intent_asked(OBS_ODYSSEY_UI_CANCEL));   // expires
    assert(dm_odyssey_ui_back() && !intent_asked(OBS_ODYSSEY_UI_PAUSE) && intent_asked(OBS_ODYSSEY_UI_CANCEL));
    static actor_t olayout; actor(&olayout, oroot);
    static _Alignas(8) uint8_t list[0x100]; static uint8_t enabled[8] = {1, 1, 0, 1, 1, 1, 1, 1};
    memset(list, 0, sizeof list); put_pointer(list, LIST_LAYOUT, olayout.a); put_i32(list, LIST_COUNT, 6); put_i32(list, LIST_TOP, 1);
    put_pointer(list, LIST_ENABLED, enabled);
    update(H_LIST_ACTIVE, list); assert(!strcmp(log_text, "exe ") && !decide_asked());   // no tap: untouched
    assert(dm_odyssey_ui_tap(nx(0), ny(295 - 54 * 3)));   // row 3 → item 3
    update(H_LIST_ACTIVE, list);
    assert(int_at(list, LIST_CURSOR) == 3 && int_at(list, LIST_PREV_CURSOR) == 0 && !strcmp(log_text, "exe "));
    assert(decide_asked() && !decide_asked());   // once
    assert(dm_odyssey_ui_tap(nx(0), ny(295 - 54 * 2))); update(H_LIST_ACTIVE, list);   // item 2 is disabled
    assert(int_at(list, LIST_CURSOR) == 3 && !decide_asked());
    put_i32(list, LIST_MOVING, 3); assert(dm_odyssey_ui_tap(nx(0), ny(241))); update(H_LIST_ACTIVE, list);   // mid-move: ignored
    assert(int_at(list, LIST_CURSOR) == 3); put_i32(list, LIST_MOVING, 0);
    // A config row (value rolls): the tapped side steps the value; no decide.
    static uint8_t rolls[8]; put_pointer(list, LIST_ROLLS, rolls);
    assert(dm_odyssey_ui_tap(nx(-100), ny(241))); update(H_LIST_ACTIVE, list);
    assert(int_at(list, LIST_CURSOR) == 1 && !strcmp(log_text, "left exe ") && !decide_asked());
    assert(dm_odyssey_ui_tap(nx(100), ny(241))); update(H_LIST_ACTIVE, list); assert(!strcmp(log_text, "right exe "));
    put_pointer(list, LIST_ROLLS, NULL);
    // A tap is only taken by the list whose layout it hit, and only while fresh.
    static actor_t other_layout; static pane_t stray_root; actor(&other_layout, pane(&stray_root, NULL, "RootPane", 0, 0, 1, 1));
    put_pointer(list, LIST_LAYOUT, other_layout.a);
    assert(dm_odyssey_ui_tap(nx(0), ny(241))); update(H_LIST_ACTIVE, list); assert(int_at(list, LIST_CURSOR) == 1 && !decide_asked());
    put_pointer(list, LIST_LAYOUT, olayout.a); update(H_LIST_ACTIVE, list); assert(decide_asked());   // still fresh: the right list takes it
    assert(dm_odyssey_ui_tap(nx(0), ny(241))); clock_ns += DM_UI_TAP_NS + 1; update(H_LIST_ACTIVE, list); assert(!decide_asked());

    // ---- WindowConfirm: one row short, then exeWait's Down step ----
    tick();
    static pane_t w_root, w_all, w_win, w_rows[3];
    uint8_t *wroot = pane(&w_root, NULL, "RootPane", 0, 0, 1280, 720), *wall = pane(&w_all, wroot, "All", 0, 0, 30, 40);
    uint8_t *win = pane(&w_win, wall, "Window", 0, 0, 30, 40);
    for (int i = 0; i < 3; i++) { char n[16]; snprintf(n, sizeof n, "ParList%02d", i); pane(&w_rows[i], win, n, 0, -150 - 58.f * i, 624, 42); }
    w_rows[2].p[PANE_FLAGS] = 0;
    dm_odyssey_ui_observe("WindowConfirm", wroot);
    static actor_t window; actor(&window, wroot);
    for (int i = 0; i < 3; i++) actor(&rows_a[i], NULL);
    static uint8_t *wrows[3]; for (int i = 0; i < 3; i++) wrows[i] = rows_a[i].a;
    put_i32(window.a, WINDOW_COUNT, 2); put_i32(window.a, WINDOW_ROWS_SIZE, 2); put_pointer(window.a, WINDOW_ROWS, wrows); put_i32(window.a, WINDOW_COOLDOWN, -1);
    assert(!dm_odyssey_ui_hit(nx(0), ny(-266)));   // the hidden third row
    assert(dm_odyssey_ui_tap(nx(0), ny(-208))); update(H_WINDOW_WAIT, window.a);
    assert(!strcmp(log_text, "Waitr0 exe ") && int_at(window.a, WINDOW_CURSOR) == 0 && int_at(window.a, WINDOW_DIRECTION) == 2 && decide_asked());
    put_i32(window.a, WINDOW_CURSOR, 1); put_i32(window.a, WINDOW_DIRECTION, 0);   // as exeWait leaves it
    assert(dm_odyssey_ui_tap(nx(0), ny(-208))); update(H_WINDOW_WAIT, window.a);   // the selected row: decide only
    assert(!strcmp(log_text, "exe ") && int_at(window.a, WINDOW_DIRECTION) == 0 && decide_asked());

    // ---- Dialogue: tap anywhere continues; a choice is CommonSelectParts ----
    tick();
    static pane_t t_root, t_all, t_msg, t_sel, t_rows[4];
    uint8_t *troot = pane(&t_root, NULL, "RootPane", 0, 0, 1280, 720), *tall = pane(&t_all, troot, "All", 0, 0, 30, 40);
    uint8_t *msg = pane(&t_msg, tall, "Message", 0, -230, 30, 40), *sel = pane(&t_sel, msg, "Select", 240, -54, 30, 40);
    for (int i = 0; i < 4; i++) { char n[16]; snprintf(n, sizeof n, "ParList%02d", i); pane(&t_rows[i], sel, n, 240, -238 - 46.f * i, 605, 79); t_rows[i].p[PANE_FLAGS] = 0; }
    dm_odyssey_ui_observe("TalkMessage", troot);
    static actor_t talk; actor(&talk, troot);
    assert(dm_odyssey_ui_tap(.1, .1)); update(H_TALK_TEXT, talk.a); assert(decide_asked());
    assert(dm_odyssey_ui_tap(.9, .9)); update(H_TALK_ICON_WAIT, talk.a); assert(decide_asked());
    t_rows[0].p[PANE_FLAGS] = t_rows[1].p[PANE_FLAGS] = 1; tick(); dm_odyssey_ui_observe("TalkMessage", troot);
    static _Alignas(8) uint8_t choice[0x60]; static uint8_t *crows[2] = {rows_a[0].a, rows_a[1].a};
    memset(choice, 0, sizeof choice); put_pointer(choice, CHOICE_LAYOUT, talk.a); put_i32(choice, CHOICE_COUNT, 2); put_pointer(choice, CHOICE_ROWS, crows);
    assert(dm_odyssey_ui_tap(nx(240), ny(-284)));
    update(H_TALK_ICON_WAIT, talk.a); assert(!decide_asked());   // a choice tap does not continue the text
    update(H_CHOICE_SELECT, choice);
    assert(!strcmp(log_text, "Waitr0 Selectr1 exe ") && int_at(choice, CHOICE_CURSOR) == 1 && decide_asked());

    // ---- Title menu: MenuSelectParts ----
    tick();
    static pane_t m_root, m_all, m_list, m_pos, m_rows[7], m_cursor;
    uint8_t *mroot = pane(&m_root, NULL, "RootPane", 0, 0, 1280, 720), *mall = pane(&m_all, mroot, "All", 0, 0, 30, 40);
    uint8_t *mlist = pane(&m_list, mall, "List", -590, -29, 30, 40), *mpos = pane(&m_pos, mlist, "ListPos", -540, -29, 30, 40);
    static const float row_y[7] = {25, -29, -83, -133, -167, -133, -133};
    for (int i = 0; i < 7; i++) { char n[16]; snprintf(n, sizeof n, "ParList%02d", i); pane(&m_rows[i], mpos, n, -540, row_y[i], 988, 40); }
    m_rows[3].p[PANE_FLAGS] = m_rows[4].p[PANE_FLAGS] = m_rows[6].p[PANE_FLAGS] = 0;
    pane(&m_cursor, mall, "ParCursor", -600, 25, 86, 44);
    dm_odyssey_ui_observe("Menu", mroot);
    static actor_t mlayout; actor(&mlayout, mroot);
    for (int i = 0; i < 7; i++) actor(&rows_a[i < 8 ? i : 0], NULL);
    static uint8_t *mrows[7]; for (int i = 0; i < 7; i++) mrows[i] = rows_a[i].a;
    static _Alignas(8) uint8_t parts[0x60]; memset(parts, 0, sizeof parts);
    put_pointer(parts, MENU_LAYOUT, mlayout.a); put_i32(parts, MENU_AMOUNT, 4); put_i32(parts, MENU_MAX, 7); put_pointer(parts, MENU_ARRAY, mrows); parts[MENU_MAIN] = 1;
    assert(dm_odyssey_ui_tap(nx(-700), ny(-133))); update(H_MENU_SELECT, parts);   // Options
    assert(!strcmp(log_text, "Waitr0 Selectr5 mario decide nerve exe ") && int_at(parts, MENU_CURSOR) == 3 && nerve_set == b + MENU_DECIDE_PARTS);
    m_cursor.p[PANE_FLAGS] = 0; tick(); dm_odyssey_ui_observe("Menu", mroot);   // not selecting: no targets
    assert(!dm_odyssey_ui_hit(nx(-700), ny(-133)));

    // ---- Kingdom select: a kingdom short, then the Wait nerve's own step ----
    tick();
    static pane_t k_root, k_all, k_world, k_pos, k_slots[17], k_parts[17];
    uint8_t *kroot = pane(&k_root, NULL, "RootPane", 0, 0, 1280, 720), *kall = pane(&k_all, kroot, "All", 0, 0, 30, 40);
    uint8_t *kworld = pane(&k_world, kall, "World", 0, -230, 30, 40), *kpos = pane(&k_pos, kworld, "Pos", 536, -230, 30, 40);
    for (int i = 0; i < 17; i++) {
        char s1[16], s2[16]; snprintf(s1, sizeof s1, "World%02d", i + 1); snprintf(s2, sizeof s2, "ParWorld%02d", i + 1);
        uint8_t *slot = pane(&k_slots[i], kpos, s1, -400 + 76.f * i, -230, 30, 40);
        pane(&k_parts[i], slot, s2, -400 + 76.f * i, -230, 396, 635);
        if (i >= 4) slot[PANE_FLAGS] = 0;   // four kingdoms reached
    }
    dm_odyssey_ui_observe("WorldSelect", kroot);
    static actor_t klayout; actor(&klayout, kroot);
    for (int i = 0; i < 17; i++) actor(&world_rows[i], NULL);
    static uint8_t *krows[17]; for (int i = 0; i < 17; i++) krows[i] = world_rows[i].a;
    static _Alignas(8) uint8_t state[0x160], repeat[0x20]; static uint32_t ids[17] = {0, 1, 2, 3};
    memset(state, 0, sizeof state); put_pointer(state, WORLD_LAYOUT, klayout.a); put_pointer(state, WORLD_ROWS, krows);
    put_pointer(state, WORLD_REPEAT, repeat); put_i32(state, WORLD_COUNT, 4); put_i32(state, WORLD_CURSOR, 1); put_pointer(state, WORLD_IDS, ids);
    assert(dm_odyssey_ui_hit(nx(-400 + 76 * 3 + 20), ny(-230)) == K_WORLD * 32 + 4 && !dm_odyssey_ui_hit(nx(-400 + 76 * 3), ny(0)));
    assert(dm_odyssey_ui_tap(nx(-400 + 76 * 3), ny(-200))); update(H_WORLD_WAIT, state);
    assert(!strcmp(log_text, "Waitw1 text1:1 exe ") && int_at(state, WORLD_CURSOR) == 2);
    assert(!repeat_asked(0, repeat) && !repeat_asked(1, parts) && repeat_asked(1, repeat) && !repeat_asked(1, repeat));
    put_i32(state, WORLD_CURSOR, 3); tick();
    assert(dm_odyssey_ui_tap(nx(-400), ny(-230))); update(H_WORLD_WAIT, state);   // the first kingdom, no wrap: from above
    assert(int_at(state, WORLD_CURSOR) == 1 && repeat_asked(0, repeat));
    put_i32(state, WORLD_CURSOR, 0); tick();
    assert(dm_odyssey_ui_tap(nx(-400), ny(-230))); update(H_WORLD_WAIT, state); assert(decide_asked());   // the selected one: go

    // ---- The map: drag through the stick, tap an icon to snap, tap again to use it ----
    tick();
    static pane_t a_root; uint8_t *aroot = pane(&a_root, NULL, "RootPane", 0, 0, 1280, 720);
    put_float(aroot, PANE_MTX, .5f);   // a prepared root scale: map-local units are wider
    dm_odyssey_ui_observe("Map", aroot);
    static actor_t maplayout; actor(&maplayout, aroot);
    static _Alignas(8) uint8_t entries[4][48], objects[4][16];
    for (int i = 0; i < 4; i++) {
        actor(&icon_actors[i], NULL); put_pointer(objects[i], 8, icon_actors[i].a); put_pointer(entries[i], 0, objects[i]);
        entries[i][8] = 1; uint32_t type = i == 3 ? 1 : 0; memcpy(entries[i] + 12, &type, 4);   // type 1 never adsorbs
        icon_at[i][0] = 200.f * i; icon_at[i][1] = 0;
    }
    put_pointer(maplayout.a, MAP_ICONS, entries); put_i32(maplayout.a, MAP_ICON_COUNT, 4); put_float(maplayout.a, MAP_ZOOM, 2);
    static _Alignas(8) uint8_t mstate[0x80]; memset(mstate, 0, sizeof mstate); put_pointer(mstate, MAP_LAYOUT, maplayout.a);
    update(H_MAP_WAIT, mstate); assert(!stick_calls);   // no drag: the stick is the player's
    dm_odyssey_ui_drag(2 / 1280.0, 0, 0); update(H_MAP_WAIT, mstate);
    // The map follows: 2 layout units at root scale .5 = 4 map units, at the drag gain, over the
    // speed at zoom 2 of 1..3 (3.6): a stick away from the drag.
    assert(stick_calls == 1 && fabsf(stick_seen.x + 4 * DM_UI_MAP_DRAG_GAIN / 3.6f) < .001f && fabsf(stick_seen.y) < .001f);
    dm_odyssey_ui_drag(0, 200 / 720.0, 0); update(H_MAP_WAIT, mstate);   // a fast flick: at most a full stick
    assert(stick_calls == 1 && stick_seen.y == 1);
    update(H_MAP_SCROLL, mstate); assert(stick_calls == 1 && fabsf(stick_seen.y - .0015f) < 1e-6f);   // held still
    dm_odyssey_ui_drag(0, 0, 1); update(H_MAP_SCROLL, mstate); assert(!stick_calls);   // released: the game snaps
    { uint64_t regs[40] = {0}; gates[OBS_ODYSSEY_UI_LEFT_STICK](regs); assert(!regs[16]); }   // outside a map nerve
    tick(); dm_odyssey_ui_observe("Map", aroot);
    assert(dm_odyssey_ui_tap(nx(400 * .5f), ny(5))); update(H_MAP_WAIT, mstate);   // icon 2 (select point 10 up)
    assert(pointer_at(mstate, MAP_ICON) == (uintptr_t)entries[2] && nerve_set == b + MAP_ADSORB && !strcmp(log_text, "nerve exe "));
    assert(dm_odyssey_ui_tap(nx(400 * .5f), ny(5))); update(H_MAP_WAIT_ADSORB, mstate); assert(decide_asked());   // selected: use it
    assert(dm_odyssey_ui_tap(nx(200 * .5f), ny(5))); update(H_MAP_WAIT_ADSORB, mstate);   // another icon while one is selected
    assert(!strcmp(log_text, "unselect nerve exe ") && pointer_at(mstate, MAP_ICON) == (uintptr_t)entries[1]);
    assert(dm_odyssey_ui_tap(nx(600 * .5f), ny(5))); update(H_MAP_WAIT, mstate); assert(!nerve_set);   // a non-adsorbing icon
    assert(dm_odyssey_ui_tap(nx(-300), ny(200))); update(H_MAP_WAIT, mstate); assert(!nerve_set);   // empty map
    assert(dm_odyssey_ui_back() && intent_asked(OBS_ODYSSEY_UI_MAP_CLOSE));   // the map's Back is its close

    // ---- Horizontal lists (shops): the list publishes its row actors; a tap is one step, the selected item decide ----
    tick();
    static pane_t h_root, h_rows[4];
    uint8_t *hroot = pane(&h_root, NULL, "RootPane", 0, 0, 1280, 720);
    static actor_t hlayout, hrow_actors[4]; static uint8_t *hrows[4];
    actor(&hlayout, hroot);
    for (int i = 0; i < 4; i++) {   // parts roots: positioned by the list, oversized bounds as in Shop
        char n[16]; snprintf(n, sizeof n, "ParList%02d", i);
        actor(&hrow_actors[i], pane(&h_rows[i], hroot, n, -570 + 190.f * i, 80, 4010, 4010)); hrows[i] = hrow_actors[i].a;
    }
    static _Alignas(8) uint8_t hlist[0xa0]; memset(hlist, 0, sizeof hlist);
    put_pointer(hlist, HLIST_LAYOUT, hlayout.a); put_pointer(hlist, HLIST_ROWS, hrows); put_i32(hlist, HLIST_ROWS_SIZE, 4); put_i32(hlist, HLIST_SHOWN, 4);
    put_i32(hlist, HLIST_TOP, 2); put_i32(hlist, HLIST_CURSOR, 3); put_i32(hlist, HLIST_COUNT, 9); put_i32(hlist, HLIST_MOVING, -1);
    assert(!dm_odyssey_ui_hit(nx(-570 + 190 * 2), ny(80)));
    update(H_HLIST_ACTIVE, hlist);   // publishes its rows
    assert(dm_odyssey_ui_screen() == DM_UI_MENU && dm_odyssey_ui_back_available());
    assert(dm_odyssey_ui_hit(nx(-570 + 190 * 2 + 80), ny(-60)) == K_HLIST * 32 + 3 && !dm_odyssey_ui_hit(nx(-570 + 190 * 2), ny(320)));
    assert(dm_odyssey_ui_tap(nx(-570 + 190 * 3), ny(80))); update(H_HLIST_ACTIVE, hlist);   // row 3 → item 5: from 4, one step right
    assert(int_at(hlist, HLIST_CURSOR) == 4 && int_at(hlist, HLIST_TOP) == 3 && !decide_asked());
    assert(!intent_asked(OBS_ODYSSEY_UI_REPEAT_LEFT) && intent_asked(OBS_ODYSSEY_UI_REPEAT_RIGHT) && !intent_asked(OBS_ODYSSEY_UI_TRIGGER_RIGHT));
    put_i32(hlist, HLIST_CURSOR, 5); put_i32(hlist, HLIST_TOP, 4); tick(); update(H_HLIST_ACTIVE, hlist);   // as the step leaves it
    assert(dm_odyssey_ui_tap(nx(-570 + 190), ny(80))); update(H_HLIST_ACTIVE, hlist); assert(decide_asked());   // the selected item: buy
    assert(dm_odyssey_ui_tap(nx(-570), ny(80))); update(H_HLIST_ACTIVE, hlist);   // row 0 → item 4: from 5, one step left
    assert(int_at(hlist, HLIST_CURSOR) == 5 && int_at(hlist, HLIST_TOP) == 4 && intent_asked(OBS_ODYSSEY_UI_TRIGGER_LEFT));
    put_i32(hlist, HLIST_MOVING, 3); assert(dm_odyssey_ui_tap(nx(-570 + 190 * 3), ny(80))); update(H_HLIST_ACTIVE, hlist);   // mid-scroll
    assert(int_at(hlist, HLIST_CURSOR) == 5 && !decide_asked() && !intent_asked(OBS_ODYSSEY_UI_REPEAT_RIGHT));
    put_i32(hlist, HLIST_MOVING, -1);
    clock_ns += DM_UI_RECENT_NS + 1;

    // ---- Save data confirmation (WindowConfirmData) ----
    tick();
    static pane_t d_root, d_all, d_win, d_rows[2];
    uint8_t *droot = pane(&d_root, NULL, "RootPane", 0, 0, 1280, 720), *dall = pane(&d_all, droot, "All", 0, 0, 30, 40);
    uint8_t *dwin = pane(&d_win, dall, "WinBase", 0, 0, 30, 40);
    pane(&d_rows[0], dwin, "ParList00", 0, -148, 624, 42); pane(&d_rows[1], dwin, "ParList01", 0, -208, 624, 42);
    dm_odyssey_ui_observe("WindowConfirmData", droot);
    static actor_t dlayout; actor(&dlayout, droot); actor(&rows_a[0], NULL); actor(&rows_a[1], NULL);
    static _Alignas(8) uint8_t data[0x60]; memset(data, 0, sizeof data);
    put_pointer(data, DATA_LAYOUT, dlayout.a); put_pointer(data, DATA_OPTIONS, rows_a[0].a); put_pointer(data, DATA_OPTIONS + 8, rows_a[1].a);
    assert(dm_odyssey_ui_tap(nx(0), ny(-208))); update(H_DATA_WAIT, data);
    assert(!strcmp(log_text, "Waitr0 Selectr1 exe ") && int_at(data, DATA_SELECTION) == 1 && decide_asked());

    // ---- Mode select: the tapped part, by its Cursor pane ----
    tick();
    static pane_t e_root, e_all, e_box, e_modes[2];
    uint8_t *eroot = pane(&e_root, NULL, "RootPane", 0, 0, 1280, 720), *eall = pane(&e_all, eroot, "All", 0, 0, 30, 40);
    uint8_t *ebox = pane(&e_box, eall, "ModeSelect", 0, 0, 30, 40);
    pane(&e_modes[0], ebox, "ParMode00", 0, 70, 990, 483); pane(&e_modes[1], ebox, "ParMode01", 0, -136, 990, 200);
    dm_odyssey_ui_observe("ModeSelect", eroot);
    static actor_t elayout; actor(&elayout, eroot); actor(&mode_parts[0], NULL); actor(&mode_parts[1], NULL);
    mode_cursor_y[0] = -136; mode_cursor_y[1] = 70;   // the "set" part (+0x30) is the lower one here
    static _Alignas(8) uint8_t mode[0x80]; memset(mode, 0, sizeof mode);
    put_pointer(mode, MODE_LAYOUT, elayout.a); put_pointer(mode, MODE_SET_PART, mode_parts[0].a); put_pointer(mode, MODE_CLEAR_PART, mode_parts[1].a);
    assert(dm_odyssey_ui_tap(nx(0), ny(-136))); update(H_MODE_WAIT, mode);
    assert(mode[MODE_FLAG] == 1 && !strcmp(log_text, "refresh exe ") && decide_asked());
    assert(dm_odyssey_ui_tap(nx(0), ny(-136))); update(H_MODE_WAIT, mode); assert(!strcmp(log_text, "exe ") && decide_asked());   // already on it

    // ---- Collection tabs, the cutscene Skip label and info windows: answered without a hook ----
    tick();
    static pane_t c_root, c_all, c_roll, g_root, g_all, g_label, i_root, i_all;
    uint8_t *croot = pane(&c_root, NULL, "RootPane", 0, 0, 1280, 720), *call = pane(&c_all, croot, "All", 0, 0, 30, 40);
    pane(&c_roll, call, "ParRoll", 0, 300, 370, 164);
    dm_odyssey_ui_observe("Map", aroot); dm_odyssey_ui_observe("CollectList", croot);
    assert(dm_odyssey_ui_tap(nx(-100), ny(300)) && intent_asked(OBS_ODYSSEY_UI_TAB_PREV) && !intent_asked(OBS_ODYSSEY_UI_TAB_CHANGE));
    assert(dm_odyssey_ui_tap(nx(100), ny(300)) && !intent_asked(OBS_ODYSSEY_UI_TAB_PREV) && intent_asked(OBS_ODYSSEY_UI_TAB_CHANGE));
    assert(dm_odyssey_ui_tap(nx(100), ny(300)) && intent_asked(OBS_ODYSSEY_UI_TAB_NEXT));
    clock_ns += DM_UI_RECENT_NS + 1;
    uint8_t *groot = pane(&g_root, NULL, "RootPane", 0, 0, 1280, 720), *gall = pane(&g_all, groot, "All", 0, 0, 30, 40);
    pane(&g_label, gall, "PicCaptureUse", 382, 312, 240, 60);
    dm_odyssey_ui_observe("PlayGuideSkip", groot);
    assert(dm_odyssey_ui_screen() == DM_UI_MENU && dm_odyssey_ui_tap(nx(0), ny(0)) && intent_asked(OBS_ODYSSEY_UI_PAUSE));   // the guide: anywhere skips
    assert(dm_odyssey_ui_tap(nx(382), ny(312)) && intent_asked(OBS_ODYSSEY_UI_PAUSE) && !intent_asked(OBS_ODYSSEY_UI_PAUSE));
    assert(!dm_odyssey_ui_back_available());
    clock_ns += DM_UI_RECENT_NS + 1;
    uint8_t *iroot = pane(&i_root, NULL, "RootPane", 0, 0, 1280, 720); pane(&i_all, iroot, "All", 0, -200, 30, 40);
    dm_odyssey_ui_observe("ImportantMessage", iroot);
    assert(dm_odyssey_ui_tap(.3, .3) && decide_asked() && !decide_asked());
    clock_ns += DM_UI_RECENT_NS + 1;
    dm_odyssey_ui_observe("ContinueLoading", iroot); assert(dm_odyssey_ui_screen() == DM_UI_LOADING);

    // ---- Cutscenes: tryStartSkip marks one; a tap anywhere asks for the Skip guide, then only the guide skips ----
    clock_ns += DM_UI_RECENT_NS + 1;
    { uint64_t regs[104] = {0}; observers[OBS_ODYSSEY_UI_DEMO](regs); }
    assert(dm_odyssey_ui_screen() == DM_UI_MENU && !dm_odyssey_ui_back_available());
    assert(dm_odyssey_ui_tap(.5, .5) && intent_asked(OBS_ODYSSEY_UI_PAUSE) && !intent_asked(OBS_ODYSSEY_UI_PAUSE));
    tick(); { uint64_t regs[104] = {0}; observers[OBS_ODYSSEY_UI_DEMO](regs); } dm_odyssey_ui_observe("PlayGuideSkip", groot);
    assert(dm_odyssey_ui_tap(.5, .5) && intent_asked(OBS_ODYSSEY_UI_PAUSE));   // the guide is up: a tap anywhere skips
    assert(dm_odyssey_ui_tap(nx(382), ny(312)) && intent_asked(OBS_ODYSSEY_UI_PAUSE));
    clock_ns += DM_UI_RECENT_NS + 1; assert(dm_odyssey_ui_screen() != DM_UI_MENU);

    // ---- Race results: a tap continues ----
    static pane_t r_root, r_all; uint8_t *rroot = pane(&r_root, NULL, "RootPane", 0, 0, 1280, 720); pane(&r_all, rroot, "All", 0, 0, 30, 40);
    dm_odyssey_ui_observe("MiniGameRaceResult", rroot);
    assert(dm_odyssey_ui_screen() == DM_UI_MENU && dm_odyssey_ui_tap(.7, .2) && decide_asked() && !decide_asked());
    clock_ns += DM_UI_RECENT_NS + 1;

    // ---- The wardrobe: outfits (list 0) and caps (list 1), the key repeat answers one step ----
    static pane_t wb_root, wb_rows[11], wh_root, wh_rows[11];
    uint8_t *wbroot = pane(&wb_root, NULL, "RootPane", 0, 0, 1280, 720), *whroot = pane(&wh_root, NULL, "RootPane", 0, 0, 1280, 720);
    static actor_t wblayout, whlayout, wb_actors[11], wh_actors[11]; static uint8_t *wb_list_rows[11], *wh_list_rows[11];
    actor(&wblayout, wbroot); actor(&whlayout, whroot);
    for (int i = 0; i < 11; i++) {
        char n[16]; snprintf(n, sizeof n, "ParList%02d", i);
        actor(&wb_actors[i], pane(&wb_rows[i], wbroot, n, -770 + 154.f * i, -62, 134, 134)); wb_list_rows[i] = wb_actors[i].a;
        actor(&wh_actors[i], pane(&wh_rows[i], whroot, n, -770 + 154.f * i, 104, 134, 190)); wh_list_rows[i] = wh_actors[i].a;
    }
    static _Alignas(8) uint8_t outfits[0xa0], caps[0xa0], closet[0x200], closet_repeat[0x20]; static uint8_t *closet_lists[2] = {outfits, caps};
    memset(closet, 0, sizeof closet); put_pointer(closet, CLOSET_LISTS, closet_lists); put_pointer(closet, CLOSET_REPEAT, closet_repeat);
    for (int l = 0; l < 2; l++) {
        uint8_t *cl = closet_lists[l]; memset(cl, 0, 0xa0);
        put_pointer(cl, HLIST_LAYOUT, l ? whlayout.a : wblayout.a); put_pointer(cl, HLIST_ROWS, l ? wh_list_rows : wb_list_rows);
        put_i32(cl, HLIST_ROWS_SIZE, 11); put_i32(cl, HLIST_SHOWN, 11);
        put_i32(cl, HLIST_COUNT, 20); put_i32(cl, HLIST_CURSOR, 6); put_i32(cl, HLIST_TOP, 1);   // the cursor in slot 5
    }
    update(H_CLOSET_SELECT, closet);   // publishes both lists
    assert(dm_odyssey_ui_screen() == DM_UI_MENU && dm_odyssey_ui_back_available() && dm_odyssey_ui_actions() == DM_UI_ACTION_RANDOM);
    update(H_HLIST_ACTIVE, outfits); assert(!strcmp(log_text, "exe "));   // its own list hook leaves the wardrobe's lists alone
    uint64_t cregs[104];
    #define CLOSET_ASKED(gate, ctrl) (memset(cregs, 0, sizeof cregs), cregs[0] = (uintptr_t)(ctrl), gates[gate](cregs), cregs[16] && cregs[0] == 1)
    assert(dm_odyssey_ui_tap(nx(-770 + 154 * 8), ny(-62))); update(H_CLOSET_SELECT, closet);   // row 8 → item 9
    assert(int_at(outfits, HLIST_CURSOR) == 8 && int_at(outfits, HLIST_TOP) == 3 && !strcmp(log_text, "exe "));
    assert(!CLOSET_ASKED(OBS_ODYSSEY_UI_CLOSET_RIGHT, outfits) && !CLOSET_ASKED(OBS_ODYSSEY_UI_CLOSET_LEFT, closet_repeat));
    assert(CLOSET_ASKED(OBS_ODYSSEY_UI_CLOSET_RIGHT, closet_repeat) && !CLOSET_ASKED(OBS_ODYSSEY_UI_CLOSET_RIGHT, closet_repeat));
    put_i32(outfits, HLIST_CURSOR, 9); put_i32(outfits, HLIST_TOP, 4); tick(); update(H_CLOSET_SELECT, closet);
    assert(dm_odyssey_ui_tap(nx(-770 + 154 * 5), ny(-62))); update(H_CLOSET_SELECT, closet); assert(decide_asked());   // selected: wear it
    tick();
    assert(dm_odyssey_ui_tap(nx(-770 + 154 * 2), ny(-62))); update(H_CLOSET_SELECT, closet);   // row 2 → item 6: from 7, one step left
    assert(int_at(outfits, HLIST_CURSOR) == 7 && int_at(outfits, HLIST_TOP) == 2 && CLOSET_ASKED(OBS_ODYSSEY_UI_CLOSET_LEFT, closet_repeat));
    tick();
    assert(dm_odyssey_ui_tap(nx(-770 + 154 * 7), ny(104))); update(H_CLOSET_SELECT, closet);   // a cap: switch lists first
    assert(int_at(caps, HLIST_CURSOR) == 6 && !CLOSET_ASKED(OBS_ODYSSEY_UI_CLOSET_OUTFITS, closet_repeat) && CLOSET_ASKED(OBS_ODYSSEY_UI_CLOSET_CAPS, closet_repeat));
    put_i32(closet, CLOSET_ACTIVE, 1); tick(); update(H_CLOSET_SELECT, closet);   // switched: the same tap steps onto item 8
    assert(int_at(caps, HLIST_CURSOR) == 7 && int_at(caps, HLIST_TOP) == 2 && CLOSET_ASKED(OBS_ODYSSEY_UI_CLOSET_RIGHT, closet_repeat));
    update(H_CLOSET_SELECT, closet); assert(!CLOSET_ASKED(OBS_ODYSSEY_UI_CLOSET_RIGHT, closet_repeat));   // taken once
    put_i32(closet, CLOSET_ACTIVE, 0); assert(dm_odyssey_ui_tap(nx(-770 + 154 * 2), ny(104)));   // a switch that never happens expires
    update(H_CLOSET_SELECT, closet); clock_ns += DM_UI_TAP_NS + 1; update(H_CLOSET_SELECT, closet);
    assert(int_at(caps, HLIST_CURSOR) == 7 && !CLOSET_ASKED(OBS_ODYSSEY_UI_CLOSET_CAPS, closet_repeat));
    #undef CLOSET_ASKED
    // Footer actions: Random answers the Y check once.
    tick(); update(H_CLOSET_SELECT, closet);
    assert(!intent_asked(OBS_ODYSSEY_UI_Y) && dm_odyssey_ui_action(DM_UI_ACTION_RANDOM) && intent_asked(OBS_ODYSSEY_UI_Y) && !intent_asked(OBS_ODYSSEY_UI_Y));
    assert(!dm_odyssey_ui_action(DM_UI_ACTION_LISTS) && !dm_odyssey_ui_action(DM_UI_ACTION_RESET));
    clock_ns += DM_UI_RECENT_NS + 1;
    dm_odyssey_ui_observe("OptionConfig", oroot); assert(dm_odyssey_ui_actions() == DM_UI_ACTION_RESET);
    assert(dm_odyssey_ui_action(DM_UI_ACTION_RESET) && intent_asked(OBS_ODYSSEY_UI_Y));
    clock_ns += DM_UI_RECENT_NS + 1;
    dm_odyssey_ui_observe("WorldSelect", kroot); assert(dm_odyssey_ui_actions() == DM_UI_ACTION_LISTS);
    assert(dm_odyssey_ui_action(DM_UI_ACTION_LISTS) && intent_asked(OBS_ODYSSEY_UI_TAB_CHANGE));
    clock_ns += DM_UI_RECENT_NS + 1;
    // The map's zoom: held, as the UI right stick, only where the map asks for it.
    dm_odyssey_ui_observe("Map", aroot); assert(dm_odyssey_ui_actions() == (DM_UI_ACTION_LISTS | DM_UI_ACTION_ZOOM));
    {
        uint64_t regs[104] = {0}; regs[101] = (uintptr_t)b + MAP_ZOOM_CALLS_1;
        gates[OBS_ODYSSEY_UI_RIGHT_STICK](regs); assert(!regs[16]);   // not held
        dm_odyssey_ui_zoom(-1); gates[OBS_ODYSSEY_UI_RIGHT_STICK](regs); float zy; memcpy(&zy, &regs[34], 4);
        assert(regs[16] && zy == -DM_UI_MAP_ZOOM_RATE);
        memset(regs, 0, sizeof regs); regs[101] = (uintptr_t)b + 0x482a40; gates[OBS_ODYSSEY_UI_RIGHT_STICK](regs); assert(!regs[16]);   // the wardrobe's turn
        dm_odyssey_ui_zoom(0); regs[101] = (uintptr_t)b + MAP_ZOOM_CALLS_0; gates[OBS_ODYSSEY_UI_RIGHT_STICK](regs); assert(!regs[16]);
    }
    clock_ns += DM_UI_RECENT_NS + 1;

    // ---- Message pad pictures: handheld (the touch variant) for the PadStyle draw only, while touch is in use ----
    {
        uint64_t regs[104] = {0}; regs[101] = (uintptr_t)b + PAD_STYLE_DRAW;
        dm_odyssey_ui_footer(0); gates[OBS_ODYSSEY_UI_PAD_HANDHELD](regs); assert(regs[16] && regs[0] == 1);
        memset(regs, 0, sizeof regs); regs[101] = (uintptr_t)b + 0x51e690; gates[OBS_ODYSSEY_UI_PAD_HANDHELD](regs); assert(!regs[16]);
        regs[101] = (uintptr_t)b + PAD_STYLE_SET; dm_odyssey_ui_footer(0); gates[OBS_ODYSSEY_UI_PAD_HANDHELD](regs); assert(regs[16] && regs[0] == 1);
        memset(regs, 0, sizeof regs); regs[101] = (uintptr_t)b + PAD_STYLE_DRAW; dm_odyssey_ui_footer(1); gates[OBS_ODYSSEY_UI_PAD_HANDHELD](regs); assert(!regs[16]);
        clock_ns += DM_UI_FOOTER_SLIDE_NS;
    }
    // ---- A touch / controller switch rebuilds drawn text boxes (with tags) once, so pad pictures follow ----
    {
        static _Alignas(8) uint8_t box_mem[0x160], plain_mem[0x160]; static pane_t t2_root;
        uint8_t *troot2 = pane(&t2_root, NULL, "RootPane", 0, 0, 1280, 720);
        uint8_t *box = pane((pane_t *)box_mem, troot2, "TxtGuide", 0, 0, 100, 30), *plain = pane((pane_t *)plain_mem, troot2, "TxtOther", 0, 0, 100, 30);
        for (uint8_t *t = box; t; t = t == box ? plain : NULL) {
            put_pointer(t, 0, b + TEXTBOX_VTABLE); *(uint16_t *)(t + TEXTBOX_LENGTH) = 12; t[TEXTBOX_FLAGS] = 0;
        }
        put_pointer(box, TEXTBOX_TAGS, b + 0x100);   // only the box with a tag processor
        clock_ns += DM_UI_TEXT_REFRESH_NS + 1;
        dm_odyssey_ui_observe("PlayGuide", troot2); assert(!(box[TEXTBOX_FLAGS] & 4));   // no switch: untouched
        dm_odyssey_ui_footer(0); dm_odyssey_ui_observe("PlayGuide", troot2);
        assert((box[TEXTBOX_FLAGS] & 4) && !(plain[TEXTBOX_FLAGS] & 4));
        box[TEXTBOX_FLAGS] = 0; dm_odyssey_ui_footer(0); clock_ns += DM_UI_TEXT_REFRESH_NS + 1;
        dm_odyssey_ui_observe("PlayGuide", troot2); assert(!(box[TEXTBOX_FLAGS] & 4));   // only for a while after the switch
        dm_odyssey_ui_footer(1); clock_ns += DM_UI_FOOTER_SLIDE_NS;
    }

    // ---- Screens ----
    static pane_t s_root; uint8_t *sroot = pane(&s_root, NULL, "RootPane", 0, 0, 1280, 720);
    tick(); dm_odyssey_ui_observe("Closet", sroot); assert(dm_odyssey_ui_screen() == DM_UI_UNKNOWN && !dm_odyssey_ui_tap(.5, .5));
    clock_ns += DM_UI_RECENT_NS + 1; dm_odyssey_ui_observe("CounterCoin", sroot); assert(dm_odyssey_ui_screen() == DM_UI_GAME);
    // Riding: the moped's jump check (called from Motorcycle code) with the HUD up.
    {
        uint64_t regs[104] = {0}; regs[101] = (uintptr_t)b + 0x2919a8;
        assert(!dm_odyssey_ui_riding()); observers[OBS_ODYSSEY_UI_RIDE](regs); assert(dm_odyssey_ui_riding());
        clock_ns += DM_UI_RIDE_NS + 1; dm_odyssey_ui_observe("CounterCoin", sroot); assert(!dm_odyssey_ui_riding());
        regs[101] = (uintptr_t)b + 0x2f6794; observers[OBS_ODYSSEY_UI_RIDE](regs); assert(!dm_odyssey_ui_riding());   // another caller
    }
    dm_odyssey_ui_observe("TalkMessage", troot); assert(dm_odyssey_ui_screen() == DM_UI_MENU);   // dialogue during play
    clock_ns += DM_UI_GAME_RECENT_NS + 1; assert(dm_odyssey_ui_screen() == DM_UI_UNKNOWN && !dm_odyssey_ui_tap(.5, .5));
    { uint64_t until = atomic_load(&g_decide_until); assert(!until || clock_ns > until); assert(!decide_asked()); }   // grants expire

    // ---- Footer: shown by default; touch slides it down with an ease ----
    assert(dm_odyssey_ui_footer_drop() == 0);
    dm_odyssey_ui_footer(0); clock_ns += DM_UI_FOOTER_SLIDE_NS / 2;
    assert(fabsf(dm_odyssey_ui_footer_drop() - DM_UI_FOOTER_DROP / 2) < .01f);
    dm_odyssey_ui_footer(1); float back = dm_odyssey_ui_footer_drop(); assert(fabsf(back - DM_UI_FOOTER_DROP / 2) < .01f);   // reverses from there
    clock_ns += DM_UI_FOOTER_SLIDE_NS; assert(dm_odyssey_ui_footer_drop() == 0);
    dm_odyssey_ui_stats stats; dm_odyssey_ui_snapshot(&stats);
    assert(stats.hooked && stats.taps >= 40 && stats.chosen >= 24 && stats.dropped >= 2 && stats.drags >= 1 && stats.steps == 7 && stats.backs == 4 && stats.actions == 3);
    hud_installed = 0; assert(!dm_odyssey_ui_available());
    free(b);
    puts("PASS: tap UI: fail-closed install, targets per layout, options/config, windows, data windows, dialogue and choices, title menu, kingdoms, mode select, shop rows, map drag/snap/use, Back, tabs, Skip, cutscene taps, race results, list steps, wardrobe lists, footer actions and zoom, pad pictures, text rebuild on input switch, riding, info windows, one-shot gates, screens, footer");
    return 0;
}
