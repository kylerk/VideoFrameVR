#pragma once

// The Frame's refresh rate for this app: SteamVR's per-app setting
// (steam.app.<SteamAppId>.preferredRefreshRate, as its per-app page shows
// it), changed through SteamVR's vrcmd. SteamVR applies a change while the
// app runs, within about 2 s; it ignores OpenXR's rate requests. Without a
// value it runs an app at 72 Hz.

// The menu's choices, in Hz.
constexpr int kRefreshRates[] = {72, 90, 120};

// Whether there is one to change: on the Frame, started from Steam (which
// passes the app id) and with SteamVR's vrcmd.
bool appRefreshRateAvailable();
// SteamVR's value, rounded to Hz; 0 if it has none or can't be read. Waits
// for vrcmd (about 30 ms).
int readAppRefreshRate();
// Sets it; waits for vrcmd.
void writeAppRefreshRate(int hz);
