// Virtual gamepad (Nintendo button positions) + physical controllers (GameController). Both are merged and handed to the host through dm_input_set (hle_hid.c, bit layout in dm_input.h).
//   Full Odyssey controls are permanent. Legacy DmPadLayout values are ignored
//   without changing stored data: corner left stick and ABXY, D-pad, an
//   invisible swipe-to-look surface in the right half, shoulders, +/- and L3/R3.
//   The deliberate gear opens the settings switches.
//   Touch-down ownership: exclusive gear first, then locked keys (shoulders,
//   − / +, L3 / R3), then the nearest diamond, D-pad or left stick grab radius,
//     then in 3D the look surface for a touch in the right half: every sample
//     turns Odyssey's follow camera directly (dm_odyssey_camera_turn) by its
//     distance times the sensitivity setting; other cameras, which read only a
//     stick, get the distance as a turn budget the right stick drains (dm_tc_look).
//   Everything hugs the edges and corners to leave the picture room; controls sit over the picture, one CAShapeLayer each: a press only swaps the fill, a stick only moves its knob, nothing redraws the whole screen.
//   Touches no control takes (in 3D, those outside the right third) go to the game's touch screen (pushGame; Super Mario Galaxy's pointer in handheld mode).
//   Odyssey's screens are tapped directly (odyssey_ui.h): on a tappable screen
//   (menus, options, confirmations, dialogue, the map, kingdom select) one touch
//   after the gear is a pointer: lifted where it went down it is a tap, moved
//   further it drags (the map scrolls). The controls stay hidden from boot until
//   the gameplay HUD is up, and on tappable screens. The game's controller
//   guides (footers, dialogue button icons) slide away on any touch and back
//   when a controller connects or is used. In the 3D layout + and − show pause
//   and map symbols. Screens whose way out is B get a Back button (top left)
//   while touch is in use; it answers the screen's own cancel or close once.
//   An unrecognized screen that stays up with the controls hidden brings them
//   back after a short wait, so nothing can be left without navigation.
//   Footer actions with no touch control (Lists, Reset, Random, map zoom) get
//   buttons at the bottom right, where the hidden controller guide was. On the
//   moped the diamond becomes a big pedal (Y, accelerate; it tilts back while
//   held) and a smaller up-arrow button (B, jump).
//   Gameplay drawings hide with a controller unless "Touch controls with
//   controller" is on; then they fade while the controller is used and return
//   on the next touch. The gear remains available and consumes its own touches.
//   Controllers are picked up when they connect, become current, or finish
//   attaching their gamepad profile. Physical buttons remain mapped by position.
#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>
#import <GameController/GameController.h>
#import <CoreText/CoreText.h>
#include <pthread.h>
#include "dm_input.h"
#include "dm.h"
#include "front.h"
#include "game_profile.h"
#include "dm_touch_controls.h"
#include "dm_display_geometry.h"
#include "odyssey_display.h"
#include "odyssey_camera.h"
#include "odyssey_ui.h"
#import "DmGearTouch.h"
#import "DmControllerFeedback.h"
#import "DmSettings.h"
#include <stdatomic.h>

enum { B_A = 0, B_B, B_X, B_Y, B_LS, B_RS, B_L, B_R, B_ZL, B_ZR, B_PLUS, B_MINUS, B_LEFT, B_UP, B_RIGHT, B_DOWN, B_N };
enum { C_NONE = 0, C_STICK, C_DPAD, C_FACE, C_BTN, C_GEAR, C_GAME, C_UI, C_BACK, C_ACTION };

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static uint64_t g_touch_btn, g_pad_btn;
static int32_t g_touch_lx, g_touch_ly, g_touch_rx, g_touch_ry, g_pad_lx, g_pad_ly, g_pad_rx, g_pad_ry;
static GCController *g_selected_pad;
static _Atomic uint64_t g_pad_epoch;
static _Atomic int g_settings_visible;
static NSString *const k_touch_with_controller = @"DmTouchWithController";
static NSString *const k_look_sensitivity = @"DmLookSensitivity";
static BOOL g_touch_in_use = YES;          // the last input was a touch (Back button, hidden guides)
static void set_touch_in_use(BOOL touch) { g_touch_in_use = touch; dm_odyssey_ui_footer(!touch); }
static _Atomic double g_look_sensitivity;   // cached for the touch path; 0 = not read yet
static uint64_t ns(double seconds) { return seconds > 0 ? (uint64_t)(seconds * 1e9) : 0; }   // UITouch and CACurrentMediaTime share the uptime clock
static int tilted(int32_t x, int32_t y) { return (int64_t)x * x + (int64_t)y * y > (int64_t)4000 * 4000; }
static void publish(void) {
    pthread_mutex_lock(&g_mu);
    if (atomic_load(&g_settings_visible)) {
        dm_input_set(0,0,0,0,0);pthread_mutex_unlock(&g_mu);return;
    }
    int32_t lx = g_touch_lx, ly = g_touch_ly, rx = g_touch_rx, ry = g_touch_ry;
    if (tilted(g_pad_lx, g_pad_ly)) { lx = g_pad_lx; ly = g_pad_ly; }   // a tilted physical stick wins
    if (tilted(g_pad_rx, g_pad_ry)) { rx = g_pad_rx; ry = g_pad_ry; }
    dm_input_set(g_touch_btn | g_pad_btn, lx, ly, rx, ry);
    pthread_mutex_unlock(&g_mu);
}

static const int k_btns[] = {B_ZL, B_L, B_ZR, B_R, B_MINUS, B_PLUS, B_LS, B_RS};   // keys locked at touch-down (L3 / R3 only in the 3D layout)

@interface DmGamepad : UIView
- (void)openSettings;
- (BOOL)fullLayout;
- (void)padsChanged;
@end
static __weak DmGamepad *g_view;
@interface DmGearAccessibility : UIAccessibilityElement
@property (weak) DmGamepad *pad;
@end
@implementation DmGearAccessibility
- (BOOL)accessibilityActivate { [self.pad openSettings]; return YES; }
@end

// Small ordered receipt distinguishes touch delivery from presenter rejection.
static uint64_t gearBegins,gearReady,gearRejected,presented;
static void settings_report(NSString *reason) {
    NSDictionary *state=@{@"build":NSBundle.mainBundle.infoDictionary[@"CFBundleVersion"]?:@"unknown",
        @"gearBegins":@(gearBegins),@"tapReleasesAccepted":@(gearReady),@"tapReleasesRejected":@(gearRejected),@"gesture":@"tap-release",
        @"presented":@(presented),@"visible":@(atomic_load(&g_settings_visible)),@"fullControls":@YES,@"reason":reason};
    static dispatch_queue_t queue;static dispatch_once_t once;
    dispatch_once(&once,^{queue=dispatch_queue_create("odyssey.touch-settings-diagnostics",DISPATCH_QUEUE_SERIAL);});
    dispatch_async(queue,^{
        NSString *docs=NSSearchPathForDirectoriesInDomains(NSDocumentDirectory,NSUserDomainMask,YES).firstObject;
        NSData *data=[NSJSONSerialization dataWithJSONObject:state options:NSJSONWritingPrettyPrinted error:nil];
        [data writeToFile:[docs stringByAppendingPathComponent:@"touch-settings.json"] options:NSDataWritingAtomic error:nil];
    });
}

