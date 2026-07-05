#pragma once

// Display control for HagGeneral. The INITIAL mode (fullscreen/borderless) is imposed cleanly at
// startup by the SettingsHook read-redirect (the game builds its own window/swapchain from our flags),
// so there is NO D3D hook and NO live SetFullscreenState — that avoids the display-mode blackscreen.
//
// Fullscreen changes therefore take effect on the next launch. Borderless<->bordered is a pure Win32
// window-style change, so it CAN apply live — but only when this session actually booted windowed
// (in an exclusive-fullscreen session there is no windowed frame to restyle, so it's next-launch too).
namespace hag {

class Display {
public:
    static void CaptureSessionMode();   // record the mode the game booted in this session (call at load)
    static bool SessionFullscreen();    // true if this session is running exclusive fullscreen
    static void ApplyBorderlessLive();  // live borderless<->bordered window-style (windowed sessions only)
};

}  // namespace hag
