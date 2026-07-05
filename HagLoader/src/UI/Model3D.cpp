#include "PCH.h"
#include "UI/Model3D.h"
#include "Log.h"
#include "Offsets.h"

#include <d3d11.h>
#include <vector>

// Route A, Stage 1: bind a native D3D texture as the img://hagCharModel virtual image so the SWF can
// show it. Everything here was hand-found in Ghidra + verified live (see docs/Model3D-RE.md,
// shared/GameOffsets.h game::render):
//   device   = *(0x3286A10)                      (ID3D11Device, verified: vtbl[5]=CreateTexture2D in d3d11.dll)
//   entry    = FUN_140d2f140(&BSFixedString)     (creates + registers a virtual-image entry in the global list)
//   entry+0x48 = our BSGraphics::Texture wrapper (the scaleform renderer samples wrapper+0x10 = the SRV)
// The BGSUserIcon path (FUN_140941970) is the working template we mirror.
namespace hag::ui {
namespace {

using namespace hag::offsets;

// BSGraphics::Texture wrapper (0x28) that the virtual-image entry points at (+0x48). Layout recovered
// from the texture-create FUN_140e48660: tex@0x00, SRV@0x10, height@0x18, width@0x1a, fmt@0x1d.
#pragma pack(push, 1)
struct BSTexture {
    ID3D11Texture2D*          tex;       // 0x00
    void*                     pad08;     // 0x08
    ID3D11ShaderResourceView* srv;       // 0x10
    std::uint16_t             height;    // 0x18
    std::uint16_t             width;     // 0x1a
    std::uint8_t              one;       // 0x1c  (=1)
    std::uint8_t              fmt;       // 0x1d  (=0x1c, DXGI_FORMAT_R8G8B8A8_UNORM)
    std::uint16_t             zero;      // 0x1e
    std::uint32_t             refCount;  // 0x20
    std::uint32_t             pad24;     // 0x24
};
#pragma pack(pop)
static_assert(sizeof(BSTexture) == 0x28, "BSTexture must be 0x28");

constexpr int kW = 512, kH = 512;

BSTexture g_wrapper{};
void*     g_entry = nullptr;
bool      g_ready = false;

ID3D11Device* Device() { return *reinterpret_cast<ID3D11Device**>(FromRVA(game::render::D3D11DevicePtr)); }

using MakeStrFn    = void  (*)(void* bsFixedStrOut, const char* s);
using DtorStrFn    = void  (*)(void* bsFixedStr);
using VImgCreateFn = void* (*)(void* nameBSFixedStr);
using VRegFn       = void  (*)(void* slot, void* entry);   // FUN_140faf5e0(slot, entry)

// Persistent registration slot { VirtualImage* @0; ...; BSFixedString @0x10 }. FUN_140faf5e0 stores our
// entry here (refcounted) + inserts name->image into the scaleform image-loader map. Must outlive the
// image, so it's static and zero-initialised.
std::uint64_t g_slot[3] = {0, 0, 0};

// Real work (C++ objects live here, NOT in the __try guard). Returns true once the img is registered.
bool DoEnsure() {
    ID3D11Device* dev = Device();
    if (!dev) { HAG_WARN("Model3D: D3D device null"); return false; }

    // 1) a clean flat near-black backdrop (#0A0A0C). This is just the initial contents; Stage 2 clears
    //    + renders the model into this same texture each frame. R8G8B8A8_UNORM => u32 = 0xAABBGGRR.
    const std::uint32_t kBackdrop = 0xFF0C0A0Au;   // opaque #0A0A0C
    std::vector<std::uint32_t> px(static_cast<std::size_t>(kW) * kH, kBackdrop);

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = kW; desc.Height = kH; desc.MipLevels = 1; desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;  // RT for Stage 2
    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem = px.data();
    init.SysMemPitch = kW * 4;

    ID3D11Texture2D* tex = nullptr;
    HRESULT hr = dev->CreateTexture2D(&desc, &init, &tex);
    if (FAILED(hr) || !tex) { HAG_ERR("Model3D: CreateTexture2D failed {:#x}", static_cast<unsigned>(hr)); return false; }
    ID3D11ShaderResourceView* srv = nullptr;
    hr = dev->CreateShaderResourceView(tex, nullptr, &srv);
    if (FAILED(hr) || !srv) { HAG_ERR("Model3D: CreateSRV failed {:#x}", static_cast<unsigned>(hr)); tex->Release(); return false; }

    g_wrapper = {};
    g_wrapper.tex = tex; g_wrapper.srv = srv;
    g_wrapper.height = kH; g_wrapper.width = kW;
    g_wrapper.one = 1; g_wrapper.fmt = static_cast<std::uint8_t>(game::render::kFmt_RGBA8);
    g_wrapper.refCount = 0x40000000;

    // 2) create + register the virtual-image entry named "hagCharModel", point it at our texture wrapper
    std::uint64_t name = 0;  // BSFixedString (8-byte interned handle)
    reinterpret_cast<MakeStrFn>(FromRVA(kBSFixedString_ctor))(&name, Model3D::kImageName);
    void* entry = reinterpret_cast<VImgCreateFn>(FromRVA(game::render::VImageCreate))(&name);
    reinterpret_cast<DtorStrFn>(FromRVA(kBSFixedString_dtor))(&name);
    if (!entry) { HAG_ERR("Model3D: VImageCreate returned null"); return false; }

    *reinterpret_cast<void**>(reinterpret_cast<char*>(entry) + game::render::VImage_Texture) = &g_wrapper;  // +0x48
    *reinterpret_cast<volatile long*>(reinterpret_cast<char*>(entry) + 8) = 0x40000000;  // pin the entry (never freed)
    g_entry = entry;

    // register the entry with the scaleform image loader so img://hagCharModel resolves (mirrors the
    // BGSUserIcon path FUN_140941970 -> FUN_140faf5e0 -> FUN_140fb3320: builds a GTexture from
    // entry+0x48 and inserts name->image into the loader's map at scaleformMgr->imageLoader+0x20).
    reinterpret_cast<VRegFn>(FromRVA(game::render::VImageRegister))(g_slot, entry);

    g_ready = true;
    HAG_INFO("Model3D: registered img://{} {}x{} (entry={} tex={} srv={})",
             Model3D::kImageName, kW, kH, entry, static_cast<void*>(tex), static_cast<void*>(srv));
    return true;
}

}  // namespace

const char* Model3D::kImageName = "hagCharModel";
bool Model3D::Ready() { return g_ready; }

bool Model3D::EnsureTexture() {
    if (g_ready) return true;
    __try { return DoEnsure(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { HAG_ERR("Model3D: EnsureTexture faulted"); return false; }
}

}  // namespace hag::ui
