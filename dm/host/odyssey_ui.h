// Odyssey 1.4.1 tappable UI. The HUD adapter's Calculate hook reports every
// primary layout it draws; known screens publish their rows as tap targets in
// normalized picture coordinates. Each screen's own update (its nerve's execute
// slot) then takes a tap meant for it and acts with the game's own steps:
// title/pause menu, options and their lists, confirmation windows, dialogue
// (tap to continue, tap a choice), the kingdom select and the map (drag to
// scroll, tap an icon to snap to it, tap it again to use it).
#pragma once
#include <stdint.h>

void dm_odyssey_ui_install(void);
// Called by the HUD adapter after a primary layout's Calculate (rendered edits in place).
void dm_odyssey_ui_observe(const char *owner, uint8_t *root);

// What the screen shows, from the layouts drawn recently.
enum {
    DM_UI_UNKNOWN = 0,   // nothing classified (boot, loading)
    DM_UI_MENU,          // a screen that takes taps (menus, options, dialogue, map, kingdom select, wardrobe, cutscenes)
    DM_UI_OTHER,         // a screen without tap support that needs the controls (none known today)
    DM_UI_GAME,          // the gameplay HUD is up
    DM_UI_LOADING,       // a loading screen: nothing to do
};
int dm_odyssey_ui_available(void);   // the HUD adapter and the screen hooks are installed
int dm_odyssey_ui_screen(void);
// A tap at (x, y), normalized on the picture with the origin top left. Returns 1
// when a tappable screen takes it (a row, a dialogue, the map).
int dm_odyssey_ui_tap(double x, double y);
// A drag step (normalized picture units, y down); ended marks the release.
void dm_odyssey_ui_drag(double dx, double dy, int ended);
// The target id under (x, y) for highlighting/ownership, 0 for none.
int dm_odyssey_ui_hit(double x, double y);
// The screen has a way back (cancel / close) that touch has no button for.
int dm_odyssey_ui_back_available(void);
int dm_odyssey_ui_back(void);   // answers the screen's next cancel or close check once

// Footer actions the screen offers with no touch control of its own: shown as
// buttons where the hidden controller guide was. Lists (map, kingdom select),
// Reset (option settings) and Random (wardrobe) answer the screen's check once;
// zoom (map) is held: +1 in, -1 out, 0 released.
enum { DM_UI_ACTION_LISTS = 1, DM_UI_ACTION_RESET = 2, DM_UI_ACTION_RANDOM = 4, DM_UI_ACTION_ZOOM = 8 };
unsigned dm_odyssey_ui_actions(void);
int dm_odyssey_ui_action(unsigned action);
void dm_odyssey_ui_zoom(int direction);
// The player rides the moped (its jump check ran recently) with the gameplay HUD up.
int dm_odyssey_ui_riding(void);

// Controller guides (the ParFooter part under a layout's All or root, and the
// button icons of dialogue and confirmation windows): shown while a controller
// is in use, slid or shrunk away for touch. Hidden guides also mean touch is in
// use: message pad pictures then draw their touch (handheld) variants.
void dm_odyssey_ui_footer(int shown);
float dm_odyssey_ui_footer_drop(void);   // layout units below its place, now
#define DM_UI_FOOTER_DROP 90.0f          // the 62-unit guide band plus margin
#define DM_UI_FOOTER_SLIDE_NS UINT64_C(300000000)

#define DM_UI_RECENT_NS UINT64_C(150000000)       // a layout drawn this recently is on screen
#define DM_UI_GAME_RECENT_NS UINT64_C(300000000)
#define DM_UI_TAP_NS UINT64_C(300000000)          // a tap not taken by its screen this soon is dropped
#define DM_UI_ARM_NS UINT64_C(100000000)          // a granted decide / step expires unless asked for this soon
#define DM_UI_ICON_REACH 40.0f                    // map-local units around an icon's select point
#define DM_UI_MAP_DRAG_GAIN 0.3f                  // map selector travel per unit of finger travel
#define DM_UI_MAP_ZOOM_RATE 0.5f                  // right stick deflection while a zoom button is held
#define DM_UI_RIDE_NS UINT64_C(300000000)         // the moped asked this recently: still riding
#define DM_UI_TEXT_REFRESH_NS UINT64_C(500000000) // after a touch / controller switch, drawn text rebuilds for this long

typedef struct {
    uint64_t taps, chosen, decides, steps, drags, dropped, backs, actions;
    int hooked;
} dm_odyssey_ui_stats;
void dm_odyssey_ui_snapshot(dm_odyssey_ui_stats *out);
