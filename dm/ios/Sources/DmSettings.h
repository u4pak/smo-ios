// Deliberate gear entry point; custom settings never overlay gameplay as buttons.
#pragma once
#import <UIKit/UIKit.h>
void dm_settings_present(UIViewController *presenter);
void dm_settings_apply_boot_preferences(void);
void dm_gamepad_settings_visible(BOOL visible);
BOOL dm_gamepad_touch_with_controller(void);
void dm_gamepad_set_touch_with_controller(BOOL enabled);
BOOL dm_gamepad_controller_connected(void);
double dm_gamepad_look_sensitivity(void);
void dm_gamepad_set_look_sensitivity(double sensitivity);
