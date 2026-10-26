// Odyssey 1.4.1 tappable UI.
//   Targets: the HUD adapter's Calculate hook (hle_odyssey_display_hud.c) hands
//   over every primary layout. Known screens publish their visible rows from
//   each pane's global matrix after Calculate, with the layout's root so a tap
//   is only ever taken by the object that owns that layout.
//   Taking a tap: the execute slots of the screens' input nerves (data vtables)
//   run a host step before the original. The step puts the screen's own cursor
//   on the tapped row with the game's functions and fields, then grants exactly
//   what the screen asks for next:
//     rs::isTriggerUiDecide answers true once (result gate, patch_nso.py RETS),
//     KeyRepeatCtrl::isUp / isDown answer true once for the kingdom select's
//     controller, and the UI left stick answers the map drag while a map nerve
//     runs. Nothing else sees those answers; no controller input is simulated.
//   Screens: MenuSelectParts (title/pause), CommonVerticalList (options, moon
//   and music lists; config rows step their value toward the tapped side),
//   CommonHorizontalList (shops, captures, souvenirs), WindowConfirm and
//   WindowConfirmData, CommonSelectParts (dialogue choices, mini-game menus),
//   SceneStateSelectMode (mode select), TalkMessage (tap to continue),
//   StageSceneStateWorldMap (kingdom select) and StageSceneStateStageMap (map:
//   drag moves the selector through the stick path, a tap on an icon snaps to
//   it, a tap on the selected icon uses it). Without a hook: info windows take
//   a tap as decide, the cutscene Skip label as pause, the collection tab bar as
//   previous/next tab, and the front end's Back button as cancel/close.
//   Horizontal lists (shops, the wardrobe's rows) publish their own rows from
//   their row actors. A tap jumps the list one short of the item, keeping the
//   cursor's slot, and answers one right / left step that the list's owner
//   takes itself (scroll, sound, description); a tap on the selected item is
//   decide. The wardrobe (StageSceneStateCloset) answers its own key repeat
//   once in the same way and switches lists first. Race results take a tap as
//   decide. Cutscenes: every cutscene state asks tryStartSkip each frame
//   (observation gate); a tap anywhere answers the pause check once: the first
//   brings up the Skip guide, a tap while it is up skips.
//   Footer actions with no touch control of their own (Lists, Reset, Random,
//   the map's zoom) are answered once (or while held) from front-end buttons.
//   The moped's jump check marks riding; while touch is in use, the message
//   pad pictures pick their handheld variants (the moped's pedal and arrow,
//   "Tap anywhere to Skip"), and on a switch between touch and a controller
//   the text boxes of drawn layouts rebuild once so the pictures follow.
// The screen class also says when gameplay is up, so the front end keeps its
// touch controls off tappable screens; the footer drop hides controller guides.
#include "dm.h"
#include "odyssey_display_hud.h"
#include "odyssey_ui.h"
#include <math.h>
#include <stdatomic.h>

static const char k_build[] = "b424be150a8e7d78701cbe7a439d9ebf";
// nn::ui2d::Pane fields.
enum { PANE_PARENT = 0x18, PANE_CHILDREN = 0x20, PANE_FIRST = 0x28, PANE_NEXT = 0x10, PANE_SIZE = 0x50,
       PANE_FLAGS = 0x58, PANE_GLOBAL_ALPHA = 0x5a, PANE_MTX = 0x70, PANE_NAME = 0xb8 };
enum { UI_LAYOUT_W = 1280, UI_LAYOUT_H = 720, UI_TARGETS = 96, UI_ROWS = 18 };

// ---- The game's code and data (exact 1.4.1) ----
enum {
    // al / rs functions called with the game's own arguments
    START_ACTION = 0x62f120, HIT_REACTION = 0x62fb50, SET_NERVE = 0x6c7b20, LOCAL_TRANS = 0x630980,
    PANE_TRANS = 0x630f50, MAP_SCALE_MIN = 0x1d4710, MAP_SCALE_MAX = 0x1d4720,
    MENU_MARIO = 0x1d7290, LIST_LEFT = 0x1c6b60, LIST_RIGHT = 0x1c6ab0, WORLD_TEXT = 0x51f8c0,
    MAP_SELECT = 0x1d38c0, MAP_UNSELECT = 0x1d42e0, MODE_REFRESH = 0x46ae80, PANE_TRANS_3D = 0x630b60,
    // strings
    STR_WAIT = 0xe75a89, STR_SELECT = 0xe846ee, STR_CURSOR = 0xe846f5, STR_DECIDE = 0xe53f83, STR_SELECT_POS = 0xe8dd2a, STR_TXT_CONTENT = 0xe87074,
    // nerve objects set by the host
    MENU_DECIDE_PARTS = 0x120dcc0, MAP_ADSORB = 0x12cbb28, MAP_WAIT = 0x12cbb08,
    // MenuSelectParts tables (after the VR / Action Guide cuts)
    MENU_MAIN_TABLE = 0xec8270, MENU_PAUSE_TABLE = 0xec8288,
    // callers, by the return address the gate block saves: the PadStyle tag's draw-time style pick, the moped's
    // code, and the map's zoom (UI right stick after these bl)
    PAD_STYLE_DRAW = 0x6a47e8, PAD_STYLE_SET = 0x6a4d88, MOTORCYCLE_LO = 0x28e000, MOTORCYCLE_HI = 0x297000,
    // nn::ui2d::TextBox: its vtable; Calculate rebuilds the glyphs while flags (+0x11c) bit 2 is set
    TEXTBOX_VTABLE = 0x132b0e8, TEXTBOX_LENGTH = 0x11a, TEXTBOX_FLAGS = 0x11c, TEXTBOX_TAGS = 0x140,
    MAP_ZOOM_CALLS_0 = 0x49d000, MAP_ZOOM_CALLS_1 = 0x49d23c, MAP_ZOOM_CALLS_2 = 0x49d734, MAP_ZOOM_CALLS_3 = 0x49d9bc,
};
enum {   // hooked execute slots
    H_MENU_SELECT, H_MENU_SECOND, H_CHOICE_SELECT, H_CHOICE_SELECT_QUIET, H_LIST_ACTIVE, H_WINDOW_WAIT,
    H_TALK_ICON_WAIT, H_TALK_TEXT, H_MAP_WAIT, H_MAP_WAIT_ADSORB, H_MAP_ICON_MOVE, H_MAP_SCROLL, H_WORLD_WAIT,
    H_HLIST_ACTIVE, H_MODE_WAIT, H_DATA_WAIT, H_CLOSET_SELECT, H_COUNT
};
typedef void (*nerve_execute_fn)(const void *nerve, void **keeper);
typedef struct { uint32_t slot, exec, typeinfo; const char *name; uint32_t words[2]; } ui_hook;
static const ui_hook k_hooks[H_COUNT] = {
    [H_MENU_SELECT] = {0x120db88, 0x1d7a20, 0x120dba0, "N12_GLOBAL__N_124MenuSelectPartsNrvSelectE", {0xf9400020, 0x17fffe4b}},
    [H_MENU_SECOND] = {0x120dc48, 0x1d7a50, 0x120dc60, "N12_GLOBAL__N_130MenuSelectPartsNrvSelectSecondE", {0xf9400020, 0x17fffe3f}},
    [H_CHOICE_SELECT] = {0x1209398, 0x1c52d0, 0x12093b0, "N12_GLOBAL__N_126CommonSelectPartsNrvSelectE", {0xf9400020, 0x17fffd2f}},
    [H_CHOICE_SELECT_QUIET] = {0x1209258, 0x1c5180, 0x1209270, "N12_GLOBAL__N_140CommonSelectPartsNrvSelectNoAppearActionE", {0xf9400020, 0x17fffd83}},
    [H_LIST_ACTIVE] = {0x1209558, 0x1c6c90, 0x1209570, "N12_GLOBAL__N_127CommonVerticalListNrvActiveE", {0xf81e0ff3, 0xa9017bfd}},
    [H_WINDOW_WAIT] = {0x1309fc8, 0x70ce20, 0x1309fe0, "N12_GLOBAL__N_120WindowConfirmNrvWaitE", {0xf9400020, 0x17fffe83}},
    [H_TALK_ICON_WAIT] = {0x12133e8, 0x1e9590, 0x1213400, "N12_GLOBAL__N_122TalkMessageNrvIconWaitE", {0xf9400020, 0x17fffe1f}},
    [H_TALK_TEXT] = {0x12134a8, 0x1e95c0, 0x12134c0, "N12_GLOBAL__N_122TalkMessageNrvTextAnimE", {0xf9400020, 0x17fffdb7}},
    [H_MAP_WAIT] = {0x12cb878, 0x49de60, 0x12cb890, "N12_GLOBAL__N_130StageSceneStateStageMapNrvWaitE", {0xf9400020, 0x17fffc33}},
    [H_MAP_WAIT_ADSORB] = {0x12cb9b8, 0x49df10, 0x12cb9d0, "N12_GLOBAL__N_136StageSceneStateStageMapNrvWaitAdsorbE", {0xf9400020, 0x17fffdd7}},
    [H_MAP_ICON_MOVE] = {0x12cba38, 0x49dfd0, 0x12cba50, "N12_GLOBAL__N_140StageSceneStateStageMapNrvIconSelectMoveE", {0xf9400020, 0x17fffe4f}},
    [H_MAP_SCROLL] = {0x12cb938, 0x49def0, 0x12cb950, "N12_GLOBAL__N_132StageSceneStateStageMapNrvScrollE", {0xf9400020, 0x17fffc87}},
    [H_WORLD_WAIT] = {0x12cce88, 0x4a54e0, 0x12ccea0, "N12_GLOBAL__N_130StageSceneStateWorldMapNrvWaitE", {0xf9400020, 0x17fffcbf}},
    [H_HLIST_ACTIVE] = {0x1208f58, 0x1c3bc0, 0x1208f70, "N12_GLOBAL__N_129CommonHorizontalListNrvActiveE", {0xf9400020, 0x17fffe13}},
    [H_MODE_WAIT] = {0x12c4908, 0x46b580, 0x12c4920, "N12_GLOBAL__N_127SceneStateSelectModeNrvWaitE", {0xf9400020, 0x17fffe93}},
    [H_DATA_WAIT] = {0x1213b68, 0x1ea5a0, 0x1213b80, "N12_GLOBAL__N_124WindowConfirmDataNrvWaitE", {0xf9400020, 0x17fffedf}},
    [H_CLOSET_SELECT] = {0x12c6b58, 0x483200, 0x12c6b70, "N12_GLOBAL__N_130StageSceneStateClosetNrvSelectE", {0xf9400020, 0x17fffb53}},
};
// FNV-1a of the first 64 bytes (in the rewritten main) of every function whose
// fields, calls or arguments the host mirrors or calls. (Writable only so the
// synthetic test can pin its own bytes; nothing changes it at run time.)
static struct { uint32_t offset, hash; } k_code[] = {
    {0x1d7350, 0x7b181517}, {0x1c4790, 0x8a2eecd4}, {0x1c6c90, 0x5842b89e}, {0x1c6090, 0x90edff7b},
    {0x70c830, 0xa0297b3f}, {0x1e8e10, 0x74014805}, {0x1e8ca0, 0xbda963a5}, {0x49cf30, 0x37bd8cf5},
    {0x49d670, 0x26402255}, {0x49d910, 0x777f6edf}, {0x49d110, 0x291439e5}, {0x49d050, 0xe625c4a8},
    {0x49d410, 0xea03a568}, {0x4a47e0, 0xcb84c053}, {0x1c6ab0, 0x88aa834b}, {0x1c6b60, 0x88aa834b},
    {0x62f120, 0x3a2b1df5}, {0x62fb50, 0x65825040}, {0x6c7b20, 0xabff1fc4}, {0x1d7290, 0x30fa1303},
    {0x630980, 0x03fec6fd}, {0x630f50, 0x7ddc173a}, {0x51f8c0, 0x3230a691}, {0x1d4710, 0x66e87006},
    {0x1d4720, 0xf253898d}, {0x51d1e4, 0x8846875b}, {0x5d6324, 0xa5799d0a}, {0x5d6374, 0xa5799d0a},
    {0x51e724, 0xf28945c7}, {0x1c3410, 0x126f40bc}, {0x46afd0, 0xb8286ab8}, {0x46ae80, 0x80234931},
    {0x1ea120, 0x601942c3}, {0x630b60, 0x099df05f}, {0x51d284, 0x8991de93}, {0x51ea74, 0x118dbf13},
    {0x51eb74, 0xa120d0cf}, {0x51d324, 0x66c1e540}, {0x51d6e4, 0x7ec1863f}, {0x51d654, 0xa8ecb427},
    {0x51eae4, 0x15c0b79a}, {0x1d38c0, 0x3a1f8a73}, {0x1d42e0, 0x3405ba2a}, {0x481f50, 0x2d28f643},
    {0x6a4780, 0x8936eb38}, {0x5d3ec4, 0x82539018}, {0x4f7e94, 0xe985bbf3}, {0x4f7ec4, 0x631b30d7},
    {0x4f7ef4, 0xf1ad46be}, {0x4f7f24, 0xf4d7ce54}, {0x51d894, 0xbbe05997}, {0x51e834, 0xd77d9d3f},
    {0x49bab4, 0x1eca8d79}, {0x5132e4, 0x9ef5a540}, {0x51d4a4, 0xd883a0ed}, {0x51d414, 0x6371c742},
    {0x51da44, 0x1a80f05a}, {0x51dad4, 0x33622e12}, {0x907a0c, 0x1fbbb612}, {0x6a4d70, 0x091f43b6},
    {0x2d74f0, 0x15ac21e3},
};
typedef struct { float x, y; } ui_vec2;
typedef void (*start_action_fn)(void *action, const char *name, const char *pane);
typedef void (*hit_reaction_fn)(void *layout, const char *name, const char *pane);
typedef void (*set_nerve_fn)(void *user, const void *nerve);
typedef void (*object_fn)(void *object);
typedef void (*object_arg_fn)(void *object, void *arg);
typedef void (*world_text_fn)(void *row, const char *pane, uint32_t world);
typedef ui_vec2 (*local_trans_fn)(void *layout);
typedef ui_vec2 (*pane_trans_fn)(void *layout, const char *pane);
typedef float (*scalar_fn)(void);
typedef void *(*keeper_fn)(void *layout);
typedef struct { float x, y, z; } ui_vec3;
typedef void (*pane_trans_3d_fn)(ui_vec3 *out, void *layout, const char *pane);
typedef void (*mode_refresh_fn)(void *state, int animate);
static struct {
    start_action_fn start_action; hit_reaction_fn hit_reaction; set_nerve_fn set_nerve;
    object_fn menu_mario, list_left, list_right; world_text_fn world_text;
    local_trans_fn local_trans; pane_trans_fn pane_trans; scalar_fn map_scale_min, map_scale_max;
    object_arg_fn map_select, map_unselect; pane_trans_3d_fn pane_trans_3d; mode_refresh_fn mode_refresh;
} g_fn;
static nerve_execute_fn g_original[H_COUNT];
static const uint8_t *g_base;
static uintptr_t g_text_lo, g_text_hi;   // guest text: the only place a called vtable entry may point
static _Atomic int g_hooked;

