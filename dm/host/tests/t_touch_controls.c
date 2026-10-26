// Canonical geometry and gear gesture boundaries, with no game or UIKit input.
#include "dm_touch_controls.h"
#include <stdio.h>
#include <string.h>
static int checks;
#define CHECK(c) do { ++checks; if (!(c)) { fprintf(stderr,"failed line %d: %s\n",__LINE__,#c); return 1; } } while (0)
static int near(double a,double b) { return fabs(a-b)<.000001; }
int main(void) {
    CHECK(DM_TC_VERSION==8);
    CHECK(near(DM_TC_IDLE_ALPHA,.06)&&near(DM_TC_PRESSED_ALPHA,.18));
    CHECK(near(DM_TC_TEXT_ALPHA,.32)&&near(DM_TC_RING_ALPHA,.025));
    CHECK(!strcmp(DM_TC_GEAR_SYMBOL,"gearshape")&&DM_TC_GEAR_ICON_SIZE==20&&DM_TC_GEAR_REGULAR_WEIGHT);
    for (int height=280;height<=1100;height+=10) {
        dm_tc_layout l=dm_tc_make_layout(1.8*height,height,(dm_tc_insets){0,30,21,40});
        CHECK(near(l.unit,fmin(height/430.0,1.3)));
        CHECK(l.gear.width==44&&l.gear.height==44&&l.gear.y>=l.safe.y);
        dm_tc_rect minus=dm_tc_pill_hit(l,0),plus=dm_tc_pill_hit(l,1);
        CHECK(l.gear.x-(minus.x+minus.width)>=DM_TC_GEAR_GAP-.000001);
        CHECK(plus.x-(l.gear.x+l.gear.width)>=DM_TC_GEAR_GAP-.000001);
        CHECK(near(l.face.y,l.left_stick.y));
        CHECK(l.dpad.x>l.left_stick.x&&l.dpad.y>l.left_stick.y);
        dm_tc_point a=dm_tc_face_position(l,0),b=dm_tc_face_position(l,1),x=dm_tc_face_position(l,2),y=dm_tc_face_position(l,3);
        CHECK(a.x>l.face.x&&b.y>l.face.y&&x.y<l.face.y&&y.x<l.face.x);
        CHECK(near(a.x-y.x,100*l.unit)&&near(b.y-x.y,100*l.unit));
        CHECK(dm_tc_shoulder(l,0,1).y<dm_tc_shoulder(l,0,0).y);
        CHECK(dm_tc_click(l,0).x>dm_tc_shoulder(l,0,1).x);
    }
    // Both local Duo display profiles, rotated forms and narrow resize samples.
    const double sizes[][2]={{678,466},{951,669},{466,678},{669,951},{320,700}};
    for (unsigned n=0;n<sizeof sizes/sizeof *sizes;n++) {
        dm_tc_layout l=dm_tc_make_layout(sizes[n][0],sizes[n][1],(dm_tc_insets){0,20,21,20});
        CHECK(isfinite(l.unit)&&l.unit>0);
        CHECK(l.gear.x>=l.safe.x&&l.gear.x+44<=l.safe.x+l.safe.width);
        CHECK(l.dpad.x+DM_TC_DPAD_R*l.unit<l.face.x-(DM_TC_FACE_GAP+DM_TC_FACE_R)*l.unit);
        CHECK(l.left_stick.x-DM_TC_STICK_R*l.unit>=0);
        CHECK(l.face.x+(DM_TC_FACE_GAP+DM_TC_FACE_R)*l.unit<=sizes[n][0]);
        // The moped pedal and jump button stay inside the diamond's footprint, apart.
        dm_tc_rect pedal=dm_tc_pedal(l); dm_tc_point jump=dm_tc_jump(l); double reach=(DM_TC_FACE_GAP+DM_TC_FACE_R)*l.unit;
        CHECK(pedal.x+pedal.width<=l.face.x+reach+.000001&&pedal.y>=l.face.y-reach-.000001&&pedal.y+pedal.height<=l.face.y+reach+.000001);
        CHECK(jump.x-DM_TC_JUMP_R*l.unit>=l.face.x-reach-.000001&&jump.y+DM_TC_JUMP_R*l.unit<=l.face.y+reach+.000001);
        CHECK(jump.x+DM_TC_JUMP_R*l.unit<pedal.x);
    }
    CHECK(dm_tc_make_layout(NAN,440,(dm_tc_insets){0}).unit==0);
    // Swipe-to-look on cameras that read only a stick: each swipe's distance is a
    // turn budget the right stick drains, so short, fast and released swipes still
    // turn by their whole length.
    {
        const uint64_t frame=UINT64_C(16666667);
        const double slack=DM_TC_LOOK_DONE+DM_TC_LOOK_FLOOR*DM_TC_LOOK_SPEED*frame/1e9;   // the last frame keeps the floor
        dm_tc_look l={0}; double x,y,turned=0,first=0; uint64_t t=UINT64_C(1000000000); int frames=0;
        dm_tc_look_begin(&l,(dm_tc_point){500,200},t);
        dm_tc_look_step(&l,t,&x,&y); CHECK(x==0&&y==0&&dm_tc_look_active(&l));   // touching, nothing swiped yet
        for (int i=1;i<=4;i++) dm_tc_look_move(&l,(dm_tc_point){500+i*50.0,200},1);   // 200 pt right in one fast flick
        dm_tc_look_release(&l);
        while (dm_tc_look_active(&l)&&frames<600) {
            t+=frame; dm_tc_look_step(&l,t,&x,&y);
            if (!frames) first=x;
            CHECK(x>=0&&x<=1&&fabs(y)<.000001&&(x==0||x>=DM_TC_LOOK_FLOOR-.000001));
            turned+=x*DM_TC_LOOK_SPEED*frame/1e9; ++frames;
        }
        CHECK(near(first,1)&&fabs(turned-200)<slack&&frames<60);   // full tilt, then the swipe's distance
        dm_tc_look_step(&l,t+frame,&x,&y); CHECK(x==0&&y==0&&!dm_tc_look_active(&l));
        // A tiny flick still clears the dead zone and finishes its distance.
        dm_tc_look_begin(&l,(dm_tc_point){500,200},t); dm_tc_look_move(&l,(dm_tc_point){510,200},1); dm_tc_look_release(&l);
        turned=0; frames=0;
        while (dm_tc_look_active(&l)&&frames<600) { t+=frame; dm_tc_look_step(&l,t,&x,&y); CHECK(x==0||x>=DM_TC_LOOK_FLOOR-.000001); turned+=x*DM_TC_LOOK_SPEED*frame/1e9; ++frames; }
        CHECK(fabs(turned-10)<slack&&frames>=1);
        // Up is positive; scale is the layout unit; a new swipe adds to the rest.
        dm_tc_look_begin(&l,(dm_tc_point){500,300},t); dm_tc_look_move(&l,(dm_tc_point){500,200},2);
        CHECK(near(l.dy,-50)); t+=frame; dm_tc_look_step(&l,t,&x,&y); CHECK(fabs(x)<.000001&&y>0);
        double rest=l.dy; dm_tc_look_release(&l);
        dm_tc_look_begin(&l,(dm_tc_point){500,300},t); dm_tc_look_move(&l,(dm_tc_point){500,280},2); CHECK(near(l.dy,rest-10));
        // A stalled frame turns at most one capped step; cancel drops everything.
        double before=hypot(l.dx,l.dy); dm_tc_look_step(&l,t+UINT64_C(1000000000),&x,&y);
        CHECK(before-hypot(l.dx,l.dy)<=DM_TC_LOOK_SPEED*DM_TC_LOOK_MAX_STEP+.000001);
        dm_tc_look_cancel(&l); dm_tc_look_step(&l,t+UINT64_C(2000000000),&x,&y); CHECK(x==0&&y==0&&!dm_tc_look_active(&l));
        dm_tc_look_begin(&l,(dm_tc_point){NAN,0},t); CHECK(!dm_tc_look_active(&l));
        dm_tc_look_begin(&l,(dm_tc_point){0,0},t); dm_tc_look_move(&l,(dm_tc_point){INFINITY,0},1); dm_tc_look_move(&l,(dm_tc_point){10,0},0);
        CHECK(l.dx==0&&l.dy==0);
        // Direct turns: a sample's distance (unit already divided by the sensitivity)
        // goes to the camera and leaves the stick budget alone; nothing is taken
        // from a released, non-finite or unscaled sample.
        double dx,dy;
        dm_tc_look_cancel(&l); dm_tc_look_begin(&l,(dm_tc_point){500,300},t);
        CHECK(dm_tc_look_take(&l,(dm_tc_point){530,290},2/dm_tc_look_sensitivity(2),&dx,&dy)&&near(dx,30)&&near(dy,-10));
        CHECK(l.dx==0&&l.dy==0&&l.last.x==530);
        CHECK(near(dx*DM_TC_LOOK_DEGREES,9));   // 30 points at sensitivity 2 and unit 2: nine degrees
        CHECK(!dm_tc_look_take(&l,(dm_tc_point){NAN,0},1,&dx,&dy)&&dx==0&&dy==0);
        CHECK(!dm_tc_look_take(&l,(dm_tc_point){540,290},0,&dx,&dy)&&l.last.x==530);
        dm_tc_look_release(&l); CHECK(!dm_tc_look_take(&l,(dm_tc_point){600,290},1,&dx,&dy)&&dx==0);
        CHECK(dm_tc_look_sensitivity(1)==1&&dm_tc_look_sensitivity(0)==1&&dm_tc_look_sensitivity(NAN)==1);
        CHECK(dm_tc_look_sensitivity(10)==DM_TC_LOOK_SENS_MAX&&dm_tc_look_sensitivity(.01)==DM_TC_LOOK_SENS_MIN);
    }
    dm_tc_layout phone=dm_tc_make_layout(956,440,(dm_tc_insets){0,62,21,62});
    CHECK(near(phone.left_stick.x,31+81*phone.unit));
    CHECK(near(phone.left_stick.y,419-82*phone.unit));
    CHECK(dm_tc_contains(phone.gear,(dm_tc_point){phone.gear.x,phone.gear.y}));
    CHECK(!dm_tc_contains(phone.gear,(dm_tc_point){phone.gear.x+44,phone.gear.y}));
    dm_tc_gear gear={0}; const uint64_t t=1000000000;
    uint64_t token=dm_tc_gear_begin(&gear,(dm_tc_point){5,5},t,0);
    CHECK(!dm_tc_gear_ready(&gear,token,t+DM_TC_GEAR_HOLD_NS-1,0));
    CHECK(dm_tc_gear_ready(&gear,token,t+DM_TC_GEAR_HOLD_NS,0));
    CHECK(!dm_tc_gear_ready(&gear,token,t+DM_TC_GEAR_HOLD_NS+1,0)); // one opening
    dm_tc_gear_cancel(&gear);
    CHECK(!dm_tc_gear_ready(&gear,token,t+1000000000,0));
    token=dm_tc_gear_begin(&gear,(dm_tc_point){0,0},t,0);
    dm_tc_gear_move(&gear,(dm_tc_point){12,0});
    CHECK(dm_tc_gear_ready(&gear,token,t+DM_TC_GEAR_HOLD_NS,0));
    token=dm_tc_gear_begin(&gear,(dm_tc_point){0,0},t,0);
    dm_tc_gear_move(&gear,(dm_tc_point){12.01,0});
    dm_tc_gear_move(&gear,(dm_tc_point){0,0}); // returning cannot rearm a swipe
    CHECK(!dm_tc_gear_ready(&gear,token,t+DM_TC_GEAR_HOLD_NS,0));
    token=dm_tc_gear_begin(&gear,(dm_tc_point){0,0},t,1);
    CHECK(!dm_tc_gear_ready(&gear,token,t+DM_TC_GEAR_HOLD_NS,0));
    token=dm_tc_gear_begin(&gear,(dm_tc_point){0,0},t,0);
    dm_tc_gear_block(&gear);
    CHECK(!dm_tc_gear_ready(&gear,token,t+DM_TC_GEAR_HOLD_NS,0));
    token=dm_tc_gear_begin(&gear,(dm_tc_point){0,0},t,0);
    CHECK(!dm_tc_gear_ready(&gear,token,t+DM_TC_GEAR_HOLD_NS,1));
    CHECK(!dm_tc_gear_ready(&gear,token,t+DM_TC_GEAR_HOLD_NS+1,0));
    token=dm_tc_gear_begin(&gear,(dm_tc_point){0,0},t,0);
    dm_tc_gear_cancel(&gear); // quick tap, touch cancel, background, rotation
    CHECK(!dm_tc_gear_ready(&gear,token,t+DM_TC_GEAR_HOLD_NS,0));
    uint64_t newer=dm_tc_gear_begin(&gear,(dm_tc_point){1,1},t+1,0);
    CHECK(!dm_tc_gear_ready(&gear,token,t+1000000000,0));
    CHECK(dm_tc_gear_ready(&gear,newer,t+1000000000,0));
    token=dm_tc_gear_begin(&gear,(dm_tc_point){NAN,0},t,0);
    CHECK(!dm_tc_gear_ready(&gear,token,t+DM_TC_GEAR_HOLD_NS,0));
    // Current v5 activation is tap/release; legacy hold helpers above remain
    // covered for historical receivers but are not called by current adapters.
    dm_tc_rect hit={0,0,44,44};
    token=dm_tc_gear_begin(&gear,(dm_tc_point){22,22},t,0);
    CHECK(dm_tc_gear_tap(&gear,token,(dm_tc_point){22,22},hit));
    CHECK(!dm_tc_gear_tap(&gear,token,(dm_tc_point){22,22},hit));
    token=dm_tc_gear_begin(&gear,(dm_tc_point){22,22},t,0);
    dm_tc_gear_move(&gear,(dm_tc_point){34.01,22});dm_tc_gear_move(&gear,(dm_tc_point){22,22});
    CHECK(!dm_tc_gear_tap(&gear,token,(dm_tc_point){22,22},hit));
    token=dm_tc_gear_begin(&gear,(dm_tc_point){43,22},t,0);
    CHECK(!dm_tc_gear_tap(&gear,token,(dm_tc_point){45,22},hit));
    token=dm_tc_gear_begin(&gear,(dm_tc_point){22,22},t,0);dm_tc_gear_block(&gear);
    CHECK(!dm_tc_gear_tap(&gear,token,(dm_tc_point){22,22},hit));
    token=dm_tc_gear_begin(&gear,(dm_tc_point){22,22},t,0);dm_tc_gear_cancel(&gear);
    CHECK(!dm_tc_gear_tap(&gear,token,(dm_tc_point){22,22},hit));
    token=dm_tc_gear_begin(&gear,(dm_tc_point){22,22},t,0);
    newer=dm_tc_gear_begin(&gear,(dm_tc_point){22,22},t+1,0);
    CHECK(!dm_tc_gear_tap(&gear,token,(dm_tc_point){22,22},hit));
    CHECK(dm_tc_gear_tap(&gear,newer,(dm_tc_point){34,22},hit));
    printf("PASS: %d canonical touch/gear checks\n",checks); return 0;
}
