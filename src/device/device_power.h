#pragma once
#include <stdint.h>
namespace device {
// Called from setup/loop only. No display or Wi-Fi operations occur here.
void beginPower(bool usb);
void servicePower(bool usb);
// UI publishes preference only; the power owner applies it without reconnecting.
void requestBatterySaver(bool enabled);
bool batterySaverEnabled();
void noteUserActivity(); // Physical input only; never background network/display work.
bool noteWifiConnected(); // One Saver extension per physical activity window; never re-arms itself.
bool batterySaverIdle(); // Atomic preference/power/activity snapshot.
// Evaluated by the always-running hardware power poll, including during setup.
bool shouldShutdown(int batteryPercent, bool usbDefinitelyAbsent);
}
