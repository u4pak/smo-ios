// Four native switches and the swipe look slider behind the deliberate gear;
// advanced values stay stored.
#import "DmSettings.h"
#import "DmMotion.h"
#import "DmDeviceHaptics.h"
#include "game_profile.h"
#include "dm_touch_controls.h"

static NSString *const framegenKey=@"DmFrameGeneration";
static BOOL explicitFramegenEnvironment;
void dm_settings_apply_boot_preferences(void) {
    explicitFramegenEnvironment=getenv("DM_FRAMEGEN")!=NULL;
    NSNumber *saved=[NSUserDefaults.standardUserDefaults objectForKey:framegenKey];
    if (saved&&!explicitFramegenEnvironment) setenv("DM_FRAMEGEN",saved.boolValue?"1":"0",0);
}
@interface DmSettingsVC : UITableViewController <UIAdaptivePresentationControllerDelegate>
@end
@implementation DmSettingsVC
- (void)viewDidLoad {
    [super viewDidLoad]; self.title=@"Settings";
    self.tableView.rowHeight=76;
    self.navigationItem.rightBarButtonItem=[[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemDone target:self action:@selector(close)];
}
- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section {
    (void)tableView; (void)section; return 5;
}
- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath {
    (void)tableView;
    static NSString *const titles[]={@"Gyro",@"Device haptics",@"Also with controller",@"Touch controls with controller",@"Swipe look sensitivity"};
    NSInteger row=indexPath.row;
    BOOL supported=dm_device_haptics_settings_supported();
    BOOL haptics=dm_device_haptics_settings_enabled();
    UITableViewCell *cell=[[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle reuseIdentifier:nil];
    cell.textLabel.text=titles[row]; cell.selectionStyle=UITableViewCellSelectionStyleNone;
    cell.textLabel.adjustsFontForContentSizeCategory=YES;
    cell.detailTextLabel.adjustsFontForContentSizeCategory=YES;
    cell.detailTextLabel.numberOfLines=0;
    if (row==4) {
        double sensitivity=dm_gamepad_look_sensitivity();
        UISlider *slider=[[UISlider alloc] initWithFrame:CGRectMake(0,0,150,34)];
        slider.minimumValue=DM_TC_LOOK_SENS_MIN; slider.maximumValue=DM_TC_LOOK_SENS_MAX; slider.value=sensitivity;
        slider.accessibilityLabel=titles[row];
        [slider addTarget:self action:@selector(slid:) forControlEvents:UIControlEventValueChanged];
        [self describe:cell slider:slider sensitivity:sensitivity];
        cell.accessoryView=slider; return cell;
    }
    if (row==1&&!supported) cell.detailTextLabel.text=@"Unavailable on this device";
    else if (row==2) cell.detailTextLabel.text=supported?@"Device vibration while a controller is connected":@"Requires device haptics";
    else if (row==3) cell.detailTextLabel.text=dm_gamepad_controller_connected()?@"Keep on-screen controls while this controller is connected":@"Applies when a controller is connected";
    UISwitch *toggle=[UISwitch new]; toggle.tag=row;
    toggle.enabled=row==0||row==3||(supported&&(row==1||haptics));
    toggle.on=row==0?dm_motion_settings_enabled():row==3?dm_gamepad_touch_with_controller():
        supported&&(row==1?haptics:dm_device_haptics_settings_with_controller());
    toggle.accessibilityLabel=titles[row]; toggle.accessibilityHint=cell.detailTextLabel.text;
    [toggle addTarget:self action:@selector(changed:) forControlEvents:UIControlEventValueChanged];
    cell.accessoryView=toggle; return cell;
}
- (void)changed:(UISwitch *)toggle {
    if (toggle.tag==0) dm_motion_settings_set_enabled(toggle.on);
    else if (toggle.tag==1) dm_device_haptics_settings_set_enabled(toggle.on);
    else if (toggle.tag==2) dm_device_haptics_settings_set_with_controller(toggle.on);
    else dm_gamepad_set_touch_with_controller(toggle.on);
    [self.tableView reloadData];
}
- (void)describe:(UITableViewCell *)cell slider:(UISlider *)slider sensitivity:(double)sensitivity {
    cell.detailTextLabel.text=[NSString stringWithFormat:@"%.2f\u00d7 \u00b7 how far a swipe turns the camera",sensitivity];
    slider.accessibilityValue=[NSString stringWithFormat:@"%.2f times",sensitivity];
}
- (void)slid:(UISlider *)slider {
    double sensitivity=round(slider.value*20)/20;   // steps of 0.05; no reload while dragging
    dm_gamepad_set_look_sensitivity(sensitivity);
    UITableViewCell *cell=[self.tableView cellForRowAtIndexPath:[NSIndexPath indexPathForRow:4 inSection:0]];
    if (cell) [self describe:cell slider:slider sensitivity:dm_gamepad_look_sensitivity()];
}
- (void)close { [self dismissViewControllerAnimated:YES completion:^{ dm_gamepad_settings_visible(NO); }]; }
- (void)presentationControllerDidDismiss:(UIPresentationController *)controller {
    (void)controller;dm_gamepad_settings_visible(NO);
}
@end
void dm_settings_present(UIViewController *presenter) {
    if (!presenter||!presenter.view.window||presenter.presentedViewController) return;
    DmSettingsVC *settings=[[DmSettingsVC alloc] initWithStyle:UITableViewStyleInsetGrouped];
    UINavigationController *navigation=[[UINavigationController alloc] initWithRootViewController:settings];
    navigation.modalPresentationStyle=UIModalPresentationPageSheet;
    navigation.preferredContentSize=CGSizeMake(420,542);
    navigation.sheetPresentationController.detents=@[UISheetPresentationControllerDetent.mediumDetent,UISheetPresentationControllerDetent.largeDetent];
    navigation.sheetPresentationController.prefersGrabberVisible=YES;
    navigation.presentationController.delegate=settings;
    dm_gamepad_settings_visible(YES);
    [presenter presentViewController:navigation animated:YES completion:nil];
}