// ---- Targets and pending input (main thread writes, game threads take) ----
enum { K_NONE, K_MENU, K_OPTION, K_WINDOW, K_CHOICE, K_WORLD, K_TALK, K_MAP, K_MODE, K_DATA, K_TAB, K_SKIP, K_SCREEN, K_HLIST };
typedef struct { uint8_t kind, index, across; uint8_t *root; float x, y, hw, hh; uint64_t seen; } ui_target;   // across: a horizontal row
typedef struct { int kind, index; uint8_t *root; double x, y; float row_x, row_hw; uint64_t ns; } ui_tap;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static ui_target g_targets[UI_TARGETS];
static unsigned g_count;
static uint8_t *g_talk_root, *g_map_root, *g_screen_root;
static uint64_t g_menu_ns, g_tap_screen_ns, g_game_ns, g_talk_ns, g_map_ns, g_screen_ns, g_back_ns, g_loading_ns,
    g_world_ns, g_reset_ns, g_closet_ns, g_skip_ns;
static uint8_t *g_closet_list[2];   // the wardrobe's lists: their taps are the wardrobe's
static _Atomic uint64_t g_demo_ns, g_ride_ns, g_flip_ns;   // a cutscene / the moped asked this recently (game threads)
static _Atomic int g_touch = 1, g_zoom;         // touch is in use; the zoom button held (+1 in, -1 out)
static int g_title_menu;   // the drawn Menu is the title menu (no Back)
static ui_tap g_tap;
static double g_drag_x, g_drag_y;
static int g_dragging, g_drag_moved;
static int g_footer_shown = 1;
static float g_footer_from;
static uint64_t g_footer_ns;
static dm_odyssey_ui_stats g_stats;
// Grants for the result gates. One intent (Back, Skip, a tab) is answered by
// the first of its gates that asks, and then by none of them.
enum { I_CANCEL = 1, I_MAP_CLOSE = 2, I_LIST_CLOSE = 4, I_PAUSE = 8, I_TAB_PREV = 16, I_TAB_NEXT = 32, I_TAB_CHANGE = 64, I_Y = 128, I_RIGHT = 256, I_LEFT = 512 };
static _Atomic uint64_t g_intent_until;
static _Atomic unsigned g_intent;
static _Atomic uint64_t g_decide_until, g_repeat_until;
static _Atomic uintptr_t g_repeat_ctrl;
static _Atomic int g_repeat_down;
// The wardrobe's key repeat answers one step (left / right / to a list) for its own controller.
enum { CLOSET_NONE, CLOSET_LEFT, CLOSET_RIGHT, CLOSET_CAPS, CLOSET_OUTFITS };
static _Atomic int g_closet_op;
static _Atomic uintptr_t g_closet_repeat;
static _Atomic uint64_t g_closet_until;
static _Thread_local int t_stick_armed;
static _Thread_local ui_vec2 t_stick;

static uintptr_t pointer_at(const uint8_t *p, unsigned offset) { uintptr_t v; memcpy(&v, p + offset, sizeof v); return v; }
static float float_at(const uint8_t *p, unsigned offset) { float v; memcpy(&v, p + offset, sizeof v); return v; }
static int32_t int_at(const uint8_t *p, unsigned offset) { int32_t v; memcpy(&v, p + offset, sizeof v); return v; }
static void put_int(uint8_t *p, unsigned offset, int32_t v) { memcpy(p + offset, &v, sizeof v); }
static int aligned(const void *p) { return p && !((uintptr_t)p & 7); }
static const char *str(uint32_t offset) { return (const char *)g_base + offset; }
static int recent(uint64_t seen, uint64_t now, uint64_t window) { return seen && now - seen <= window; }
static int has_prefix(const char *s, const char *prefix) { return !strncmp(s, prefix, strlen(prefix)); }

static int pane_ok(const uint8_t *pane) {
    if (!aligned(pane) || !memchr(pane + PANE_NAME, 0, 25)) return 0;
    for (unsigned offset = PANE_MTX; offset < PANE_MTX + 0x30; offset += 4)
        if (!isfinite(float_at(pane, offset)) || fabsf(float_at(pane, offset)) > 100000) return 0;
    return isfinite(float_at(pane, PANE_SIZE)) && isfinite(float_at(pane, PANE_SIZE + 4));
}
static int pane_shown(const uint8_t *pane) { return (pane[PANE_FLAGS] & 1) && pane[PANE_GLOBAL_ALPHA]; }
// The one direct child with this name (ambiguity, a bad link or a long list fail closed).
static uint8_t *child(uint8_t *parent, const char *name) {
    if (!pane_ok(parent)) return NULL;
    uintptr_t sentinel = (uintptr_t)parent + PANE_CHILDREN, node = pointer_at(parent, PANE_FIRST);
    uint8_t *found = NULL;
    for (unsigned i = 0; node != sentinel; i++) {
        if (i == 512 || node < 8 || (node & 7)) return NULL;
        uint8_t *pane = (uint8_t *)(node - 8);
        if (!pane_ok(pane) || pointer_at(pane, PANE_PARENT) != (uintptr_t)parent) return NULL;
        if (!strncmp((const char *)pane + PANE_NAME, name, 25)) {
            if (found) return NULL;
            found = pane;
        }
        node = pointer_at(pane, PANE_NEXT);
    }
    return found;
}
static uint8_t *path(uint8_t *pane, const char *const *names) {
    for (; pane && *names; names++) pane = pane_shown(pane) ? child(pane, *names) : NULL;
    return pane && pane_shown(pane) ? pane : NULL;
}
// Layout units (origin centre, y up) on the 1280x720 screen → normalized picture.
static ui_target target_of(const uint8_t *pane, int kind, int index, uint8_t *root, uint64_t now) {
    float sx = fabsf(float_at(pane, PANE_MTX)), sy = fabsf(float_at(pane, PANE_MTX + 0x14));
    return (ui_target){(uint8_t)kind, (uint8_t)index, 0, root,
        .5f + float_at(pane, PANE_MTX + 0xc) / UI_LAYOUT_W, .5f - float_at(pane, PANE_MTX + 0x1c) / UI_LAYOUT_H,
        float_at(pane, PANE_SIZE) * sx / 2 / UI_LAYOUT_W, float_at(pane, PANE_SIZE + 4) * sy / 2 / UI_LAYOUT_H, now};
}

