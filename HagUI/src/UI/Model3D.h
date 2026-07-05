#pragma once

namespace hag::ui {

// The Model3D widget's C++ backing (Route A): owns an img:// virtual-image texture that the SWF samples
// via loadMovie("img://hagCharModel"). Stage 1 fills it with a static test pattern to prove the binding;
// Stage 2 renders the engine's menu-3D scene into it. See docs/Model3D-RE.md.
class Model3D {
public:
    static const char* kImageName;   // "hagCharModel"

    // Create + register the img:// texture once (idempotent). Safe to call every menu tick; the D3D
    // resource creation runs once. Returns true when the image is registered and ready to show.
    static bool EnsureTexture();
    static bool Ready();
};

}  // namespace hag::ui
