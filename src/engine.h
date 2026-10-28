#pragma once
// engine.h — device/swapchain/DGSLEffect/PrimitiveBatch/SpriteBatch/keyboard/audio (MINE, final).
// RAII contract: COM members (ComPtr), unique_ptr components; fonts via runtime in-memory ctor from
// generated header static arrays (px-accurate); textures loaded once (map, stable ids).
// DirectXTK components acquired lazily on first use (GetSpriteBatch/GetKeyboard).
#include "core.h"
#include <string>
#include <map>
#include <unordered_map>
#include <d3d11.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <SimpleMath.h>
#include "SpriteBatch.h"
#include "CommonStates.h"
#include "GraphicsMemory.h"
#include "Keyboard.h"
#include "DDSTextureLoader.h"
#include "WICTextureLoader.h"
#include "Effects.h"
#include "PrimitiveBatch.h"
#include "VertexTypes.h"
#include "SpriteFont.h"
#include "Audio.h"
#include "PostProcess.h"
#include "assets.h" // generated header (single source of truth: font+atlas+mesh+sfx+cso data)

namespace tetris
{

using Microsoft::WRL::ComPtr;

class Engine
{
public:
    Engine();
    ~Engine();
    void CreateDevice(bool useWarp); // device + swapchain + WARP fallback
    HWND CreateWindowHandle(HINSTANCE hInst, int w, int h,
                            WNDPROC wndProc); // engine class registration; main owns WNDPROC via callback
    void Resize(int w, int h);                // resize swapchain
    void Present(bool showCursor);
    // DirectXTK components (RAII members, acquired lazily on first use):
    DirectX::SpriteBatch* GetSpriteBatch();
    DirectX::CommonStates* GetCommonStates();
    DirectX::GraphicsMemory* GetGraphicsMemory();
    DirectX::Keyboard* GetKeyboard();
    // my pipeline + DGSL batch:
    DirectX::DGSLEffect* GetEffect() { return m_effect.get(); }
    DirectX::PrimitiveBatch<DirectX::VertexPositionNormalColorTexture>* GetPrimBatch(); // lazy begin
    // fonts/textures (generated data):
    ID3D11ShaderResourceView* GetWhiteTexture();  // 1x1 opaque white (filled rects via SpriteBatch)
    ID3D11ShaderResourceView* GetTexture(int id); // stable hash id keys: font sheet + atlas (loaded once)
    const DirectX::SpriteFont*
    GetFont(int fontId); // runtime in-memory ctor from generated arrays (fontId = stable hash id contract)
    // snapshot/dump mode (--snapshot): first present writes PNG via WIC then exits.
    void EnableSnapshotMode() { m_snapshot = true; }
    bool SnapshotRequested() const { return m_snapshot; }
    void SaveSnapshotPng(const char* path);          // WIC encoder (RGBA32 copy of backbuffer)
    void Clear(const DirectX::SimpleMath::Color& c); // clear RT+DSV
    ID3D11Device* Device() { return m_dev.Get(); }
    ID3D11DeviceContext* Ctx() { return m_ctx.Get(); }
    // audio (no-op when the machine has no playback device)
    void PlaySfx(const wchar_t* fileName); // relative path, loaded once then cached
    bool AudioReady() const { return m_audio != nullptr; }

private:
    void BindBackBuffer(); // flip-model: GetBuffer(0) is only valid until Present, so rebind each frame
    ComPtr<ID3D11Device> m_dev;
    ComPtr<ID3D11DeviceContext> m_ctx;
    ComPtr<ID3D11InputLayout> m_layout; // pos/normal/texcoord/color layout (stride 44, fxc-verified)
    ComPtr<ID3D11VertexShader> m_vs;    // shaders/game.cso (fxc build-time)
    ComPtr<ID3D11PixelShader> m_ps;
    ComPtr<ID3D11Buffer> m_cbPerMat; // per-material matrices (DGSL b0/b1 chain)
    ComPtr<ID3D11RenderTargetView> m_rtv;
    ComPtr<ID3D11DepthStencilView> m_dsv;
    ComPtr<ID3D11Texture2D> m_backbuffer; // (for snapshot copy)

    std::unique_ptr<DirectX::DGSLEffect>
        m_effect; // with optional ToneMapPostProcess chain (ToneMapPostProcess::Create)
    std::unique_ptr<DirectX::PrimitiveBatch<DirectX::VertexPositionNormalColorTexture>> m_prim;
    std::unique_ptr<DirectX::ToneMapPostProcess> m_postProc; // optional glow chain (Bloom/ColorAdjust/Blur/ToneMap)
    std::unique_ptr<DirectX::SpriteBatch> m_batch;
    std::unique_ptr<DirectX::CommonStates> m_states;
    std::unique_ptr<DirectX::GraphicsMemory> m_gfxMem;
    std::unique_ptr<DirectX::Keyboard> m_keyboard;

    // fonts/textures (RAII map; loaded once; DirectXTK loaders)
    std::map<int, std::unique_ptr<DirectX::SpriteFont>> m_fonts;             // fontId = stable hash id contract
    std::map<int, Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> m_texes; // stable hash id keys

    // audio: RAII engine (unique_ptr) + SoundEffect loaded once + wave bank (from memory)
    std::unique_ptr<DirectX::AudioEngine> m_audio; // DirectXTK DirectX::AudioEngine ✓ (RAII)
    std::unordered_map<std::wstring, std::unique_ptr<DirectX::SoundEffect>> m_sfx; // lazy-loaded, keyed by file name
    std::unique_ptr<DirectX::WaveBank>
        m_waveBank; // (unused runtime; generated static wav pool in game.cpp — loaded direct in-game from header data)
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_fontSrv; // generated font sheet (loaded once)
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_white;   // 1x1 white (filled rects)
    Microsoft::WRL::ComPtr<IDXGISwapChain> m_swapchain;
    Microsoft::WRL::ComPtr<ID3D11Device> m_dev2; // (same device for audio engine)

    // window + class registration (main owns WNDPROC; engine registers class + main via callback)
    HWND m_hwnd = nullptr;
    WNDPROC m_mainWndProc = nullptr;
    HINSTANCE m_hInst = nullptr;
    const wchar_t* m_szClass = L"tetris_window_class";
    int m_width = 0, m_height = 0;
    bool m_snapshot = false;
};

} // namespace tetris