// ---- Observation ----
// The first pane with this name at most depth levels down (breadth first through shown panes).
static uint8_t *find(uint8_t *pane, const char *name, int depth) {
    if (!pane || !pane_shown(pane) || depth < 0) return NULL;
    uint8_t *direct = child(pane, name);
    if (direct) return direct;
    uintptr_t sentinel = (uintptr_t)pane + PANE_CHILDREN, node = pointer_at(pane, PANE_FIRST);
    for (unsigned i = 0; node != sentinel && i < 512; i++) {
        if (node < 8 || (node & 7)) return NULL;
        uint8_t *sub = (uint8_t *)(node - 8);
        if (!pane_ok(sub)) return NULL;
        uint8_t *found = depth > 0 ? find(sub, name, depth - 1) : NULL;
        if (found) return found;
        node = pointer_at(sub, PANE_NEXT);
    }
    return NULL;
}
static unsigned rows(uint8_t *parent, const char *format, int first, int last, int kind, uint8_t *root, uint64_t now,
                     ui_target *out, unsigned room) {
    unsigned count = 0;
    for (int i = first; parent && i <= last && count < room; i++) {
        char name[24]; snprintf(name, sizeof name, format, i);
        uint8_t *row = child(parent, name);
        if (!row || !pane_shown(row)) continue;
        ui_target t = target_of(row, kind, i - first, root, now);
        if (t.hw > 0 && t.hh > 0) out[count++] = t;
    }
    // Rows laid out across (shop items, captures): each owns half the gap to its neighbours.
    if (count >= 2 && fabsf(out[1].x - out[0].x) > fabsf(out[1].y - out[0].y)) {
        float half = fabsf(out[1].x - out[0].x) / 2;
        for (unsigned i = 0; i < count; i++) { out[i].across = 1; out[i].hw = half; out[i].hh = 200.f / UI_LAYOUT_H; }
    }
    return count;
}
// The world strip: World01..17 hold ParWorld01..17 (list order).
static unsigned worlds(uint8_t *strip, uint8_t *root, uint64_t now, ui_target *out, unsigned room) {
    unsigned count = 0;
    for (int i = 1; strip && i <= 17 && count < room; i++) {
        char slot[16], part[16]; snprintf(slot, sizeof slot, "World%02d", i); snprintf(part, sizeof part, "ParWorld%02d", i);
        uint8_t *holder = child(strip, slot), *row = holder && pane_shown(holder) ? child(holder, part) : NULL;
        if (!row || !pane_shown(row)) continue;
        ui_target t = target_of(row, K_WORLD, i - 1, root, now);
        t.hw = 30.f / UI_LAYOUT_W; t.hh = 90.f / UI_LAYOUT_H;   // icons on the strip: nearest by x, a band around it
        out[count++] = t;
    }
    return count;
}
// Replace this root's targets with the newly found ones.
static void publish(uint8_t *root, const ui_target *found, unsigned count) {
    unsigned keep = 0;
    for (unsigned i = 0; i < g_count; i++)
        if (g_targets[i].root != root) g_targets[keep++] = g_targets[i];
    g_count = keep;
    for (unsigned i = 0; i < count && g_count < UI_TARGETS; i++) g_targets[g_count++] = found[i];
}
static int menu_rows(uint8_t *root, uint64_t now, ui_target *found) {
    static const char *const list_path[] = {"All", "List", "ListPos", NULL}, *const cursor_path[] = {"All", "ParCursor", NULL};
    uint8_t *items = path(root, list_path), *cursor = path(root, cursor_path);
    if (!items || !cursor) return 0;
    unsigned count = rows(items, "ParList%02d", 0, 6, K_MENU, root, now, found, UI_ROWS);
    float cursor_y = target_of(cursor, 0, 0, root, now).y;
    for (unsigned i = 0; i < count; i++) if (fabsf(found[i].y - cursor_y) <= found[i].hh) return (int)count;   // selecting
    return 0;
}
static int in_names(const char *owner, const char *const *names) {
    for (; *names; names++) if (!strcmp(owner, *names)) return 1;
    return 0;
}
// The first sighting of each layout owner goes to the log (for screens without tap support yet).
static void seen_owner(const char *owner) {
    static char seen[96][32]; static unsigned count; static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&lock);
    unsigned i = 0;
    while (i < count && strncmp(seen[i], owner, sizeof seen[i] - 1)) i++;
    if (i == count && count < 96) { snprintf(seen[count++], sizeof seen[0], "%s", owner); LOG("Odyssey tap UI: layout %s drawn", owner); }
    pthread_mutex_unlock(&lock);
}
// After a switch between touch and a controller: text boxes (with a tag
// processor) rebuild their glyphs at the next Calculate, so pad pictures follow.
static void rebuild_text(uint8_t *pane, int depth) {
    if (!pane_ok(pane) || depth > 16) return;
    if (pointer_at(pane, 0) == (uintptr_t)g_base + TEXTBOX_VTABLE && pointer_at(pane, TEXTBOX_TAGS) &&
        *(const uint16_t *)(pane + TEXTBOX_LENGTH)) pane[TEXTBOX_FLAGS] |= 4;
    uintptr_t sentinel = (uintptr_t)pane + PANE_CHILDREN, node = pointer_at(pane, PANE_FIRST);
    for (unsigned i = 0; node != sentinel && i < 512; i++) {
        if (node < 8 || (node & 7)) return;
        uint8_t *sub = (uint8_t *)(node - 8);
        rebuild_text(sub, depth + 1);
        node = pointer_at(sub, PANE_NEXT);
    }
}
void dm_odyssey_ui_observe(const char *owner, uint8_t *root) {
    if (!owner || !pane_ok(root)) return;
    uint64_t now = now_ns();
    ui_target found[UI_ROWS]; unsigned count = 0;
    int tap_screen = 0, talk = 0, map = 0, back = 0, screen = 0, published = 1;
    static const char *const lists[] = {"ShineList", "BgmList", "HackList", "SouvenirList", NULL};
    static const char *const info[] = {"WindowInfo", "ImportantMessage", NULL};
    static const char *const loading[] = {"BootLoading", "ContinueLoading", NULL};
    int game = !strcmp(owner, "CounterCoin") || !strcmp(owner, "CounterLife") || !strcmp(owner, "CounterLifeKids");
    int world = 0, reset = 0, skip = 0;
    seen_owner(owner);
    if (recent(atomic_load(&g_flip_ns), now, DM_UI_TEXT_REFRESH_NS)) rebuild_text(root, 0);
    uint8_t *all = pane_shown(root) ? child(root, "All") : NULL;
    if (!strcmp(owner, "Menu")) {
        int n = menu_rows(root, now, found);
        count = n > 0 ? (unsigned)n : 0; tap_screen = n > 0; back = tap_screen && !g_title_menu;
    } else if (has_prefix(owner, "Option") || in_names(owner, lists)) {
        count = rows(find(all, "ListArea", 3), "ParList%02d", 0, 15, K_OPTION, root, now, found, UI_ROWS);
        tap_screen = back = count > 0;
        reset = tap_screen && !strcmp(owner, "OptionConfig");   // its footer offers Y Reset
    } else if (!strcmp(owner, "MiniGameRaceResult")) {
        screen = tap_screen = all != NULL;
    } else if (!strcmp(owner, "WindowConfirmData")) {
        uint8_t *first = find(all, "ParList00", 3), *holder = first ? (uint8_t *)pointer_at(first, PANE_PARENT) : NULL;
        count = rows(holder, "ParList%02d", 0, 1, K_DATA, root, now, found, UI_ROWS);
        tap_screen = back = count > 0;
    } else if (has_prefix(owner, "WindowConfirm")) {
        count = rows(find(all, "Window", 1), "ParList%02d", 0, 2, K_WINDOW, root, now, found, UI_ROWS);
        tap_screen = back = count > 0;
    } else if (!strcmp(owner, "ModeSelect")) {
        count = rows(find(all, "ModeSelect", 1), "ParMode%02d", 0, 1, K_MODE, root, now, found, UI_ROWS);
        tap_screen = back = count > 0;
    } else if (!strcmp(owner, "MiniGameMenu")) {
        count = rows(find(all, "List", 1), "ParList%02d", 0, 5, K_CHOICE, root, now, found, UI_ROWS);
        tap_screen = count > 0;
    } else if (!strcmp(owner, "TalkMessage")) {
        static const char *const s[] = {"All", "Message", "Select", NULL};
        talk = find(all, "Message", 1) != NULL;
        count = rows(path(root, s), "ParList%02d", 0, 3, K_CHOICE, root, now, found, UI_ROWS);
        tap_screen = talk;
    } else if (!strcmp(owner, "WorldSelect")) {
        static const char *const p[] = {"All", "World", "Pos", NULL};
        count = worlds(path(root, p), root, now, found, UI_ROWS);
        tap_screen = back = world = count > 0;
    } else if (!strcmp(owner, "Map")) {
        map = tap_screen = back = pane_shown(root);
    } else if (!strcmp(owner, "CollectList")) {
        uint8_t *roll = find(all, "ParRoll", 1);   // the tab bar: its left half is the previous tab, its right half the next
        if (roll) { found[0] = target_of(roll, K_TAB, 0, root, now); count = 1; }
        published = 1;
    } else if (!strcmp(owner, "PlayGuideSkip")) {
        uint8_t *label = find(all, "PicCaptureUse", 3);
        if (!label) label = find(root, "Capture", 1);
        if (label) { found[0] = target_of(label, K_SKIP, 0, root, now); found[0].hw += 20.f / UI_LAYOUT_W; found[0].hh += 20.f / UI_LAYOUT_H; count = 1; }
        tap_screen = skip = count > 0;
    } else if (in_names(owner, info)) {
        screen = tap_screen = all != NULL;
    } else if (in_names(owner, loading)) {
        if (pane_shown(root)) { pthread_mutex_lock(&g_lock); g_loading_ns = now; pthread_mutex_unlock(&g_lock); }
        return;
    } else if (!game) return;
    (void)published;
    pthread_mutex_lock(&g_lock);
    publish(root, found, count);
    if (!strcmp(owner, "Menu") && tap_screen) g_menu_ns = now;
    if (tap_screen) g_tap_screen_ns = now;
    if (back) g_back_ns = now;
    if (talk) { g_talk_root = root; g_talk_ns = now; }
    if (map) { g_map_root = root; g_map_ns = now; }
    if (screen) { g_screen_root = root; g_screen_ns = now; }
    if (game && pane_shown(root)) g_game_ns = now;
    if (world) g_world_ns = now;
    if (reset) g_reset_ns = now;
    if (skip) g_skip_ns = now;
    pthread_mutex_unlock(&g_lock);
}

