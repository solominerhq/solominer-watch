#pragma once
#include <stdint.h>
namespace diagnostics {
struct WatchState { int screen=-1,page=-1,brightness=-1,demo=0,usb=-1; };
enum class Event : uint8_t { DemoStart, DemoCancel, DemoEnd, AudioReady, AudioStart, AudioStop, WifiRetry, Accepted, Rejected, RealBlock, Shutdown, HashError, SubmitFailed, PoolParseError, SettingsSaveFailed, SaverPause, SaverResume, SaverCheck, SaverCheckOk, SaverCheckFailed, SaverRadioError, WifiDisconnected, AudioInitFailed, AudioDecodeFailed, AudioOutputFailed, AudioTaskFailed };
void begin();
void event(Event kind);
void batteryRaw(int mv,int raw,int source,int charge);
void batteryDisplayed(int percent);
void usbState(bool usb);
void miningRate(uint32_t kh,uint32_t elapsedMs);
void checkpoint();
void prepareForLightSleep(); // Wake worker so it can park between file transactions.
}
// Read only; implementation holds the existing LVGL lock, never draws.
bool watchDiagnosticState(diagnostics::WatchState& state);