@implementation DmGamepad {
    NSMapTable<UITouch *, NSNumber *> *_owner;     // touch → control (with the key for C_BTN or the stick for C_STICK, << 8)
    UITouch *_stick[2]; CGPoint _stickNow[2];      // the touch holding the left stick / the look surface, and where it is now
    dm_tc_look _look;                              // the swipe turn budget the right stick drains
    double _lookX, _lookY;                         // the right-stick deflection from the last drain step
    CADisplayLink *_lookLink;                      // drains the budget every frame while a swipe is held or still turning
    unsigned _padRetries;                          // re-checks while a connected controller has no gamepad profile yet
    uint64_t _faceBits, _dpadBits, _btnBits, _gearToken;
    NSMapTable<UITouch *, NSNumber *> *_faceOf;    // which keys each touch on the diamond currently holds
    CAShapeLayer *_key[B_N]; CATextLayer *_label[B_N];   // one layer per key
    CAShapeLayer *_ring, *_knob;                   // the left stick (the right one is not drawn)
    UIImageView *_gearIcon;
    dm_tc_gear _gear;
    UITouch *_gearTouch;
    CGRect _previousBounds; UIEdgeInsets _previousInsets;
    DmGearAccessibility *_gearAccessibility;
    BOOL _stickShown;                              // the left stick was held at the last refresh
    BOOL _full;                                    // the 3D layout is current
    CALayer *_controls;                            // holds every control layer: hidden while a physical controller is connected (the view itself stays, for game touches)
    BOOL _padOff;                                  // a physical controller is connected: every touch goes to the game
    BOOL _uiHidden;                                // off gameplay (boot, loading, tappable menus): the controls are faded out
    BOOL _faded;                                   // the controls are drawn transparent (off gameplay, or the controller is in use)
    UITouch *_uiTouch; CGPoint _uiStart, _uiLast; BOOL _uiDragging;   // the pointer on a tappable screen
    CALayer *_glyph[B_N];                          // symbol keys (+ pause, − map) instead of text
    UIImageView *_backIcon; CGRect _backRect; BOOL _backShown;   // Back on screens whose way out is B
    CAShapeLayer *_pedal, *_jump; CALayer *_jumpGlyph; BOOL _moped;   // the moped's pedal (Y) and jump (B)
    unsigned _actions; int _actionCount;                           // footer action buttons now shown
    UIView *_actionView[5]; CGRect _actionRect[5]; unsigned _actionKind[5]; int _actionZoom[5];
    CFTimeInterval _unknownSince; BOOL _seenScreen;   // the fallback for screens the observer does not know
    NSTimer *_screenTimer;                         // follows the game's screen (dm_odyssey_ui_screen)
    UITouch *_game[16];                            // touches on the game's touch screen by finger slot
}

static UIColor *k_idle, *k_on, *k_line, *k_ringFill, *k_text, *k_textOn;
static CAShapeLayer *shape(CALayer *parent) {
    CAShapeLayer *l = [CAShapeLayer layer];
    l.fillColor = k_idle.CGColor; l.strokeColor = k_line.CGColor; l.lineWidth = 1.5;
    l.shadowColor = UIColor.blackColor.CGColor; l.shadowOpacity = DM_TC_SHADOW_ALPHA; l.shadowRadius = 3; l.shadowOffset = CGSizeMake(0, 1);
    [parent addSublayer:l]; return l;
}
static CATextLayer *label(CALayer *parent, NSString *s) {
    CATextLayer *t = [CATextLayer layer];
    t.string = s; t.alignmentMode = kCAAlignmentCenter; t.foregroundColor = k_text.CGColor; t.contentsScale = UIScreen.mainScreen.scale;
    [parent addSublayer:t]; return t;
}
static CALayer *glyph(CALayer *parent, NSString *symbol) {   // a white SF Symbol at the label's opacity
    UIImageSymbolConfiguration *config = [UIImageSymbolConfiguration configurationWithPointSize:30 weight:UIImageSymbolWeightBold];
    UIImage *image = [[UIImage systemImageNamed:symbol withConfiguration:config] imageWithTintColor:UIColor.whiteColor renderingMode:UIImageRenderingModeAlwaysOriginal];
    CALayer *l = [CALayer layer];
    if (image) {
        UIImage *flat = [[[UIGraphicsImageRenderer alloc] initWithSize:image.size] imageWithActions:^(UIGraphicsImageRendererContext *context) {
            (void)context; [image drawAtPoint:CGPointZero]; }];
        l.contents = (__bridge id)flat.CGImage; l.contentsScale = flat.scale;
    }
    l.contentsGravity = kCAGravityResizeAspect; l.opacity = DM_TC_TEXT_ALPHA;
    [parent addSublayer:l]; return l;
}
static BOOL fullOnly(int b) { return b == B_LS || b == B_RS || b >= B_LEFT; }   // keys shown only in the 3D layout