// ---- Front end ----
static int screen_locked(uint64_t now) {
    if (recent(g_tap_screen_ns, now, DM_UI_RECENT_NS)) return DM_UI_MENU;
    if (recent(atomic_load(&g_demo_ns), now, DM_UI_RECENT_NS)) return DM_UI_MENU;   // a cutscene: a tap brings up Skip
    if (recent(g_game_ns, now, DM_UI_GAME_RECENT_NS)) return DM_UI_GAME;
    if (recent(g_loading_ns, now, DM_UI_RECENT_NS)) return DM_UI_LOADING;
    return DM_UI_UNKNOWN;
}
static void grant_intent(unsigned mask) {
    atomic_store(&g_intent, 0);
    atomic_store(&g_intent_until, now_ns() + DM_UI_ARM_NS);
    atomic_store(&g_intent, mask);
}
int dm_odyssey_ui_available(void) {
    dm_odyssey_display_hud_stats hud; dm_odyssey_display_hud_snapshot(&hud);
    return hud.installed && atomic_load(&g_hooked);
}
int dm_odyssey_ui_screen(void) {
    uint64_t now = now_ns();
    pthread_mutex_lock(&g_lock); int screen = screen_locked(now); pthread_mutex_unlock(&g_lock);
    return screen;
}
static const ui_target *hit_locked(double x, double y, uint64_t now) {
    const ui_target *best = NULL; double best_d = INFINITY;
    for (unsigned i = 0; i < g_count; i++) {
        const ui_target *t = &g_targets[i];
        if (!recent(t->seen, now, DM_UI_RECENT_NS)) continue;
        // Rows reach halfway into the gaps between them; world icons and rows laid out across are nearest by x.
        double dx = fabs(x - t->x), dy = fabs(y - t->y);
        int inside = t->kind == K_WORLD ? dx <= t->hw * 2 && dy <= t->hh : t->across ? dx <= t->hw && dy <= t->hh :
            dx <= t->hw && dy <= t->hh * 1.35;
        double d = t->kind == K_WORLD || t->across ? dx : dy;
        if (inside && d < best_d) { best_d = d; best = t; }
    }
    return best;
}
int dm_odyssey_ui_hit(double x, double y) {
    if (!isfinite(x) || !isfinite(y) || !atomic_load(&g_hooked)) return 0;
    uint64_t now = now_ns();
    pthread_mutex_lock(&g_lock);
    const ui_target *t = screen_locked(now) == DM_UI_MENU ? hit_locked(x, y, now) : NULL;
    int id = t ? t->kind * 32 + t->index + 1 : 0;
    pthread_mutex_unlock(&g_lock);
    return id;
}
int dm_odyssey_ui_tap(double x, double y) {
    if (!isfinite(x) || !isfinite(y) || !atomic_load(&g_hooked)) return 0;
    uint64_t now = now_ns();
    pthread_mutex_lock(&g_lock);
    int ok = screen_locked(now) == DM_UI_MENU;
    if (ok) {
        const ui_target *t = hit_locked(x, y, now);
        ui_tap tap = {K_NONE, 0, NULL, x, y, 0, 0, now};
        if (t) { tap.kind = t->kind; tap.index = t->index; tap.root = t->root; tap.row_x = t->x; tap.row_hw = t->hw; }
        else if (recent(g_map_ns, now, DM_UI_RECENT_NS)) { tap.kind = K_MAP; tap.root = g_map_root; }
        else if (recent(g_talk_ns, now, DM_UI_RECENT_NS)) { tap.kind = K_TALK; tap.root = g_talk_root; }
        else if (recent(g_screen_ns, now, DM_UI_RECENT_NS)) tap.kind = K_SCREEN;
        else if (recent(atomic_load(&g_demo_ns), now, DM_UI_RECENT_NS) || recent(g_skip_ns, now, DM_UI_RECENT_NS)) tap.kind = K_SKIP;
        // The tab bar, a cutscene (first the Skip guide, then the skip) and info windows are answered without a hook.
        if (tap.kind == K_TAB) grant_intent(x < tap.row_x ? I_TAB_PREV | I_TAB_CHANGE : I_TAB_NEXT | I_TAB_CHANGE);
        else if (tap.kind == K_SKIP) grant_intent(I_PAUSE);
        else if (tap.kind == K_SCREEN) { atomic_store(&g_decide_until, now + DM_UI_ARM_NS); ++g_stats.decides; }
        else if (tap.kind != K_NONE) g_tap = tap;
        if (tap.kind != K_NONE) ++g_stats.taps; else ok = 0;
    }
    pthread_mutex_unlock(&g_lock);
    return ok;
}
int dm_odyssey_ui_back_available(void) {
    if (!atomic_load(&g_hooked)) return 0;
    uint64_t now = now_ns();
    pthread_mutex_lock(&g_lock);
    int ok = screen_locked(now) == DM_UI_MENU && recent(g_back_ns, now, DM_UI_RECENT_NS);
    pthread_mutex_unlock(&g_lock);
    return ok;
}
int dm_odyssey_ui_back(void) {
    if (!dm_odyssey_ui_back_available()) return 0;
    grant_intent(I_CANCEL | I_MAP_CLOSE | I_LIST_CLOSE);   // whichever close the screen asks for
    pthread_mutex_lock(&g_lock); ++g_stats.backs; pthread_mutex_unlock(&g_lock);
    return 1;
}
unsigned dm_odyssey_ui_actions(void) {
    if (!atomic_load(&g_hooked)) return 0;
    uint64_t now = now_ns();
    pthread_mutex_lock(&g_lock);
    unsigned actions = 0;
    if (screen_locked(now) == DM_UI_MENU) {
        int map = recent(g_map_ns, now, DM_UI_RECENT_NS);
        if (map || recent(g_world_ns, now, DM_UI_RECENT_NS)) actions |= DM_UI_ACTION_LISTS;
        if (map) actions |= DM_UI_ACTION_ZOOM;
        if (recent(g_reset_ns, now, DM_UI_RECENT_NS)) actions |= DM_UI_ACTION_RESET;
        if (recent(g_closet_ns, now, DM_UI_RECENT_NS)) actions |= DM_UI_ACTION_RANDOM;
    }
    pthread_mutex_unlock(&g_lock);
    return actions;
}
int dm_odyssey_ui_action(unsigned action) {
    unsigned offered = dm_odyssey_ui_actions();
    if (action == DM_UI_ACTION_LISTS && (offered & action)) grant_intent(I_TAB_CHANGE);
    else if ((action == DM_UI_ACTION_RESET || action == DM_UI_ACTION_RANDOM) && (offered & action)) grant_intent(I_Y);
    else return 0;
    pthread_mutex_lock(&g_lock); ++g_stats.actions; pthread_mutex_unlock(&g_lock);
    return 1;
}
void dm_odyssey_ui_zoom(int direction) {
    atomic_store(&g_zoom, direction > 0 ? 1 : direction < 0 ? -1 : 0);
}
int dm_odyssey_ui_riding(void) {
    uint64_t now = now_ns();
    if (!atomic_load(&g_hooked) || !recent(atomic_load(&g_ride_ns), now, DM_UI_RIDE_NS)) return 0;
    pthread_mutex_lock(&g_lock); int game = screen_locked(now) == DM_UI_GAME; pthread_mutex_unlock(&g_lock);
    return game;
}
void dm_odyssey_ui_drag(double dx, double dy, int ended) {
    if (!isfinite(dx) || !isfinite(dy)) return;
    pthread_mutex_lock(&g_lock);
    if (recent(g_map_ns, now_ns(), DM_UI_RECENT_NS) && !ended) {
        g_drag_x += dx; g_drag_y += dy; g_dragging = 1;
        if (dx || dy) g_drag_moved = 1;
    } else { g_dragging = 0; g_drag_x = g_drag_y = 0; }
    pthread_mutex_unlock(&g_lock);
}
// The pending tap, if it is for this screen and this layout.
static ui_tap take_tap(int kind, uint8_t *root) {
    uint64_t now = now_ns();
    ui_tap none = {0};
    pthread_mutex_lock(&g_lock);
    ui_tap tap = g_tap;
    if (tap.kind != K_NONE && now - tap.ns > DM_UI_TAP_NS) { g_tap.kind = K_NONE; ++g_stats.dropped; tap.kind = K_NONE; }
    int mine = tap.kind == kind && root && tap.root == root;
    if (mine) g_tap.kind = K_NONE;
    pthread_mutex_unlock(&g_lock);
    return mine ? tap : none;
}

