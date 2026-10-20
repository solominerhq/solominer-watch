#pragma once
namespace ui {
struct PageRect {
    int x, y, width, height;
    bool contains(int px, int py) const {
        return px >= x && px < x + width && py >= y && py < y + height;
    }
};
// Three large Settings rows, with 16 px gaps and room for the page dots below.
constexpr PageRect brightness{24, 104, 362, 100};
constexpr PageRect batterySaver{24, 220, 362, 100};
constexpr PageRect setup{24, 336, 362, 100};
constexpr PageRect detailBack{20, 21, 62, 64};
constexpr PageRect brightnessLow{32, 117, 346, 88};
constexpr PageRect brightnessMedium{32, 221, 346, 88};
constexpr PageRect brightnessHigh{32, 325, 346, 88};
constexpr PageRect saverToggle{32, 117, 346, 88};
// Persistent footer controls sit above the centered celebration page dots.
constexpr PageRect demoMute{40, 398, 72, 72};
constexpr PageRect demoExit{298, 398, 72, 72};
constexpr PageRect miningBlocks{276, 365, 120, 88};
constexpr PageRect blockNotice{80, 276, 250, 52};
constexpr PageRect openSetup{32, 282, 346, 88};
constexpr PageRect cancelSetup{32, 386, 346, 88};
constexpr PageRect setupAlternate{24, 413, 173, 60};
constexpr PageRect setupExit{213, 413, 173, 60};
constexpr PageRect setupExitWide{32, 413, 346, 60};
constexpr PageRect miningAction{32, 264, 346, 60};
constexpr PageRect bottomAction{32, 386, 346, 88};
// Fixed primary row with room for optional guidance below.
constexpr PageRect clockStatus{20, 349, 370, 104};
enum class SetupView { Qr, Manual, Failed, Connecting, Leaving };
enum class SettingsView { List, BatterySaver, SetupConfirmation, Brightness };
enum class PageAction { None = -1, Brightness, BatterySaver, ShowSetup, OpenSetup, Cancel,
                        ToggleSaver, Back, BrightnessLow, BrightnessMedium, BrightnessHigh };
inline PageAction settingsAction(int x, int y, SettingsView view) {
    if (view != SettingsView::List && detailBack.contains(x,y)) return PageAction::Back;
    if (view == SettingsView::SetupConfirmation) {
        if (openSetup.contains(x,y)) return PageAction::OpenSetup;
        if (cancelSetup.contains(x,y)) return PageAction::Back;
    } else if (view == SettingsView::BatterySaver) {
        if (saverToggle.contains(x,y)) return PageAction::ToggleSaver;
    } else if (view == SettingsView::Brightness) {
        if (brightnessLow.contains(x,y)) return PageAction::BrightnessLow;
        if (brightnessMedium.contains(x,y)) return PageAction::BrightnessMedium;
        if (brightnessHigh.contains(x,y)) return PageAction::BrightnessHigh;
    } else {
        if (brightness.contains(x,y)) return PageAction::Brightness;
        if (batterySaver.contains(x,y)) return PageAction::BatterySaver;
        if (setup.contains(x,y)) return PageAction::ShowSetup;
    }
    return PageAction::None;
}
inline int selectedBrightness(PageAction action) {
    return action == PageAction::BrightnessLow ? 60 : action == PageAction::BrightnessMedium ? 150 :
           action == PageAction::BrightnessHigh ? 230 : -1;
}
enum class SetupAction { None, Manual, Qr, Retry, Exit };
inline SetupAction setupAction(int x, int y, SetupView view) {
    if (view == SetupView::Manual) {
        if (detailBack.contains(x,y)) return SetupAction::Qr;
        if (setupExitWide.contains(x,y)) return SetupAction::Exit;
    } else if (view == SetupView::Qr || view == SetupView::Failed) {
        if (setupAlternate.contains(x,y)) return view == SetupView::Qr ? SetupAction::Manual : SetupAction::Retry;
        if (setupExit.contains(x,y)) return SetupAction::Exit;
    }
    return SetupAction::None;
}
enum class DemoAction { None, ToggleMute, Exit };
inline DemoAction demoAction(int x, int y) {
    if (demoMute.contains(x,y)) return DemoAction::ToggleMute;
    if (demoExit.contains(x,y)) return DemoAction::Exit;
    return DemoAction::None;
}
}