- (instancetype)initWithFrame:(CGRect)f {
    if ((self = [super initWithFrame:f])) {
        self.multipleTouchEnabled = YES; self.opaque = NO; self.backgroundColor = UIColor.clearColor;
        _owner = [NSMapTable strongToStrongObjectsMapTable]; _faceOf = [NSMapTable strongToStrongObjectsMapTable];
        static dispatch_once_t once;
        dispatch_once(&once, ^{
            // each colour carries its own low opacity instead of lowering the whole view's alpha (that would add an off-screen pass for the full-screen layer)
            k_idle = [UIColor colorWithWhite:1 alpha:DM_TC_IDLE_ALPHA]; k_on = [UIColor colorWithWhite:1 alpha:DM_TC_PRESSED_ALPHA]; k_line = [UIColor colorWithWhite:1 alpha:DM_TC_STROKE_ALPHA];
            k_ringFill = [UIColor colorWithWhite:1 alpha:DM_TC_RING_ALPHA]; k_text = [UIColor colorWithWhite:1 alpha:DM_TC_TEXT_ALPHA]; k_textOn = [UIColor colorWithWhite:1 alpha:DM_TC_PRESSED_TEXT_ALPHA];
        });
        _controls = [CALayer layer]; [self.layer addSublayer:_controls];
        _ring = shape(_controls); _ring.fillColor = k_ringFill.CGColor;
        _knob = shape(_controls);
        CAShapeLayer *in = [CAShapeLayer layer]; in.fillColor = nil; in.strokeColor = k_line.CGColor; in.lineWidth = 1.5;   // inner ring of the knob
        [_knob addSublayer:in];
        static NSString *const names[B_N] = {@"A", @"B", @"X", @"Y", @"L3", @"R3", @"L", @"R", @"ZL", @"ZR"};   // + and − are symbols
        for (int b = 0; b < B_N; b++) {
            _key[b] = shape(_controls);
            if (names[b]) _label[b] = label(_key[b], names[b]);   // the D-pad arms carry no label
        }
        _glyph[B_PLUS] = glyph(_key[B_PLUS], @"pause.fill"); _glyph[B_MINUS] = glyph(_key[B_MINUS], @"map.fill");
        _pedal = shape(_controls); _pedal.fillRule = kCAFillRuleEvenOdd; _pedal.anchorPoint = CGPointMake(.5, 1); _pedal.hidden = YES;
        _jump = shape(_controls); _jump.hidden = YES; _jumpGlyph = glyph(_jump, @"arrow.up");
        UIImageSymbolConfiguration *symbol=[UIImageSymbolConfiguration configurationWithPointSize:DM_TC_GEAR_ICON_SIZE weight:UIImageSymbolWeightRegular];
        _gearIcon=[[UIImageView alloc] initWithImage:[UIImage systemImageNamed:@DM_TC_GEAR_SYMBOL withConfiguration:symbol]];
        _gearIcon.tintColor=UIColor.whiteColor; _gearIcon.contentMode=UIViewContentModeScaleAspectFit;
        _gearIcon.alpha=DM_TC_GEAR_ALPHA; _gearIcon.userInteractionEnabled=NO;
        [self addSubview:_gearIcon];
        _gearAccessibility = [[DmGearAccessibility alloc] initWithAccessibilityContainer:self];
        _gearAccessibility.pad = self; _gearAccessibility.accessibilityLabel = @"Settings";
        _gearAccessibility.accessibilityHint = @"Tap to open settings";
        _gearAccessibility.accessibilityTraits = UIAccessibilityTraitButton;
        _gearAccessibility.accessibilityCustomActions = @[[[UIAccessibilityCustomAction alloc] initWithName:@"Open settings" actionHandler:^BOOL(UIAccessibilityCustomAction *action) {
            (void)action; [g_view openSettings]; return YES;
        }]];
        self.accessibilityElements = @[_gearAccessibility];
        _full = YES; // Old compact preferences must never remove camera controls.
        [self applyLayout];
        NSNotificationCenter *nc = NSNotificationCenter.defaultCenter;
        [nc addObserver:self selector:@selector(padsChanged) name:GCControllerDidConnectNotification object:nil];
        [nc addObserver:self selector:@selector(padsChanged) name:GCControllerDidDisconnectNotification object:nil];
        [nc addObserver:self selector:@selector(padsChanged) name:GCControllerDidBecomeCurrentNotification object:nil];
        _lookLink = [CADisplayLink displayLinkWithTarget:self selector:@selector(lookTick)];
        _lookLink.paused = YES; [_lookLink addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
        _uiHidden = _faded = YES; _controls.opacity = 0;   // nothing to control until the game says so
        __weak DmGamepad *weak = self;
        _screenTimer = [NSTimer timerWithTimeInterval:0.1 repeats:YES block:^(NSTimer *timer) { (void)timer; [weak screenTick]; }];
        [NSRunLoop.mainRunLoop addTimer:_screenTimer forMode:NSRunLoopCommonModes];
        [self padsChanged];
        set_touch_in_use(g_selected_pad == nil);   // button guides only with a controller
        UIImageSymbolConfiguration *back = [UIImageSymbolConfiguration configurationWithPointSize:30 weight:UIImageSymbolWeightSemibold];
        _backIcon = [[UIImageView alloc] initWithImage:[UIImage systemImageNamed:@"chevron.backward.circle.fill" withConfiguration:back]];
        _backIcon.tintColor = UIColor.whiteColor; _backIcon.contentMode = UIViewContentModeScaleAspectFit;
        _backIcon.alpha = 0; _backIcon.userInteractionEnabled = NO; _backIcon.accessibilityLabel = @"Back";
        [self addSubview:_backIcon];
    }
    return self;
}
- (void)applyLayout {   // show / hide the controls for the current layout
    [CATransaction begin]; [CATransaction setDisableActions:YES];
    for (int b = 0; b < B_N; b++) _key[b].hidden = fullOnly(b) && !_full;
    [CATransaction commit];
}
- (BOOL)fullLayout { return _full; }
- (NSUInteger)gameplayTouchCount {
    NSUInteger count = 0;
    for (UITouch *touch in _owner) if (([_owner objectForKey:touch].unsignedLongLongValue & 0xff) != C_GEAR) ++count;
    return count;
}
- (void)openSettings {
    if (UIApplication.sharedApplication.applicationState!=UIApplicationStateActive) { settings_report(@"application-inactive"); return; }
    if (atomic_load(&g_settings_visible)) { settings_report(@"already-visible"); return; }
    UIViewController *presenter=self.window.rootViewController;
    if (!presenter) { settings_report(@"no-window-presenter"); return; }
    if (presenter.presentedViewController) { settings_report(@"another-modal-presented"); return; }
    [self reset];
    dm_settings_present(presenter);
    if (presenter.presentedViewController) { ++presented; settings_report(@"presented"); }
    else { dm_gamepad_settings_visible(NO); settings_report(@"presentation-rejected"); }
}
- (BOOL)openSettingsAccessibility:(UIAccessibilityCustomAction *)action { (void)action; [self openSettings]; return YES; }

// ---- Layout (points; computed inside the safe area) ----
- (dm_tc_layout)touchLayout {
    UIEdgeInsets i=self.safeAreaInsets;
    return dm_tc_make_layout(self.bounds.size.width, self.bounds.size.height, (dm_tc_insets){i.top,i.left,i.bottom,i.right});
}
static CGPoint cgPoint(dm_tc_point p) { return CGPointMake(p.x,p.y); }
static CGRect cgRect(dm_tc_rect r) { return CGRectMake(r.x,r.y,r.width,r.height); }
- (CGFloat)u { return self.touchLayout.unit; }
- (CGPoint)faceCenter { return cgPoint(self.touchLayout.face); }
- (CGFloat)faceGap { return DM_TC_FACE_GAP * self.u; }
- (CGFloat)faceR { return DM_TC_FACE_R * self.u; }
- (CGPoint)facePos:(int)b { return cgPoint(dm_tc_face_position(self.touchLayout,b)); }
- (CGPoint)stickHome { return cgPoint(self.touchLayout.left_stick); }
- (CGFloat)stickR { return DM_TC_STICK_R * self.u; }
- (BOOL)inRightArea:(CGPoint)p { CGRect b = self.bounds; return p.x >= CGRectGetMaxX(b) - b.size.width * DM_TC_RSWIPE; }
- (CGPoint)dpadCenter { return cgPoint(self.touchLayout.dpad); }
- (CGFloat)dpadR { return DM_TC_DPAD_R * self.u; }
static BOOL isDrop(int b) { return b == B_ZL || b == B_L || b == B_ZR || b == B_R; }
static BOOL isClick(int b) { return b == B_LS || b == B_RS; }
- (CGPoint)dropCenter:(int)b { return cgPoint(dm_tc_shoulder(self.touchLayout,b==B_ZR||b==B_R,b==B_ZL||b==B_ZR)); }
- (CGFloat)dropR:(int)b { return (b == B_ZL || b == B_ZR ? DM_TC_ZL_R : DM_TC_L_R) * self.u; }
- (CGPoint)clickCenter:(int)b { return cgPoint(dm_tc_click(self.touchLayout,b==B_RS)); }
- (CGRect)pillRect:(int)b { return cgRect(dm_tc_pill(self.touchLayout,b==B_PLUS)); }

static CGFloat dist(CGPoint a, CGPoint b) { return hypot(a.x - b.x, a.y - b.y); }
- (int)btnAt:(CGPoint)p {   // locked keys (tested on touch-down only, then locked)
    CGFloat u = self.u;
    for (int i = 0; i < (int)(sizeof k_btns / sizeof *k_btns); i++) { int b = k_btns[i];
        if (fullOnly(b) && !_full) continue;
        BOOL hit = isDrop(b) ? dist(p, [self dropCenter:b]) <= [self dropR:b] * 1.35
                 : isClick(b) ? dist(p, [self clickCenter:b]) <= DM_TC_CLICK_R * u * 1.35
                 : CGRectContainsPoint(cgRect(dm_tc_pill_hit(self.touchLayout,b==B_PLUS)),p);
        if (hit) return b; }
    return -1;
}
- (uint64_t)faceAt:(CGPoint)p {   // keys under the finger in the diamond (between two keys both count)
    uint64_t m = 0; CGFloat r = self.faceR * 1.25;
    for (int b = B_A; b <= B_Y; b++) if (dist(p, [self facePos:b]) <= r) m |= 1ull << b;
    return m;
}
- (uint64_t)dpadAt:(CGPoint)p {   // 8 directions: 45° each, diagonals press two keys; a small centre circle counts as none
    CGPoint c = self.dpadCenter; CGFloat dx = p.x - c.x, dy = p.y - c.y;
    if (hypot(dx, dy) < self.dpadR * 0.25) return 0;
    double a = atan2(-dy, dx) * 180 / M_PI; uint64_t m = 0;
    if (a > -67.5 && a < 67.5) m |= 1ull << B_RIGHT;
    if (a > 22.5 && a < 157.5) m |= 1ull << B_UP;
    if (a > 112.5 || a < -112.5) m |= 1ull << B_LEFT;
    if (a < -22.5 && a > -157.5) m |= 1ull << B_DOWN;
    return m;
}
- (int)mopedAt:(CGPoint)p {   // the pedal (Y) or the jump button (B), with a little slack
    dm_tc_layout l = self.touchLayout; CGFloat u = l.unit;
    if (CGRectContainsPoint(CGRectInset(cgRect(dm_tc_pedal(l)), -10 * u, -10 * u), p)) return B_Y;
    if (dist(p, cgPoint(dm_tc_jump(l))) <= DM_TC_JUMP_R * u * 1.4) return B_B;
    return -1;
}
- (CGPoint)knobOffset {   // left knob offset from the stick centre (clamped to the stick radius)
    if (!_stick[0]) return CGPointZero;
    CGPoint h = self.stickHome; CGFloat dx = _stickNow[0].x - h.x, dy = _stickNow[0].y - h.y, d = hypot(dx, dy), R = self.stickR;
    if (d > R) { dx *= R / d; dy *= R / d; }
    return CGPointMake(dx, dy);
}

// ---- Touch ----
- (void)recompute {
    uint64_t face = 0; for (UITouch *t in _faceOf) face |= [_faceOf objectForKey:t].unsignedLongLongValue;
    uint64_t btn = 0, dpad = 0;
    for (UITouch *t in _owner) { uint64_t v = [_owner objectForKey:t].unsignedLongLongValue; int c = (int)(v & 0xff);
        if (c == C_BTN) btn |= 1ull << (v >> 8);
        else if (c == C_DPAD) dpad |= [self dpadAt:[t locationInView:self]]; }
    int32_t ax[4] = {0};
    CGPoint o = self.knobOffset; CGFloat R = self.stickR, d = hypot(o.x, o.y), dz = R * DM_TC_DEAD;
    if (d > dz) {
        CGFloat m = fmin((d - dz) / (R * DM_TC_FULL - dz), 1) / d;   // outside the dead zone: 0 rising to full at the full-tilt radius
        ax[0] = (int32_t)lrint(o.x * m * 32767); ax[1] = (int32_t)lrint(-o.y * m * 32767);   // host: up is positive; rounded so full tilt is exactly 32767
    }
    ax[2] = (int32_t)lrint(_lookX * 32767); ax[3] = (int32_t)lrint(_lookY * 32767);
    BOOL changed = face != _faceBits || dpad != _dpadBits || btn != _btnBits;
    _faceBits = face; _dpadBits = dpad; _btnBits = btn;
    pthread_mutex_lock(&g_mu);
    g_touch_btn = face | dpad | btn; g_touch_lx = ax[0]; g_touch_ly = ax[1]; g_touch_rx = ax[2]; g_touch_ry = ax[3];
    pthread_mutex_unlock(&g_mu);
    publish();
    BOOL stick = _stick[0] != nil;   // only the left stick is drawn: the right one needs no refresh
    if (changed || stick || _stickShown) [self refresh];   // also refresh on the stick release so the knob returns to the centre
    _stickShown = stick;
}
// Touches no control takes (all of them while a physical controller is connected) go to the game's touch screen, normalized on the 16:9 picture that front_ios.m
// centres in the screen: Super Mario Galaxy's pointer in handheld mode (profile op_mode). The host reports them only in handheld mode, as on the Switch (hle_hid.c touch_read)
- (void)pushGame {
    if (atomic_load(&g_settings_visible)) { dm_touch_set(NULL,0);return; }
    CGRect b = self.bounds;
    dm_display_geometry geometry=dm_display_make(b.size.width,b.size.height,1,dm_odyssey_display_scene_aspect());
    if (!geometry.valid) { dm_touch_set(NULL,0); return; }
    double w=geometry.picture.width, h=geometry.picture.height;
    double x0=geometry.picture.x, y0=geometry.picture.y;
    dm_touch t[16]; int n = 0;
    for (int i = 0; i < 16; i++) if (_game[i]) {
        CGPoint p = [_game[i] locationInView:self];
        t[n++] = (dm_touch){(uint32_t)i, (float)fmin(fmax((p.x - x0) / w, 0), 0.999), (float)fmin(fmax((p.y - y0) / h, 0), 0.999)};
    }
    dm_touch_set(t, n);
}
// A view point on the picture, normalized with the origin top left (as pushGame).
- (BOOL)picturePoint:(CGPoint)p x:(double *)x y:(double *)y {
    CGRect b = self.bounds;
    dm_display_geometry geometry=dm_display_make(b.size.width,b.size.height,1,dm_odyssey_display_scene_aspect());
    if (!geometry.valid || !(geometry.picture.width > 0) || !(geometry.picture.height > 0)) return NO;
    *x = (p.x - geometry.picture.x) / geometry.picture.width; *y = (p.y - geometry.picture.y) / geometry.picture.height;
    return YES;
}
- (BOOL)tappableScreen { return dm_odyssey_ui_available() && dm_odyssey_ui_screen() == DM_UI_MENU; }
// Before the guest reads input nothing is drawn; without the observer (16:9
// builds) the controls behave as before. Unknown screens keep the last state.
- (void)screenTick {
    BOOL hide = _uiHidden;
    int screen = dm_odyssey_ui_available() ? dm_odyssey_ui_screen() : DM_UI_UNKNOWN;
    CFTimeInterval now = CACurrentMediaTime();
    if (dm_input_samples() <= 65536) hide = YES;
    else if (!dm_odyssey_ui_available()) hide = NO;
    else switch (screen) {
        case DM_UI_MENU: hide = YES; _seenScreen = YES; break;
        case DM_UI_OTHER: case DM_UI_GAME: hide = NO; _seenScreen = YES; break;
        default: break;
    }
    // Hidden on a screen nobody recognizes (not loading) for a while: show the
    // controls, so an unsupported menu can always be navigated.
    if (screen == DM_UI_UNKNOWN && hide && _seenScreen) {
        if (!_unknownSince) _unknownSince = now;
        else if (now - _unknownSince > DM_TC_UNKNOWN_SHOW) hide = NO;
    } else if (screen != DM_UI_UNKNOWN) _unknownSince = 0;
    BOOL moped = _full && !hide && !_padOff && dm_odyssey_ui_available() && dm_odyssey_ui_riding();
    if (moped != _moped) [self setMoped:moped];
    unsigned actions = hide && g_touch_in_use && dm_odyssey_ui_available() ? dm_odyssey_ui_actions() : 0;
    if (actions != _actions) [self showActions:actions];
    BOOL back = hide && g_touch_in_use && dm_odyssey_ui_back_available();
    if (back != _backShown) {
        _backShown = back;
        [UIView animateWithDuration:0.2 animations:^{ self->_backIcon.alpha = back ? DM_TC_BACK_ALPHA : 0; }];
    }
    if (hide != _uiHidden) { _uiHidden = hide; [self reset]; }
    [self fadeControls:YES];
}
// With "Touch controls with controller" on, the drawings get out of the way
// while the controller is used and come back on the next touch (which still
// presses what it lands on). Off gameplay they stay faded either way.
- (void)fadeControls:(BOOL)settle {
    BOOL faded = _uiHidden || (g_selected_pad != nil && !_padOff && !g_touch_in_use);
    if (faded == _faded) return;
    _faded = faded;
    CALayer *shown = _controls.presentationLayer ?: _controls;
    CABasicAnimation *fade = [CABasicAnimation animationWithKeyPath:@"opacity"];
    fade.fromValue = @(shown.opacity); fade.toValue = @(faded ? 0 : 1);
    fade.duration = faded ? 0.2 : settle ? 0.6 : 0.15;
    fade.timingFunction = [CAMediaTimingFunction functionWithName:kCAMediaTimingFunctionEaseInEaseOut];
    if (!faded && settle) { fade.beginTime = CACurrentMediaTime() + 0.25; fade.fillMode = kCAFillModeBackwards; }   // into gameplay: let the HUD settle first
    [CATransaction begin]; [CATransaction setDisableActions:YES];
    _controls.opacity = faded ? 0 : 1; [_controls addAnimation:fade forKey:@"fade"];
    [CATransaction commit];
}
// The moped swaps the diamond for the pedal and jump button; touches on the old
// keys are let go so nothing stays held across the swap.
- (void)setMoped:(BOOL)on {
    _moped = on;
    for (UITouch *t in [_owner.keyEnumerator allObjects]) {
        uint64_t v = [_owner objectForKey:t].unsignedLongLongValue; int c = (int)(v & 0xff), key = (int)(v >> 8);
        if (c == C_FACE || (c == C_BTN && (key == B_Y || key == B_B))) { [_owner removeObjectForKey:t]; [_faceOf removeObjectForKey:t]; }
    }
    [CATransaction begin]; [CATransaction setDisableActions:YES];
    for (int b = B_A; b <= B_Y; b++) _key[b].hidden = on;
    _pedal.hidden = _jump.hidden = !on;
    [CATransaction commit];
    [self recompute];
}
static NSString *const k_action_symbol[] = {@"list.bullet", @"arrow.counterclockwise", @"shuffle", @"plus.magnifyingglass", @"minus.magnifyingglass"};
static NSString *const k_action_title[] = {@"Lists", @"Reset", @"Random", nil, nil};
// Footer actions as buttons at the bottom right, right to left in the guide's order.
- (void)showActions:(unsigned)actions {
    for (int i = 0; i < _actionCount; i++) {
        UIView *v = _actionView[i]; _actionView[i] = nil;
        [UIView animateWithDuration:0.15 animations:^{ v.alpha = 0; } completion:^(BOOL done) { (void)done; [v removeFromSuperview]; }];
    }
    for (UITouch *t in [_owner.keyEnumerator allObjects])
        if (([_owner objectForKey:t].unsignedLongLongValue & 0xff) == C_ACTION) [_owner removeObjectForKey:t];
    dm_odyssey_ui_zoom(0);
    _actions = actions; _actionCount = 0;
    static const struct { unsigned kind; int item, zoom; } order[] = {
        {DM_UI_ACTION_ZOOM, 4, -1}, {DM_UI_ACTION_ZOOM, 3, 1}, {DM_UI_ACTION_LISTS, 0, 0}, {DM_UI_ACTION_RESET, 1, 0}, {DM_UI_ACTION_RANDOM, 2, 0}};
    UIEdgeInsets safe = self.safeAreaInsets;
    CGFloat h = DM_TC_ACTION_H, right = self.bounds.size.width - fmax(safe.right, 12) - 6, bottom = self.bounds.size.height - fmax(safe.bottom, 10) - 6;
    UIImageSymbolConfiguration *config = [UIImageSymbolConfiguration configurationWithPointSize:17 weight:UIImageSymbolWeightSemibold];
    for (unsigned i = 0; i < sizeof order / sizeof *order; i++) {
        if (!(actions & order[i].kind)) continue;
        NSString *title = k_action_title[order[i].item];
        UIImageView *icon = [[UIImageView alloc] initWithImage:[UIImage systemImageNamed:k_action_symbol[order[i].item] withConfiguration:config]];
        icon.tintColor = UIColor.whiteColor; icon.contentMode = UIViewContentModeCenter;
        UILabel *label = nil; CGFloat w = h;
        if (title) {
            label = [UILabel new]; label.text = title; label.textColor = UIColor.whiteColor;
            label.font = [UIFont systemFontOfSize:16 weight:UIFontWeightSemibold]; [label sizeToFit];
            w = 14 + 22 + 6 + label.bounds.size.width + 16;
        }
        UIView *v = [[UIView alloc] initWithFrame:CGRectMake(right - w, bottom - h, w, h)];
        v.backgroundColor = [UIColor colorWithWhite:0 alpha:.45]; v.layer.cornerRadius = h / 2;
        v.layer.borderColor = [UIColor colorWithWhite:1 alpha:.25].CGColor; v.layer.borderWidth = 1;
        v.userInteractionEnabled = NO; v.isAccessibilityElement = YES;
        v.accessibilityLabel = title ?: (order[i].zoom > 0 ? @"Zoom in" : @"Zoom out");
        icon.frame = title ? CGRectMake(14, 0, 22, h) : v.bounds; [v addSubview:icon];
        if (label) { label.frame = CGRectMake(14 + 22 + 6, 0, label.bounds.size.width, h); [v addSubview:label]; }
        v.alpha = 0; [self addSubview:v];
        [UIView animateWithDuration:0.25 animations:^{ v.alpha = DM_TC_ACTION_ALPHA; }];
        _actionView[_actionCount] = v; _actionRect[_actionCount] = v.frame;
        _actionKind[_actionCount] = order[i].kind; _actionZoom[_actionCount] = order[i].zoom;
        ++_actionCount; right -= w + DM_TC_ACTION_GAP;
    }
}
- (BOOL)gameBegin:(UITouch *)t {
    for (int i = 0; i < 16; i++) if (!_game[i]) { _game[i] = t; [_owner setObject:@(C_GAME | (uint64_t)i << 8) forKey:t]; return YES; }
    return NO;
}
- (void)touchesBegan:(NSSet<UITouch *> *)ts withEvent:(UIEvent *)e {
    BOOL game = NO;
    if (ts.count) { set_touch_in_use(YES); [self fadeControls:NO]; }   // touch is in use: no controller button guides, the drawings back
    for (UITouch *t in ts) {
        CGPoint p = [t locationInView:self];
        if (dm_tc_contains(self.touchLayout.gear,(dm_tc_point){p.x,p.y})) {
            ++gearBegins;settings_report(@"gear-touch-began");
            [_owner setObject:@(C_GEAR) forKey:t];
            if (!_gearTouch) {
                _gearTouch=t;
                // A resting gameplay thumb must not silently reject a deliberate
                // hold. Presentation neutralizes all current input.
                _gearToken=dm_tc_gear_begin(&_gear,(dm_tc_point){p.x,p.y},now_ns(),0);
            } else dm_tc_gear_block(&_gear);
            continue;
        }
        if (_backShown && CGRectContainsPoint(_backRect, p)) { [_owner setObject:@(C_BACK) forKey:t]; continue; }
        int action = -1;
        for (int i = 0; i < _actionCount; i++) if (CGRectContainsPoint(CGRectInset(_actionRect[i], -6, -6), p)) action = i;
        if (action >= 0) {   // held: a zoom button zooms until released
            [_owner setObject:@(C_ACTION | (uint64_t)action << 8) forKey:t];
            _actionView[action].alpha = 1;
            if (_actionZoom[action]) dm_odyssey_ui_zoom(_actionZoom[action]);
            continue;
        }
        if ([self tappableScreen]) {   // one pointer; other fingers are ignored
            if (!_uiTouch) { _uiTouch = t; _uiStart = _uiLast = p; _uiDragging = NO; [_owner setObject:@(C_UI) forKey:t]; }
            continue;
        }
        if (_padOff) { game |= [self gameBegin:t]; continue; }
        if (_uiHidden) { game |= [self gameBegin:t]; continue; }   // nothing drawn to press
        int hit = [self btnAt:p];
        if (hit < 0 && _moped) hit = [self mopedAt:p];
        if (hit >= 0) { [_owner setObject:@(C_BTN | (uint64_t)hit << 8) forKey:t]; continue; }
        // diamond, D-pad, left stick: take the one with the smallest distance / grab radius, if it is <= 1
        CGFloat best = 1; int kind = C_NONE, idx = 0;
        CGFloat f = dist(p, self.faceCenter) / (self.faceGap + self.faceR * 1.6); if (f <= best && !_moped) { best = f; kind = C_FACE; }
        if (_full) { CGFloat d = dist(p, self.dpadCenter) / (self.dpadR * DM_TC_DPAD_GRAB); if (d < best) { best = d; kind = C_DPAD; } }
        if (!_stick[0]) { CGFloat s = dist(p, self.stickHome) / (self.stickR * DM_TC_STICK_GRAB); if (s < best) { best = s; kind = C_STICK; } }
        if (kind == C_NONE && _full && !_stick[1] && [self inRightArea:p]) {   // the look surface: from here on the swipe turns the camera
            kind = C_STICK; idx = 1; dm_tc_look_begin(&_look, (dm_tc_point){p.x, p.y}, ns(CACurrentMediaTime())); _lookLink.paused = NO;
        }
        if (kind == C_FACE) { [_owner setObject:@(C_FACE) forKey:t]; [_faceOf setObject:@([self faceAt:p]) forKey:t]; }
        else if (kind == C_DPAD) [_owner setObject:@(C_DPAD) forKey:t];
        else if (kind == C_STICK) { [_owner setObject:@(C_STICK | (uint64_t)idx << 8) forKey:t]; _stick[idx] = t; _stickNow[idx] = p; }
        else game |= [self gameBegin:t];
    }
    if (game) [self pushGame];
    [self recompute];
}
- (void)touchesMoved:(NSSet<UITouch *> *)ts withEvent:(UIEvent *)e {
    BOOL game = NO;
    for (UITouch *t in ts) {
        uint64_t v = [_owner objectForKey:t].unsignedLongLongValue; int c = (int)(v & 0xff); CGPoint p = [t locationInView:self];
        if (c == C_GEAR) dm_gear_observe_actual_samples(&_gear,t,e,self);
        else if (c == C_FACE) [_faceOf setObject:@([self faceAt:p]) forKey:t];
        else if (c == C_STICK) { _stickNow[v >> 8] = p;
            if ((v >> 8) == 1) {   // every coalesced sample turns the camera by its distance
                double gain = dm_gamepad_look_sensitivity(), dx, dy;
                for (UITouch *sample in [e coalescedTouchesForTouch:t] ?: @[t]) {
                    CGPoint q = [sample locationInView:self];
                    if (!dm_tc_look_take(&_look, (dm_tc_point){q.x, q.y}, self.u / gain, &dx, &dy) || (dx == 0 && dy == 0)) continue;
                    if (!dm_odyssey_camera_turn(dx * DM_TC_LOOK_DEGREES, -dy * DM_TC_LOOK_DEGREES)) { _look.dx += dx; _look.dy += dy; }
                } } }
        else if (c == C_GAME) game = YES;
        else if (c == C_UI && t == _uiTouch) {
            if (!_uiDragging && hypot(p.x - _uiStart.x, p.y - _uiStart.y) > DM_TC_UI_SLOP) _uiDragging = YES;
            double x0, y0, x1, y1;
            if (_uiDragging && [self picturePoint:_uiLast x:&x0 y:&y0] && [self picturePoint:p x:&x1 y:&y1]) dm_odyssey_ui_drag(x1 - x0, y1 - y0, 0);
            if (_uiDragging) _uiLast = p;
        }
    }
    if (game) [self pushGame];
    [self recompute];
}
- (void)endTouches:(NSSet<UITouch *> *)ts tapped:(BOOL)ended event:(UIEvent *)event {
    BOOL game = NO, open=NO;
    for (UITouch *t in ts) {
        uint64_t v = [_owner objectForKey:t].unsignedLongLongValue;
        if ((v & 0xff)==C_GEAR) {
            if (t==_gearTouch) {
                dm_gear_observe_actual_samples(&_gear,t,event,self);
                CGPoint p=[t locationInView:self];
                if (ended&&dm_tc_gear_tap(&_gear,_gearToken,(dm_tc_point){p.x,p.y},self.touchLayout.gear)) open=YES;
                else { ++gearRejected;settings_report(@"tap-moved-cancelled-or-conflicted"); }
                _gearTouch=nil;
            }
            dm_tc_gear_cancel(&_gear);
        }
        if ((v & 0xff) == C_GAME && (v >> 8) < 16) { _game[v >> 8] = nil; game = YES; }
        if ((v & 0xff) == C_BACK && ended && CGRectContainsPoint(CGRectInset(_backRect, -12, -12), [t locationInView:self])) dm_odyssey_ui_back();
        if ((v & 0xff) == C_ACTION && (int)(v >> 8) < _actionCount) {
            int i = (int)(v >> 8);
            _actionView[i].alpha = DM_TC_ACTION_ALPHA;
            if (_actionZoom[i]) dm_odyssey_ui_zoom(0);
            else if (ended && CGRectContainsPoint(CGRectInset(_actionRect[i], -12, -12), [t locationInView:self])) dm_odyssey_ui_action(_actionKind[i]);
        }
        if ((v & 0xff) == C_UI && t == _uiTouch) {
            double x, y;
            if (_uiDragging) dm_odyssey_ui_drag(0, 0, 1);
            else if (ended && [self picturePoint:_uiStart x:&x y:&y]) dm_odyssey_ui_tap(x, y);
            _uiTouch = nil; _uiDragging = NO;
        }
        [_owner removeObjectForKey:t]; [_faceOf removeObjectForKey:t]; for (int i = 0; i < 2; i++) if (t == _stick[i]) _stick[i] = nil;
        if (!_stick[1] && _look.touching) dm_tc_look_release(&_look);   // the rest of the swipe still turns
    }
    if (game) [self pushGame];
    [self recompute];
    if (open) { ++gearReady;[self openSettings]; }
}
- (void)touchesEnded:(NSSet<UITouch *> *)ts withEvent:(UIEvent *)e { [self endTouches:ts tapped:YES event:e]; }
- (void)touchesCancelled:(NSSet<UITouch *> *)ts withEvent:(UIEvent *)e { [self endTouches:ts tapped:NO event:e]; }
- (void)reset { dm_odyssey_ui_zoom(0); if (_uiDragging) dm_odyssey_ui_drag(0, 0, 1); _uiTouch = nil; _uiDragging = NO; dm_tc_gear_cancel(&_gear); _gearTouch=nil; [_owner removeAllObjects]; [_faceOf removeAllObjects]; _stick[0] = _stick[1] = nil; dm_tc_look_cancel(&_look); _lookX = _lookY = 0; _lookLink.paused = YES; for (int i = 0; i < 16; i++) _game[i] = nil; [self pushGame]; [self recompute]; }
- (void)lookTick {
    dm_tc_look_step(&_look, ns(CACurrentMediaTime()), &_lookX, &_lookY);
    [self recompute];
    if (!dm_tc_look_active(&_look)) _lookLink.paused = YES;
}

// ---- Shapes ----
static CGPathRef circlePath(CGPoint c, CGFloat r) { return CGPathCreateWithEllipseInRect(CGRectMake(c.x - r, c.y - r, 2 * r, 2 * r), NULL); }
static CGPathRef dropPath(CGPoint c, CGFloat r, CGFloat tipAngle, CGFloat round) {   // teardrop: circle + two tangents meeting at the tip, tip slightly rounded
    CGFloat d = r * DM_TC_DROP_TIP, th = acos(r / d);
    CGPoint tip = CGPointMake(c.x + d * cos(tipAngle), c.y + d * sin(tipAngle));
    CGPoint p1 = CGPointMake(c.x + r * cos(tipAngle + th), c.y + r * sin(tipAngle + th)), p2 = CGPointMake(c.x + r * cos(tipAngle - th), c.y + r * sin(tipAngle - th));
    CGMutablePathRef p = CGPathCreateMutable();
    CGPathMoveToPoint(p, NULL, p2.x, p2.y);
    CGPathAddArcToPoint(p, NULL, tip.x, tip.y, p1.x, p1.y, round);
    CGPathAddArc(p, NULL, c.x, c.y, r, tipAngle + th, tipAngle - th + 2 * M_PI, false);
    CGPathCloseSubpath(p); return p;
}
static CGPathRef armPath(CGPoint c, CGFloat R, CGFloat w, CGFloat in, CGFloat angle, CGFloat round) {   // one D-pad arm: an upward pentagon (tip toward the centre) rotated by angle around the centre
    CGPoint v[5] = {{-w, -R}, {w, -R}, {w, -(in + w)}, {0, -in}, {-w, -(in + w)}};
    CGFloat cs = cos(angle), sn = sin(angle);
    for (int i = 0; i < 5; i++) v[i] = CGPointMake(c.x + v[i].x * cs - v[i].y * sn, c.y + v[i].x * sn + v[i].y * cs);
    CGMutablePathRef p = CGPathCreateMutable();
    CGPathMoveToPoint(p, NULL, (v[4].x + v[0].x) / 2, (v[4].y + v[0].y) / 2);
    for (int i = 0; i < 5; i++) CGPathAddArcToPoint(p, NULL, v[i].x, v[i].y, v[(i + 1) % 5].x, v[(i + 1) % 5].y, round);
    CGPathCloseSubpath(p); return p;
}
static void setPath(CAShapeLayer *l, CGPathRef p) { l.path = p; l.shadowPath = p; CGPathRelease(p); }
static CGPathRef pedalPath(CGFloat w, CGFloat h, CGFloat u) {   // a pad with grooves (even-odd holes) on a hinge bar
    CGMutablePathRef p = CGPathCreateMutable();
    CGFloat bar = 10 * u, pad = h - bar - 4 * u;
    CGPathAddRoundedRect(p, NULL, CGRectMake(w * .08, 0, w * .84, pad), 14 * u, 14 * u);
    for (int i = 0; i < 4; i++) {
        CGFloat y = pad * (.2 + .19 * i);
        CGPathAddRoundedRect(p, NULL, CGRectMake(w * .24, y, w * .52, 5 * u), 2.5 * u, 2.5 * u);
    }
    CGPathAddRoundedRect(p, NULL, CGRectMake(0, h - bar, w, bar), bar / 2, bar / 2);
    return p;
}
static void placeLabel(CATextLayer *t, CGPoint c, CGFloat fs) {
    CTFontRef f = CTFontCreateUIFontForLanguage(kCTFontUIFontEmphasizedSystem, fs, NULL);
    t.font = f; t.fontSize = fs;
    CGFloat h = CTFontGetAscent(f) + CTFontGetDescent(f) + CTFontGetLeading(f), w = fs * 3;
    t.frame = CGRectMake(c.x - w / 2, c.y - h / 2, w, h);
    CFRelease(f);
}

- (void)layoutSubviews {
    [super layoutSubviews];
    if (!CGRectEqualToRect(_previousBounds,self.bounds)||!UIEdgeInsetsEqualToEdgeInsets(_previousInsets,self.safeAreaInsets)) {
        [self reset]; _previousBounds=self.bounds; _previousInsets=self.safeAreaInsets;
        if (_actionCount) [self showActions:_actions];   // re-place the footer buttons
    }
    CGFloat u = self.u;
    [CATransaction begin]; [CATransaction setDisableActions:YES];
    _controls.frame = self.layer.bounds;
    for (int b = B_A; b <= B_Y; b++) { CGPoint c = [self facePos:b]; setPath(_key[b], circlePath(c, self.faceR)); placeLabel(_label[b], c, 20 * u); }
    for (int i = 0; i < (int)(sizeof k_btns / sizeof *k_btns); i++) { int b = k_btns[i];
        if (isDrop(b)) {
            BOOL right = b == B_ZR || b == B_R; CGPoint c = [self dropCenter:b];
            setPath(_key[b], dropPath(c, [self dropR:b], right ? -M_PI_4 : -3 * M_PI_4, 4 * u)); placeLabel(_label[b], c, 16 * u);   // tip toward the near top corner
        } else if (isClick(b)) {
            CGPoint c = [self clickCenter:b]; setPath(_key[b], circlePath(c, DM_TC_CLICK_R * u)); placeLabel(_label[b], c, 13 * u);
        } else {
            CGRect rc = [self pillRect:b];
            setPath(_key[b], CGPathCreateWithRoundedRect(rc, rc.size.height / 2, rc.size.height / 2, NULL)); placeLabel(_label[b], CGPointMake(CGRectGetMidX(rc), CGRectGetMidY(rc)), 18 * u);
            CGFloat g = 15 * u; _glyph[b].frame = CGRectMake(CGRectGetMidX(rc) - g / 2, CGRectGetMidY(rc) - g / 2, g, g);
        }
    }
    static const struct { int b; CGFloat angle; } arms[] = {{B_UP, 0}, {B_RIGHT, M_PI_2}, {B_DOWN, M_PI}, {B_LEFT, -M_PI_2}};   // the same arm in 90° steps
    for (int i = 0; i < 4; i++) setPath(_key[arms[i].b], armPath(self.dpadCenter, self.dpadR, DM_TC_ARM_W * u, DM_TC_ARM_IN * u, arms[i].angle, 6 * u));
    CGRect gear=cgRect(self.touchLayout.gear);
    _gearIcon.frame=CGRectInset(gear,(DM_TC_GEAR_SIZE-DM_TC_GEAR_ICON_SIZE)/2,(DM_TC_GEAR_SIZE-DM_TC_GEAR_ICON_SIZE)/2);
    _gearAccessibility.accessibilityFrameInContainerSpace=gear;
    UIEdgeInsets safe = self.safeAreaInsets;
    _backRect = CGRectMake(safe.left + 10, fmax(safe.top, 8) + 6, DM_TC_BACK_SIZE, DM_TC_BACK_SIZE);
    _backIcon.frame = CGRectInset(_backRect, 6, 6);
    dm_tc_rect pedal = dm_tc_pedal(self.touchLayout);   // anchored at its foot so a press tilts it back
    _pedal.bounds = CGRectMake(0, 0, pedal.width, pedal.height);
    _pedal.position = CGPointMake(pedal.x + pedal.width / 2, pedal.y + pedal.height);
    setPath(_pedal, pedalPath(pedal.width, pedal.height, u));
    CGPoint jump = cgPoint(dm_tc_jump(self.touchLayout)); CGFloat jr = DM_TC_JUMP_R * u, jg = 20 * u;
    setPath(_jump, circlePath(jump, jr)); _jumpGlyph.frame = CGRectMake(jump.x - jg / 2, jump.y - jg / 2, jg, jg);
    setPath(_ring, circlePath(self.stickHome, self.stickR));
    CGFloat k = DM_TC_KNOB_R * u; _knob.bounds = CGRectMake(0, 0, 2 * k, 2 * k);
    setPath(_knob, circlePath(CGPointMake(k, k), k));
    CAShapeLayer *in = (CAShapeLayer *)_knob.sublayers.firstObject; CGPathRef ip = circlePath(CGPointMake(k, k), k * 0.62);
    in.frame = _knob.bounds; in.path = ip; CGPathRelease(ip);
    [CATransaction commit];
    [self refresh];
}
- (void)safeAreaInsetsDidChange { [super safeAreaInsetsDidChange]; [self setNeedsLayout]; }
- (void)refresh {   // pressed state → fill, sticks → knob position
    uint64_t all = _faceBits | _dpadBits | _btnBits;
    [CATransaction begin]; [CATransaction setDisableActions:YES];
    for (int b = 0; b < B_N; b++) { BOOL on = (all >> b) & 1;
        _key[b].fillColor = (on ? k_on : k_idle).CGColor; _label[b].foregroundColor = (on ? k_textOn : k_text).CGColor;
        _glyph[b].opacity = on ? DM_TC_PRESSED_TEXT_ALPHA : DM_TC_TEXT_ALPHA; }
    BOOL accel = (all >> B_Y) & 1, jumpOn = (all >> B_B) & 1;
    _pedal.fillColor = (accel ? k_on : k_idle).CGColor;
    _jump.fillColor = (jumpOn ? k_on : k_idle).CGColor; _jumpGlyph.opacity = jumpOn ? DM_TC_PRESSED_TEXT_ALPHA : DM_TC_TEXT_ALPHA;
    CATransform3D tilt = CATransform3DIdentity; tilt.m34 = -1.0 / 500;
    _pedal.transform = accel ? CATransform3DRotate(tilt, DM_TC_PEDAL_TILT * M_PI / 180, 1, 0, 0) : CATransform3DIdentity;   // pressed: leans back
    CGPoint h = self.stickHome, o = self.knobOffset;
    _knob.position = CGPointMake(h.x + o.x, h.y + o.y); _knob.fillColor = (_stick[0] ? k_on : k_idle).CGColor;
    [CATransaction commit];
}

// ---- Physical controllers ----
static int32_t axis(float v) { return (int32_t)(fmaxf(-1.f, fminf(1.f, v)) * 32767); }
static void read_pad(GCExtendedGamepad *g) {
    uint64_t m = 0;
    #define K(btn, bit) if (btn.isPressed) m |= 1ull << bit
    K(g.buttonB, B_A); K(g.buttonA, B_B); K(g.buttonY, B_X); K(g.buttonX, B_Y);   // by position: right A, bottom B, top X, left Y
    K(g.leftShoulder, B_L); K(g.rightShoulder, B_R);
    pthread_mutex_lock(&g_mu); uint64_t prior = g_pad_btn; pthread_mutex_unlock(&g_mu);
    if (g.leftTrigger.value > ((prior & DM_BTN_ZL) ? 0.08f : 0.14f)) m |= DM_BTN_ZL;
    if (g.rightTrigger.value > ((prior & DM_BTN_ZR) ? 0.08f : 0.14f)) m |= DM_BTN_ZR;
    K(g.buttonMenu, B_PLUS); if (g.buttonOptions) K(g.buttonOptions, B_MINUS);
    if (g.leftThumbstickButton) K(g.leftThumbstickButton, B_LS); if (g.rightThumbstickButton) K(g.rightThumbstickButton, B_RS);
    K(g.dpad.left, B_LEFT); K(g.dpad.up, B_UP); K(g.dpad.right, B_RIGHT); K(g.dpad.down, B_DOWN);
    #undef K
    pthread_mutex_lock(&g_mu);
    g_pad_btn = m; g_pad_lx = axis(g.leftThumbstick.xAxis.value); g_pad_ly = axis(g.leftThumbstick.yAxis.value);
    g_pad_rx = axis(g.rightThumbstick.xAxis.value); g_pad_ry = axis(g.rightThumbstick.yAxis.value);
    pthread_mutex_unlock(&g_mu);
    dm_controller_feedback_travel(g.leftTrigger.value, g.rightTrigger.value);
    publish();
    pthread_mutex_lock(&g_mu); int used = m || tilted(g_pad_lx, g_pad_ly) || tilted(g_pad_rx, g_pad_ry); pthread_mutex_unlock(&g_mu);
    if (used) set_touch_in_use(NO);   // the controller is in use: show its button guides
}
- (void)padsChanged {
    if (!NSThread.isMainThread) { dispatch_async(dispatch_get_main_queue(), ^{ [self padsChanged]; }); return; }
    NSArray<GCController *> *controllers = GCController.controllers;
    GCController *chosen = g_selected_pad && [controllers containsObject:g_selected_pad] ? g_selected_pad : nil;
    for (GCController *c in controllers) if (c.extendedGamepad) {
        if (!chosen) chosen = c;
        if ([c.extendedGamepad isKindOfClass:GCDualSenseGamepad.class]) { chosen = c; break; }
    }
    if (chosen != g_selected_pad) {
        uint64_t epoch = atomic_fetch_add(&g_pad_epoch, 1) + 1;
        g_selected_pad.extendedGamepad.valueChangedHandler = nil;
        g_selected_pad = chosen;
        set_touch_in_use(chosen == nil);   // connected: guides slide up; gone: touch only
        pthread_mutex_lock(&g_mu); g_pad_btn = 0; g_pad_lx = g_pad_ly = g_pad_rx = g_pad_ry = 0; pthread_mutex_unlock(&g_mu);
        dm_controller_feedback_attach(chosen);
        chosen.extendedGamepad.valueChangedHandler = ^(GCExtendedGamepad *g, GCControllerElement *el) {
            (void)el; if (atomic_load(&g_pad_epoch) == epoch) read_pad(g);
        };
    }
    if (chosen) read_pad(chosen.extendedGamepad); else publish();
    // A controller can connect before its gamepad profile is attached; look again shortly.
    if (!chosen && controllers.count && _padRetries < 8) {
        ++_padRetries;
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 250 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{ [self padsChanged]; });
    } else if (chosen || !controllers.count) _padRetries = 0;
    BOOL off = chosen != nil && !dm_gamepad_touch_with_controller();
    if (off != self->_padOff || self->_controls.hidden != off) {   // switch input modes only when they change
        self->_padOff = off; self->_controls.hidden = off; [self reset];
    }
}
@end