// ---- Game-side helpers ----
static int in_text(const void *fn) { return (uintptr_t)fn >= g_text_lo && (uintptr_t)fn < g_text_hi; }
// A LayoutActor's root pane: IUseLayout (+8) → getLayoutKeeper (slot 1) → layout (+0x10) → root (+0x18),
// as al::calcPaneTrans reads it.
static uint8_t *root_of(uint8_t *actor) {
    if (!aligned(actor)) return NULL;
    uint8_t *iu = actor + 8;
    uintptr_t vtable = pointer_at(iu, 0);
    if (!aligned((void *)vtable)) return NULL;
    keeper_fn keeper_of = (keeper_fn)pointer_at((const uint8_t *)vtable, 8);
    if (!in_text((const void *)keeper_of)) return NULL;
    uint8_t *keeper = keeper_of(iu), *layout = aligned(keeper) ? (uint8_t *)pointer_at(keeper, 0x10) : NULL;
    uint8_t *root = aligned(layout) ? (uint8_t *)pointer_at(layout, 0x18) : NULL;
    return pane_ok(root) ? root : NULL;
}
static void *action_of(uint8_t *actor) { return actor ? actor + 0x10 : NULL; }   // IUseLayoutAction
static void grant_decide(void) {
    atomic_store(&g_decide_until, now_ns() + DM_UI_ARM_NS);
    pthread_mutex_lock(&g_lock); ++g_stats.decides; pthread_mutex_unlock(&g_lock);
}
static void count_chosen(void) { pthread_mutex_lock(&g_lock); ++g_stats.chosen; pthread_mutex_unlock(&g_lock); }

// MenuSelectParts (exeSelect's move and decide, with the game's functions).
enum { MENU_LAYOUT = 0x10, MENU_AMOUNT = 0x18, MENU_CURSOR = 0x1c, MENU_MAX = 0x20, MENU_ARRAY = 0x28, MENU_MAIN = 0x58 };
static void menu_step(uint8_t *parts) {
    if (!aligned(parts)) return;
    pthread_mutex_lock(&g_lock); g_title_menu = parts[MENU_MAIN] != 0; pthread_mutex_unlock(&g_lock);
    uint8_t *layout = (uint8_t *)pointer_at(parts, MENU_LAYOUT);
    ui_tap tap = take_tap(K_MENU, root_of(layout));
    if (tap.kind != K_MENU) return;
    int part = tap.index, amount = int_at(parts, MENU_AMOUNT), cursor = int_at(parts, MENU_CURSOR), max = int_at(parts, MENU_MAX);
    const uint8_t *table = g_base + (parts[MENU_MAIN] ? MENU_MAIN_TABLE : MENU_PAUSE_TABLE);
    uint8_t **array = (uint8_t **)pointer_at(parts, MENU_ARRAY);
    if (amount <= 0 || amount > 6 || cursor < 0 || cursor >= amount || max <= part || max > 8 || !aligned(array)) return;
    int index = -1;
    for (int i = 0; i < amount; i++) {
        int32_t value = int_at(table, 4 * (unsigned)i);
        if (value < 0 || value >= max) return;
        if (value == part) index = i;
    }
    uint8_t *was = array[int_at(table, 4 * (unsigned)cursor)], *row = array[part];
    if (index < 0 || !row) return;
    if (index != cursor) {
        if (was) g_fn.start_action(action_of(was), str(STR_WAIT), NULL);
        put_int(parts, MENU_CURSOR, index);
        g_fn.start_action(action_of(row), str(STR_SELECT), NULL);
        g_fn.menu_mario(parts);
    }
    g_fn.hit_reaction(layout, str(STR_DECIDE), NULL);
    g_fn.set_nerve(parts, g_base + MENU_DECIDE_PARTS);
    count_chosen();
}
// CommonSelectParts (dialogue choices): the move, then exeSelect's own decide.
enum { CHOICE_LAYOUT = 0x10, CHOICE_COUNT = 0x18, CHOICE_CURSOR = 0x1c, CHOICE_ROWS = 0x28 };
static void choice_step(uint8_t *parts) {
    if (!aligned(parts)) return;
    ui_tap tap = take_tap(K_CHOICE, root_of((uint8_t *)pointer_at(parts, CHOICE_LAYOUT)));
    int count = int_at(parts, CHOICE_COUNT), cursor = int_at(parts, CHOICE_CURSOR);
    uint8_t **array = (uint8_t **)pointer_at(parts, CHOICE_ROWS);
    if (tap.kind != K_CHOICE || tap.index >= count || count > 8 || cursor < 0 || cursor >= count || !aligned(array) || !array[tap.index]) return;
    if (tap.index != cursor) {
        if (array[cursor]) g_fn.start_action(action_of(array[cursor]), str(STR_WAIT), NULL);
        put_int(parts, CHOICE_CURSOR, tap.index);
        g_fn.start_action(action_of(array[tap.index]), str(STR_SELECT), NULL);
    }
    grant_decide(); count_chosen();
}
// CommonVerticalList (options): rows show item top + row - 1; a config row steps its value.
enum { LIST_LAYOUT = 0x10, LIST_CURSOR = 0x3c, LIST_TOP = 0x40, LIST_PREV_CURSOR = 0x44, LIST_PREV_TOP = 0x48,
       LIST_MOVING = 0x50, LIST_ENABLED = 0x90, LIST_COUNT = 0x9c, LIST_ROLLS = 0xb8 };
static void list_step(uint8_t *list) {
    if (!aligned(list)) return;
    ui_tap tap = take_tap(K_OPTION, root_of((uint8_t *)pointer_at(list, LIST_LAYOUT)));
    if (tap.kind != K_OPTION || int_at(list, LIST_MOVING) > 0) return;
    int item = int_at(list, LIST_TOP) + tap.index - 1, count = int_at(list, LIST_COUNT);
    const uint8_t *enabled = (const uint8_t *)pointer_at(list, LIST_ENABLED);
    if (item < 0 || item >= count || count > 256 || (enabled && !enabled[item])) return;
    if (item != int_at(list, LIST_CURSOR)) {
        put_int(list, LIST_PREV_CURSOR, int_at(list, LIST_CURSOR)); put_int(list, LIST_PREV_TOP, int_at(list, LIST_TOP));
        put_int(list, LIST_CURSOR, item);   // the list's own update shows Select / Wait and moves the cursor
    }
    if (pointer_at(list, LIST_ROLLS)) (tap.x < tap.row_x ? g_fn.list_left : g_fn.list_right)(list);   // a config value
    else grant_decide();
    count_chosen();
}
// WindowConfirm: one row short, then exeWait's own Down step lands on it.
enum { WINDOW_DIRECTION = 0x12c, WINDOW_COUNT = 0x130, WINDOW_CURSOR = 0x134, WINDOW_COOLDOWN = 0x140,
       WINDOW_ROWS_SIZE = 0x148, WINDOW_ROWS = 0x150 };
static void window_step(uint8_t *window) {
    if (!aligned(window)) return;
    ui_tap tap = take_tap(K_WINDOW, root_of(window));
    int count = int_at(window, WINDOW_COUNT), cursor = int_at(window, WINDOW_CURSOR), size = int_at(window, WINDOW_ROWS_SIZE);
    uint8_t **array = (uint8_t **)pointer_at(window, WINDOW_ROWS);
    if (tap.kind != K_WINDOW || count <= 0 || count > 3 || size < count || tap.index >= count || cursor < 0 || cursor >= count || !aligned(array)) return;
    if (tap.index != cursor) {
        if (array[cursor]) g_fn.start_action(action_of(array[cursor]), str(STR_WAIT), NULL);
        put_int(window, WINDOW_CURSOR, (tap.index + count - 1) % count);
        put_int(window, WINDOW_DIRECTION, 2);
    }
    grant_decide(); count_chosen();
}
// CommonHorizontalList (shops, the wardrobe): row actors (+0x20, count +0x18, shown +0x38) show item top + row;
// next / previous move cursor (+0x3c) and top (+0x40) together, so the cursor keeps its slot.
enum { HLIST_LAYOUT = 0x10, HLIST_ROWS_SIZE = 0x18, HLIST_ROWS = 0x20, HLIST_SHOWN = 0x38, HLIST_CURSOR = 0x3c, HLIST_TOP = 0x40,
       HLIST_MOVING = 0x4c, HLIST_COUNT = 0x8c };
// Publish a list's rows from its row actors (their parts' root panes), under the list layout's root.
static uint8_t *hlist_targets(uint8_t *list, uint64_t now) {
    uint8_t *root = root_of((uint8_t *)pointer_at(list, HLIST_LAYOUT));
    int size = int_at(list, HLIST_ROWS_SIZE), shown = int_at(list, HLIST_SHOWN), top = int_at(list, HLIST_TOP), count = int_at(list, HLIST_COUNT);
    uint8_t **rows = (uint8_t **)pointer_at(list, HLIST_ROWS);
    if (!root || !aligned(rows) || size <= 0 || size > 64 || count <= 0 || count > 4096) return NULL;
    ui_target found[UI_ROWS]; unsigned n = 0;
    for (int i = 0; i < size && i < shown && n < UI_ROWS; i++) {
        uint8_t *row = rows[i], *pane = aligned(row) ? root_of(row) : NULL;
        if (!pane || !pane_shown(pane) || top + i < 0 || top + i >= count) continue;
        ui_target t = target_of(pane, K_HLIST, i, root, now);
        float h = float_at(pane, PANE_SIZE + 4) * fabsf(float_at(pane, PANE_MTX + 0x14));
        t.across = 1; t.hh = (h > 0 && h < 400 ? h / 2 : 150) / UI_LAYOUT_H;
        found[n++] = t;
    }
    for (unsigned i = 0; i < n; i++) {   // each row owns half the gap to its neighbours
        float gap = n > 1 ? fabsf(found[i ? i : 1].x - found[i ? i - 1 : 0].x) : 100.f / UI_LAYOUT_W;
        found[i].hw = gap / 2;
    }
    pthread_mutex_lock(&g_lock);
    publish(root, found, n);
    if (n) { g_tap_screen_ns = g_back_ns = now; }
    pthread_mutex_unlock(&g_lock);
    return root;
}
// Put the list one short of item (cursor keeps its slot); 1 = a step right is next, -1 left, 0 = item is selected.
static int hlist_short(uint8_t *list, int item) {
    int cursor = int_at(list, HLIST_CURSOR), top = int_at(list, HLIST_TOP), count = int_at(list, HLIST_COUNT);
    if (item == cursor) return 0;
    int dir = item > cursor ? 1 : -1, from = item - dir;
    if (from < 0 || from >= count) return 0;
    put_int(list, HLIST_CURSOR, from); put_int(list, HLIST_TOP, from - (cursor - top));
    return dir;
}
static int closet_list(const uint8_t *list) {
    pthread_mutex_lock(&g_lock);
    int mine = recent(g_closet_ns, now_ns(), DM_UI_RECENT_NS) && (list == g_closet_list[0] || list == g_closet_list[1]);
    pthread_mutex_unlock(&g_lock);
    return mine;
}
static void hlist_step(uint8_t *list) {
    if (!aligned(list) || closet_list(list)) return;   // the wardrobe publishes and steps its own
    uint8_t *root = hlist_targets(list, now_ns());
    ui_tap tap = take_tap(K_HLIST, root);
    if (tap.kind != K_HLIST || int_at(list, HLIST_MOVING) >= 0) return;   // idle below zero
    int item = int_at(list, HLIST_TOP) + tap.index, count = int_at(list, HLIST_COUNT);
    if (item < 0 || item >= count || count > 4096) return;
    int dir = hlist_short(list, item);
    if (!dir) { grant_decide(); count_chosen(); return; }   // the selected item: choose it
    grant_intent(dir > 0 ? I_RIGHT : I_LEFT);
    pthread_mutex_lock(&g_lock); ++g_stats.steps; pthread_mutex_unlock(&g_lock);
    count_chosen();
}
// WindowConfirmData (save data confirmations): exeWait places the cursor from the selection.
enum { DATA_LAYOUT = 0x10, DATA_OPTIONS = 0x20, DATA_SELECTION = 0x38 };
static void data_step(uint8_t *window) {
    if (!aligned(window)) return;
    ui_tap tap = take_tap(K_DATA, root_of((uint8_t *)pointer_at(window, DATA_LAYOUT)));
    int selection = int_at(window, DATA_SELECTION);
    if (tap.kind != K_DATA || tap.index > 1 || selection < 0 || selection > 1) return;
    uint8_t *rows[2] = {(uint8_t *)pointer_at(window, DATA_OPTIONS), (uint8_t *)pointer_at(window, DATA_OPTIONS + 8)};
    if (tap.index != selection) {
        if (rows[selection]) g_fn.start_action(action_of(rows[selection]), str(STR_WAIT), NULL);
        if (rows[tap.index]) g_fn.start_action(action_of(rows[tap.index]), str(STR_SELECT), NULL);
        put_int(window, DATA_SELECTION, tap.index);
    }
    grant_decide(); count_chosen();
}
// SceneStateSelectMode: two modes; the flag at +0x59 picks the part at +0x30 (set) or +0x38.
enum { MODE_LAYOUT = 0x28, MODE_SET_PART = 0x30, MODE_CLEAR_PART = 0x38, MODE_FLAG = 0x59 };
static void mode_step(uint8_t *state) {
    if (!aligned(state)) return;
    ui_tap tap = take_tap(K_MODE, root_of((uint8_t *)pointer_at(state, MODE_LAYOUT)));
    if (tap.kind != K_MODE) return;
    uint8_t *set = (uint8_t *)pointer_at(state, MODE_SET_PART), *clear = (uint8_t *)pointer_at(state, MODE_CLEAR_PART);
    if (!aligned(set) || !aligned(clear)) return;
    // Which part was tapped: the one whose Cursor pane is nearer the tapped row.
    ui_vec3 a = {0}, b = {0};
    g_fn.pane_trans_3d(&a, set + 8, str(STR_CURSOR)); g_fn.pane_trans_3d(&b, clear + 8, str(STR_CURSOR));
    float row_y = (float)((.5 - tap.y) * UI_LAYOUT_H);
    int want = fabsf(a.y - row_y) < fabsf(b.y - row_y);
    if ((state[MODE_FLAG] != 0) != want) { state[MODE_FLAG] = (uint8_t)want; g_fn.mode_refresh(state, 0); }
    grant_decide(); count_chosen();
}
// TalkMessage: a tap anywhere on the dialogue continues it.
static void talk_step(uint8_t *talk) {
    if (!aligned(talk)) return;
    ui_tap tap = take_tap(K_TALK, root_of(talk));
    if (tap.kind == K_TALK) { grant_decide(); count_chosen(); }
}
// Kingdom select: one kingdom short, then the Wait nerve's own step (granted
// KeyRepeatCtrl isDown / isUp) turns the globe, swaps names and plays the sound.
enum { WORLD_LAYOUT = 0x20, WORLD_ROWS = 0x40, WORLD_REPEAT = 0x68, WORLD_COUNT = 0xfc, WORLD_CURSOR = 0x100,
       WORLD_IDS = 0x108, WORLD_WRAP = 0x149 };
// StageSceneStateCloset exeSelect: two CommonHorizontalLists (+0x30: outfits, caps), the active one (+0x78) and
// the key repeat its steps and list switches read (+0x1d0). The cursor goes one short of the tapped item and the
// repeat answers one step onto it, so exeSelect changes the costume itself; a tap on the other list switches
// first and is taken again on the next frame.
enum { CLOSET_LISTS = 0x30, CLOSET_ACTIVE = 0x78, CLOSET_REPEAT = 0x1d0 };
static void closet_arm(uintptr_t repeat, int op) {
    atomic_store(&g_closet_op, CLOSET_NONE);
    atomic_store(&g_closet_repeat, repeat); atomic_store(&g_closet_until, now_ns() + DM_UI_ARM_NS);
    atomic_store(&g_closet_op, op);
}
static void closet_step(uint8_t *state) {
    if (!aligned(state)) return;
    uint8_t **lists = (uint8_t **)pointer_at(state, CLOSET_LISTS);
    uintptr_t repeat = pointer_at(state, CLOSET_REPEAT);
    int active = int_at(state, CLOSET_ACTIVE);
    if (!aligned(lists) || !aligned((void *)repeat) || (active != 0 && active != 1)) return;
    uint64_t now = now_ns();
    uint8_t *roots[2] = {NULL, NULL};
    pthread_mutex_lock(&g_lock); g_closet_list[0] = lists[0]; g_closet_list[1] = lists[1]; g_closet_ns = now; pthread_mutex_unlock(&g_lock);
    for (int l = 0; l < 2; l++) if (aligned(lists[l])) roots[l] = hlist_targets(lists[l], now);
    for (int l = 0; l < 2; l++) {
        uint8_t *list = lists[l];
        if (!aligned(list) || !roots[l]) continue;
        ui_tap tap = take_tap(K_HLIST, roots[l]);
        if (tap.kind != K_HLIST) continue;
        int item = int_at(list, HLIST_TOP) + tap.index, count = int_at(list, HLIST_COUNT), cursor = int_at(list, HLIST_CURSOR);
        if (item < 0 || item >= count || count > 4096 || cursor < 0 || cursor >= count) return;
        if (l != active) {   // switch lists now, step onto the item next frame
            closet_arm(repeat, l ? CLOSET_CAPS : CLOSET_OUTFITS);   // the tap keeps its time, so it cannot outlive DM_UI_TAP_NS
            pthread_mutex_lock(&g_lock); if (g_tap.kind == K_NONE) g_tap = tap; pthread_mutex_unlock(&g_lock);
            return;
        }
        int dir = hlist_short(list, item);
        if (!dir) { grant_decide(); count_chosen(); return; }   // wear it
        closet_arm(repeat, dir > 0 ? CLOSET_RIGHT : CLOSET_LEFT);
        pthread_mutex_lock(&g_lock); ++g_stats.steps; pthread_mutex_unlock(&g_lock);
        count_chosen();
        return;
    }
}
static void world_step(uint8_t *state) {
    if (!aligned(state)) return;
    ui_tap tap = take_tap(K_WORLD, root_of((uint8_t *)pointer_at(state, WORLD_LAYOUT)));
    int count = int_at(state, WORLD_COUNT), cursor = int_at(state, WORLD_CURSOR);
    uint8_t **array = (uint8_t **)pointer_at(state, WORLD_ROWS);
    const uint8_t *ids = (const uint8_t *)pointer_at(state, WORLD_IDS);
    uintptr_t repeat = pointer_at(state, WORLD_REPEAT);
    if (tap.kind != K_WORLD || count <= 0 || count > 17 || tap.index >= count || cursor < 0 || cursor >= count ||
        !aligned(array) || !aligned(ids) || !aligned((void *)repeat)) return;
    if (tap.index == cursor) { grant_decide(); count_chosen(); return; }
    if (array[cursor]) {
        g_fn.start_action(action_of(array[cursor]), str(STR_WAIT), NULL);
        uint32_t id; memcpy(&id, ids + 4 * (unsigned)cursor, sizeof id);
        g_fn.world_text(array[cursor], str(STR_TXT_CONTENT), id);
    }
    int down = tap.index > 0 || state[WORLD_WRAP];
    put_int(state, WORLD_CURSOR, down ? (tap.index + count - 1) % count : tap.index + 1);
    atomic_store(&g_repeat_ctrl, repeat); atomic_store(&g_repeat_down, down);
    atomic_store(&g_repeat_until, now_ns() + DM_UI_ARM_NS);
    pthread_mutex_lock(&g_lock); ++g_stats.steps; pthread_mutex_unlock(&g_lock);
    count_chosen();
}
// The map: StageSceneStateStageMap (+0x20 MapLayout, +0x28 the adsorb icon);
// MapLayout icons at +0x210 (48-byte entries, count +0x218), zoom at +0x26c.
enum { MAP_LAYOUT = 0x20, MAP_ICON = 0x28, MAP_ICONS = 0x210, MAP_ICON_COUNT = 0x218, MAP_ZOOM = 0x26c };
enum { MAP_WAITING, MAP_SELECTED, MAP_MOVING };
static int map_scale(uint8_t *map, float *sx, float *sy) {
    uint8_t *root = root_of(map);
    if (!root) return 0;
    *sx = fabsf(float_at(root, PANE_MTX)); *sy = fabsf(float_at(root, PANE_MTX + 0x14));
    return *sx > .05f && *sy > .05f;
}
static void map_step(uint8_t *state, int mode) {
    if (!aligned(state)) return;
    uint8_t *map = (uint8_t *)pointer_at(state, MAP_LAYOUT);
    if (!aligned(map)) return;
    float sx, sy;
    if (!map_scale(map, &sx, &sy)) return;
    // The drag reaches the game as the UI left stick, for this nerve only.
    pthread_mutex_lock(&g_lock);
    int dragging = g_dragging; double dx = g_drag_x, dy = g_drag_y; g_drag_x = g_drag_y = 0;
    if (dragging && g_drag_moved) ++g_stats.drags;
    pthread_mutex_unlock(&g_lock);
    t_stick_armed = 0;
    if (dragging) {
        float zoom = float_at(map, MAP_ZOOM), lo = g_fn.map_scale_min(), hi = g_fn.map_scale_max();
        float t = hi > lo ? fminf(fmaxf((zoom - lo) / (hi - lo), 0), 1) : 0, speed = 3.2f + (4.f - 3.2f) * t;
        // The map follows the finger (a stick away from the drag), at a gentle gain.
        float mx = (float)(-dx * UI_LAYOUT_W) / sx, my = (float)(dy * UI_LAYOUT_H) / sy;
        t_stick = (ui_vec2){fminf(fmaxf(mx * DM_UI_MAP_DRAG_GAIN / speed, -1), 1), fminf(fmaxf(my * DM_UI_MAP_DRAG_GAIN / speed, -1), 1)};
        if (hypotf(t_stick.x, t_stick.y) < .0015f) t_stick = (ui_vec2){0, .0015f};   // held still: still scrolling
        t_stick_armed = 1;
        return;
    }
    if (mode == MAP_MOVING) return;
    ui_tap tap = take_tap(K_MAP, root_of(map));
    if (tap.kind != K_MAP) return;
    // The tap in map-local units, as the adsorb search measures icons (from the centre).
    float lx = (float)((tap.x - .5) * UI_LAYOUT_W) / sx, ly = (float)((.5 - tap.y) * UI_LAYOUT_H) / sy;
    int count = int_at(map, MAP_ICON_COUNT);
    uint8_t *icons = (uint8_t *)pointer_at(map, MAP_ICONS), *best = NULL;
    float best_d = DM_UI_ICON_REACH;
    for (int i = 0; aligned(icons) && i < count && i < 2048; i++) {
        uint8_t *entry = icons + 48 * (unsigned)i;
        uint32_t type; memcpy(&type, entry + 12, sizeof type);
        if (!entry[8] || type > 0x11 || !((1u << type) & 0x381c5)) continue;   // the icons adsorb considers
        uint8_t *object = (uint8_t *)pointer_at(entry, 0), *icon = aligned(object) ? (uint8_t *)pointer_at(object, 8) : NULL;
        if (!aligned(icon)) continue;
        ui_vec2 at = g_fn.local_trans(icon + 8), select = g_fn.pane_trans(icon + 8, str(STR_SELECT_POS));
        float d = hypotf(at.x + select.x - lx, at.y + select.y - ly);
        if (d <= best_d) { best_d = d; best = entry; }
    }
    if (!best) return;
    uint8_t *selected = (uint8_t *)pointer_at(state, MAP_ICON);
    if (mode == MAP_SELECTED && selected == best) { grant_decide(); count_chosen(); return; }   // use it
    if (mode == MAP_SELECTED && aligned(selected)) g_fn.map_unselect(map, (void *)pointer_at(selected, 0));
    memcpy(state + MAP_ICON, &best, sizeof best);
    g_fn.set_nerve(state, g_base + MAP_ADSORB);   // the snap, as when the cursor nears it
    count_chosen();
}