BOOL dm_gamepad_touch_with_controller(void) {   // on unless turned off
    id stored = [NSUserDefaults.standardUserDefaults objectForKey:k_touch_with_controller];
    return [stored isKindOfClass:NSNumber.class] ? [stored boolValue] : YES;
}
BOOL dm_gamepad_controller_connected(void) { return g_selected_pad != nil; }
double dm_gamepad_look_sensitivity(void) {
    double cached = atomic_load(&g_look_sensitivity);
    if (cached > 0) return cached;
    id stored = [NSUserDefaults.standardUserDefaults objectForKey:k_look_sensitivity];
    double s = dm_tc_look_sensitivity([stored isKindOfClass:NSNumber.class] ? [stored doubleValue] : 1);
    atomic_store(&g_look_sensitivity, s);
    return s;
}
void dm_gamepad_set_look_sensitivity(double sensitivity) {
    double s = dm_tc_look_sensitivity(sensitivity);
    [NSUserDefaults.standardUserDefaults setDouble:s forKey:k_look_sensitivity];
    atomic_store(&g_look_sensitivity, s);
}
void dm_gamepad_set_touch_with_controller(BOOL enabled) {
    [NSUserDefaults.standardUserDefaults setBool:enabled forKey:k_touch_with_controller];
    if (NSThread.isMainThread) [g_view padsChanged]; else dispatch_async(dispatch_get_main_queue(), ^{ [g_view padsChanged]; });
}
UIView *dm_gamepad_view(CGRect frame) { DmGamepad *v = [[DmGamepad alloc] initWithFrame:frame]; g_view = v; return v; }
void dm_gamepad_geometry_changed(void) {
    // Reflow cancels touches/gear only. Keep physical input, selected controller
    // ownership, motion and feedback epochs intact while folding or resizing.
    if (NSThread.isMainThread) [g_view reset]; else dispatch_async(dispatch_get_main_queue(), ^{ [g_view reset]; });
}
void dm_gamepad_reset(void) {   // On resign active: release everything (a key held while switching away and released in background gets no "release")
    if (NSThread.isMainThread) [g_view reset];else dispatch_async(dispatch_get_main_queue(), ^{ [g_view reset]; });
    pthread_mutex_lock(&g_mu); g_pad_btn = 0; g_pad_lx = g_pad_ly = g_pad_rx = g_pad_ry = 0; pthread_mutex_unlock(&g_mu); publish();
}
void dm_gamepad_settings_visible(BOOL visible) {
    atomic_store(&g_settings_visible,visible);[g_view reset];settings_report(visible?@"opening":@"dismissed");
    if (!visible&&g_selected_pad) read_pad(g_selected_pad.extendedGamepad);else publish();
}