static void run(int hook, const void *nerve, void **keeper) {
    uint8_t *object = keeper && aligned(keeper) ? (uint8_t *)*keeper : NULL;
    switch (hook) {
    case H_MENU_SELECT: case H_MENU_SECOND: menu_step(object); break;
    case H_CHOICE_SELECT: case H_CHOICE_SELECT_QUIET: choice_step(object); break;
    case H_LIST_ACTIVE: list_step(object); break;
    case H_WINDOW_WAIT: window_step(object); break;
    case H_TALK_ICON_WAIT: case H_TALK_TEXT: talk_step(object); break;
    case H_WORLD_WAIT: world_step(object); break;
    case H_MAP_WAIT: case H_MAP_SCROLL: map_step(object, MAP_WAITING); break;
    case H_MAP_WAIT_ADSORB: map_step(object, MAP_SELECTED); break;
    case H_MAP_ICON_MOVE: map_step(object, MAP_MOVING); break;
    case H_HLIST_ACTIVE: hlist_step(object); break;
    case H_MODE_WAIT: mode_step(object); break;
    case H_DATA_WAIT: data_step(object); break;
    case H_CLOSET_SELECT: closet_step(object); break;
    }
    g_original[hook](nerve, keeper);
    t_stick_armed = 0;
}
#define HOOK(n) static void hook_##n(const void *nerve, void **keeper) { run(n, nerve, keeper); }
HOOK(H_MENU_SELECT) HOOK(H_MENU_SECOND) HOOK(H_CHOICE_SELECT) HOOK(H_CHOICE_SELECT_QUIET) HOOK(H_LIST_ACTIVE)
HOOK(H_WINDOW_WAIT) HOOK(H_TALK_ICON_WAIT) HOOK(H_TALK_TEXT) HOOK(H_MAP_WAIT) HOOK(H_MAP_WAIT_ADSORB)
HOOK(H_MAP_ICON_MOVE) HOOK(H_MAP_SCROLL) HOOK(H_WORLD_WAIT) HOOK(H_HLIST_ACTIVE) HOOK(H_MODE_WAIT) HOOK(H_DATA_WAIT)
HOOK(H_CLOSET_SELECT)
#undef HOOK
static const nerve_execute_fn k_replacements[H_COUNT] = {
    hook_H_MENU_SELECT, hook_H_MENU_SECOND, hook_H_CHOICE_SELECT, hook_H_CHOICE_SELECT_QUIET, hook_H_LIST_ACTIVE,
    hook_H_WINDOW_WAIT, hook_H_TALK_ICON_WAIT, hook_H_TALK_TEXT, hook_H_MAP_WAIT, hook_H_MAP_WAIT_ADSORB,
    hook_H_MAP_ICON_MOVE, hook_H_MAP_SCROLL, hook_H_WORLD_WAIT, hook_H_HLIST_ACTIVE, hook_H_MODE_WAIT, hook_H_DATA_WAIT,
    hook_H_CLOSET_SELECT,
};

// ---- Result gates (regs[16] = 1 answers with regs[0], or s0/s1 in the saved q0/q1) ----
static void decide_gate(uint64_t *regs) {
    uint64_t until = atomic_load(&g_decide_until);
    if (until && now_ns() <= until && atomic_compare_exchange_strong(&g_decide_until, &until, 0)) { regs[0] = 1; regs[16] = 1; }
}
static void repeat_gate(uint64_t *regs, int down) {
    uint64_t until = atomic_load(&g_repeat_until);
    if (!until || regs[0] != atomic_load(&g_repeat_ctrl) || atomic_load(&g_repeat_down) != down) return;
    if (now_ns() <= until && atomic_compare_exchange_strong(&g_repeat_until, &until, 0)) { regs[0] = 1; regs[16] = 1; }
}
static void intent_gate(uint64_t *regs, unsigned bit) {
    unsigned intent = atomic_load(&g_intent);
    if (!(intent & bit) || now_ns() > atomic_load(&g_intent_until)) return;
    if (atomic_compare_exchange_strong(&g_intent, &intent, 0)) { regs[0] = 1; regs[16] = 1; }
}
#define INTENT_GATE(n, bit) static void n(uint64_t *regs) { intent_gate(regs, bit); }
INTENT_GATE(cancel_gate, I_CANCEL) INTENT_GATE(map_close_gate, I_MAP_CLOSE) INTENT_GATE(list_close_gate, I_LIST_CLOSE)
INTENT_GATE(pause_gate, I_PAUSE) INTENT_GATE(tab_prev_gate, I_TAB_PREV) INTENT_GATE(tab_next_gate, I_TAB_NEXT)
INTENT_GATE(tab_change_gate, I_TAB_CHANGE) INTENT_GATE(y_gate, I_Y) INTENT_GATE(right_gate, I_RIGHT) INTENT_GATE(left_gate, I_LEFT)
#undef INTENT_GATE
static void repeat_up_gate(uint64_t *regs) { repeat_gate(regs, 0); }
static void repeat_down_gate(uint64_t *regs) { repeat_gate(regs, 1); }
static void stick_gate(uint64_t *regs) {
    if (!t_stick_armed) return;
    uint32_t x, y; memcpy(&x, &t_stick.x, 4); memcpy(&y, &t_stick.y, 4);
    regs[32] = x; regs[33] = 0; regs[34] = y; regs[35] = 0;   // q0, q1 in dm_gate's frame
    regs[16] = 1;
}

static void closet_gate(uint64_t *regs, int op) {
    int armed = op;
    if (regs[0] != atomic_load(&g_closet_repeat) || now_ns() > atomic_load(&g_closet_until)) return;
    if (atomic_compare_exchange_strong(&g_closet_op, &armed, CLOSET_NONE)) { regs[0] = 1; regs[16] = 1; }
}
static void closet_left_gate(uint64_t *regs) { closet_gate(regs, CLOSET_LEFT); }
static void closet_right_gate(uint64_t *regs) { closet_gate(regs, CLOSET_RIGHT); }
static void closet_caps_gate(uint64_t *regs) { closet_gate(regs, CLOSET_CAPS); }
static void closet_outfits_gate(uint64_t *regs) { closet_gate(regs, CLOSET_OUTFITS); }
// The gate block saved the caller's x29, x30 just above dm_gate's 800-byte frame.
static uint64_t caller(const uint64_t *regs) { return regs[101]; }
static void demo_seen(const uint64_t *regs) { (void)regs; atomic_store(&g_demo_ns, now_ns()); }
static void ride_seen(const uint64_t *regs) {
    uint64_t lr = caller(regs);
    if (lr >= (uintptr_t)g_base + MOTORCYCLE_LO && lr < (uintptr_t)g_base + MOTORCYCLE_HI) atomic_store(&g_ride_ns, now_ns());
}
// Only the message PadStyle picks (at draw and at text set): touch picks the handheld
// variant (the touch pictures; every other handheld entry reads as the controller's).
static void pad_handheld_gate(uint64_t *regs) {
    uint64_t lr = caller(regs) - (uintptr_t)g_base;
    if ((lr == PAD_STYLE_DRAW || lr == PAD_STYLE_SET) && atomic_load(&g_touch)) { regs[0] = 1; regs[16] = 1; }
}
static void right_stick_gate(uint64_t *regs) {
    int zoom = atomic_load(&g_zoom);
    uint64_t lr = caller(regs) - (uintptr_t)g_base;
    if (!zoom || (lr != MAP_ZOOM_CALLS_0 && lr != MAP_ZOOM_CALLS_1 && lr != MAP_ZOOM_CALLS_2 && lr != MAP_ZOOM_CALLS_3)) return;
    float x = 0, y = zoom * DM_UI_MAP_ZOOM_RATE; uint32_t bx, by; memcpy(&bx, &x, 4); memcpy(&by, &y, 4);
    regs[32] = bx; regs[33] = 0; regs[34] = by; regs[35] = 0;   // q0, q1
    regs[16] = 1;
}

static uint32_t fnv(const uint8_t *p, size_t n) {
    uint32_t h = 0x811c9dc5u;
    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 0x01000193u;
    return h;
}
void dm_odyssey_ui_install(void) {
    const char *option = getenv("DM_ODYSSEY_TAP_UI");
    if ((option && strcmp(option, "1")) || atomic_load(&g_hooked)) return;
    module_t *m = NULL;
    for (int i = 0; i < g_nmod; i++) if (!strcmp(g_mod[i].name, "main")) { m = &g_mod[i]; break; }
    if (!m || strcmp(m->build_id, k_build) || !m->base || m->size <= 0x1309fd0 || m->text_end <= START_ACTION) return;
    uint8_t *b = m->base;
    int ok = 1;
    for (int gate = OBS_ODYSSEY_UI_DECIDE; gate <= OBS_ODYSSEY_UI_TRIGGER_LEFT; gate++) ok = ok && gate_obs_present(gate);
    for (unsigned i = 0; ok && i < H_COUNT; i++) {
        const ui_hook *h = &k_hooks[i];
        size_t len = strlen(h->name) + 1;
        ok = pointer_at(b, h->slot) == (uintptr_t)(b + h->exec) && pointer_at(b, h->slot - 8) == (uintptr_t)(b + h->typeinfo) &&
            pointer_at(b, h->typeinfo + 8) && !memcmp((const char *)pointer_at(b, h->typeinfo + 8), h->name, len) &&
            !memcmp(b + h->exec, h->words, sizeof h->words);
    }
    for (unsigned i = 0; ok && i < sizeof k_code / sizeof *k_code; i++)
        ok = k_code[i].offset + 64 <= m->text_end && fnv(b + k_code[i].offset, 64) == k_code[i].hash;
    ok = ok && pointer_at(b, MENU_DECIDE_PARTS) == (uintptr_t)(b + 0x120dc08) && pointer_at(b, MAP_ADSORB) == (uintptr_t)(b + 0x12cb978) &&
        !strcmp((const char *)b + STR_WAIT, "Wait") && !strcmp((const char *)b + STR_SELECT, "Select") &&
        !strcmp((const char *)b + STR_DECIDE, "\xe6\xb1\xba\xe5\xae\x9a") && !strcmp((const char *)b + STR_SELECT_POS, "SelectPos") &&
        !strcmp((const char *)b + STR_TXT_CONTENT, "TxtContent") && !strcmp((const char *)b + STR_CURSOR, "Cursor");
    if (!ok) { LOG("[RED] Odyssey tap UI: screen guard failed; screens keep controller input only"); return; }
    g_base = b; g_text_lo = (uintptr_t)b; g_text_hi = (uintptr_t)b + m->text_end;
    g_fn.start_action = (start_action_fn)(b + START_ACTION); g_fn.hit_reaction = (hit_reaction_fn)(b + HIT_REACTION);
    g_fn.set_nerve = (set_nerve_fn)(b + SET_NERVE); g_fn.menu_mario = (object_fn)(b + MENU_MARIO);
    g_fn.list_left = (object_fn)(b + LIST_LEFT); g_fn.list_right = (object_fn)(b + LIST_RIGHT);
    g_fn.world_text = (world_text_fn)(b + WORLD_TEXT); g_fn.local_trans = (local_trans_fn)(b + LOCAL_TRANS);
    g_fn.pane_trans = (pane_trans_fn)(b + PANE_TRANS); g_fn.map_scale_min = (scalar_fn)(b + MAP_SCALE_MIN);
    g_fn.map_scale_max = (scalar_fn)(b + MAP_SCALE_MAX);
    g_fn.map_select = (object_arg_fn)(b + MAP_SELECT); g_fn.map_unselect = (object_arg_fn)(b + MAP_UNSELECT);
    g_fn.pane_trans_3d = (pane_trans_3d_fn)(b + PANE_TRANS_3D); g_fn.mode_refresh = (mode_refresh_fn)(b + MODE_REFRESH);
    gate_ret_set(OBS_ODYSSEY_UI_DECIDE, decide_gate);
    gate_ret_set(OBS_ODYSSEY_UI_REPEAT_UP, repeat_up_gate);
    gate_ret_set(OBS_ODYSSEY_UI_REPEAT_DOWN, repeat_down_gate);
    gate_ret_set(OBS_ODYSSEY_UI_LEFT_STICK, stick_gate);
    gate_ret_set(OBS_ODYSSEY_UI_CANCEL, cancel_gate); gate_ret_set(OBS_ODYSSEY_UI_MAP_CLOSE, map_close_gate);
    gate_ret_set(OBS_ODYSSEY_UI_LIST_CLOSE, list_close_gate); gate_ret_set(OBS_ODYSSEY_UI_PAUSE, pause_gate);
    gate_ret_set(OBS_ODYSSEY_UI_TAB_PREV, tab_prev_gate); gate_ret_set(OBS_ODYSSEY_UI_TAB_NEXT, tab_next_gate);
    gate_ret_set(OBS_ODYSSEY_UI_TAB_CHANGE, tab_change_gate);
    gate_obs_set(OBS_ODYSSEY_UI_DEMO, demo_seen); gate_obs_set(OBS_ODYSSEY_UI_RIDE, ride_seen);
    gate_ret_set(OBS_ODYSSEY_UI_PAD_HANDHELD, pad_handheld_gate);
    gate_ret_set(OBS_ODYSSEY_UI_CLOSET_LEFT, closet_left_gate); gate_ret_set(OBS_ODYSSEY_UI_CLOSET_RIGHT, closet_right_gate);
    gate_ret_set(OBS_ODYSSEY_UI_CLOSET_CAPS, closet_caps_gate); gate_ret_set(OBS_ODYSSEY_UI_CLOSET_OUTFITS, closet_outfits_gate);
    gate_ret_set(OBS_ODYSSEY_UI_Y, y_gate); gate_ret_set(OBS_ODYSSEY_UI_RIGHT_STICK, right_stick_gate);
    gate_ret_set(OBS_ODYSSEY_UI_REPEAT_RIGHT, right_gate); gate_ret_set(OBS_ODYSSEY_UI_TRIGGER_RIGHT, right_gate);
    gate_ret_set(OBS_ODYSSEY_UI_REPEAT_LEFT, left_gate); gate_ret_set(OBS_ODYSSEY_UI_TRIGGER_LEFT, left_gate);
    for (unsigned i = 0; i < H_COUNT; i++) {
        g_original[i] = (nerve_execute_fn)(b + k_hooks[i].exec);
        *(nerve_execute_fn *)(b + k_hooks[i].slot) = k_replacements[i];
    }
    atomic_store(&g_hooked, 1);
    pthread_mutex_lock(&g_lock); g_stats.hooked = 1; pthread_mutex_unlock(&g_lock);
    LOG("Odyssey tap UI: exact 1.4.1 menus, lists, windows, dialogue, mode select, kingdom, map and wardrobe nerves hooked");
}

// ---- Footer slide ----
static float footer_locked(uint64_t now) {
    float to = g_footer_shown ? 0 : DM_UI_FOOTER_DROP;
    if (!g_footer_ns || now - g_footer_ns >= DM_UI_FOOTER_SLIDE_NS) return to;
    float t = (float)(now - g_footer_ns) / (float)DM_UI_FOOTER_SLIDE_NS;
    float eased = t < .5f ? 4 * t * t * t : 1 - powf(-2 * t + 2, 3) / 2;   // ease in and out
    return g_footer_from + (to - g_footer_from) * eased;
}
void dm_odyssey_ui_footer(int shown) {
    uint64_t now = now_ns();
    if (atomic_exchange(&g_touch, !shown) != !shown) atomic_store(&g_flip_ns, now);   // guides hidden: touch is in use; a switch rebuilds text
    pthread_mutex_lock(&g_lock);
    if (!shown != !g_footer_shown) {
        g_footer_from = footer_locked(now);   // a reversal starts from where the slide is
        g_footer_shown = shown != 0; g_footer_ns = now;
    }
    pthread_mutex_unlock(&g_lock);
}
float dm_odyssey_ui_footer_drop(void) {
    uint64_t now = now_ns();
    pthread_mutex_lock(&g_lock); float drop = footer_locked(now); pthread_mutex_unlock(&g_lock);
    return drop;
}
void dm_odyssey_ui_snapshot(dm_odyssey_ui_stats *out) {
    if (!out) return;
    pthread_mutex_lock(&g_lock); *out = g_stats; pthread_mutex_unlock(&g_lock);
}
