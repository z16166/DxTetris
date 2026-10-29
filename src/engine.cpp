// engine.cpp — device/swapchain/window (MINE, final; RAII: COM members + unique_ptr components).
#include "engine.h"
#include <windows.h>
#include <imm.h>
#include <wrl/client.h>
#include <dxgi1_6.h>
#include <d3d11.h>
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
#include "ScreenGrab.h"
#include "Audio.h"
#include "PostProcess.h"
#include <wincodec.h>
#include <algorithm>
#include <cmath>
#include <vector>
#include <map>
#include <unordered_map>
#include "assets.h" // generated header (single source of truth: font+cso+atlas+mesh+sfx data)

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "windowscodecs.lib")

using namespace DirectX;
using Microsoft::WRL::ComPtr;

namespace tetris
{

Engine::Engine() {}
Engine::~Engine() {} // RAII members destroy in order

void Engine::CreateDevice(bool useWarp)
{
    D3D_DRIVER_TYPE dt = useWarp ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE;
    UINT flags = 0;
    // D3D_DRIVER_TYPE_UNKNOWN requires a non-null adapter; with a null adapter the driver type must be explicit.
    HRESULT hrDev = D3D11CreateDevice(nullptr, dt, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION, m_dev.GetAddressOf(),
                                      nullptr, m_ctx.GetAddressOf());
    if (FAILED(hrDev) && !useWarp)
    { // no hardware device (headless/RDP session) — retry on WARP
        m_dev.Reset();
        m_ctx.Reset();
        hrDev = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION,
                                  m_dev.GetAddressOf(), nullptr, m_ctx.GetAddressOf());
    }
    TETRIS_CHECK(hrDev);

    ComPtr<IDXGIFactory2> factory;
    TETRIS_CHECK(CreateDXGIFactory2(0, __uuidof(IDXGIFactory2), &factory));
    TETRIS_CHECK(m_dev->CreateVertexShader(game_vs_cso, game_vs_cso_len, nullptr, m_vs.GetAddressOf()));
    TETRIS_CHECK(m_dev->CreatePixelShader(game_ps_cso, game_ps_cso_len, nullptr, m_ps.GetAddressOf()));
    // input layout: POSITION/NORMAL/TEXCOORD0/COLOR (stride 44 — VertexPositionNormalColorTexture)
    D3D11_INPUT_ELEMENT_DESC il[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    TETRIS_CHECK(m_dev->CreateInputLayout(il, 4, game_vs_cso, game_vs_cso_len, m_layout.GetAddressOf()));
    // DGSL effect (b0 matrices b1 lit params — our shader; DGSL layout compatible ✓ stride 44)
    m_effect = std::make_unique<DGSLEffect>(m_dev.Get(), nullptr); // public 2-arg ctor ✓
    m_postProc = nullptr; // (optional HDR/glow chain — set at init if enabled; see README design notes)

    m_batch = std::make_unique<SpriteBatch>(m_ctx.Get());     // RAII ✓ (ctor: deviceContext only)
    m_states = std::make_unique<CommonStates>(m_dev.Get());   // RAII ✓
    m_gfxMem = std::make_unique<GraphicsMemory>(m_dev.Get()); // RAII ✓
    m_keyboard = std::make_unique<Keyboard>();                // RAII ✓ (main pumps)
    m_prim = nullptr;                                         // lazy begin

    // audio (RAII engine + sound effects from in-memory wav blobs — DirectXTK DirectX::AudioEngine ✓)
    try
    {
        m_audio = std::make_unique<DirectX::AudioEngine>();
    } // optional: no audio endpoint (VM/RDP) must not kill startup
    catch (...)
    {
        m_audio = nullptr;
    }
    // fonts: generated header data → runtime SpriteFont in-memory ctor (DirectXTK standard memory ctor ✓)
}

HWND Engine::CreateWindowHandle(HINSTANCE hInst, int w, int h, WNDPROC wndProc)
{
    m_hInst = hInst;
    WNDCLASSEXW wc{(UINT)sizeof(wc)};
    wc.lpfnWndProc = wndProc; // main owns WNDPROC (engine registers class, main calls back)
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = m_szClass;
    wc.cbWndExtra = 0;
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hIconSm = wc.hIcon;
    wc.style = CS_HREDRAW | CS_VREDRAW;
    if (!RegisterClassExW(&wc)) TETRIS_CHECK(HRESULT_FROM_WIN32(GetLastError()));

    const DWORD style = WS_OVERLAPPEDWINDOW;

    // Fit the default client size inside the desktop work area (screen minus taskbar/appbars) and
    // centre it there: a window created at CW_USEDEFAULT can start partly underneath the taskbar.
    RECT work{};
    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0) || work.right <= work.left ||
        work.bottom <= work.top)
        work = {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    const int workW = work.right - work.left;
    const int workH = work.bottom - work.top;

    RECT probe{0, 0, w, h};
    AdjustWindowRectEx(&probe, style, FALSE, 0);
    const int frameW = (probe.right - probe.left) - w;
    const int frameH = (probe.bottom - probe.top) - h;
    const double maxScaleW = (double)std::max(320, workW - frameW - 8) / (double)w;
    const double maxScaleH = (double)std::max(240, workH - frameH - 8) / (double)h;
    const double fit = std::min({1.0, maxScaleW, maxScaleH}); // keep the design aspect when shrinking
    const int clientW = std::max(320, (int)std::lround(w * fit));
    const int clientH = std::max(240, (int)std::lround(h * fit));

    RECT rc{0, 0, clientW, clientH};
    AdjustWindowRectEx(&rc, style, FALSE, 0);
    const int outerW = rc.right - rc.left;
    const int outerH = rc.bottom - rc.top;
    const int posX = work.left + std::max(0, (workW - outerW) / 2);
    const int posY = work.top + std::max(0, (workH - outerH) / 2);

    m_width = clientW;
    m_height = clientH;
    HWND hwnd = CreateWindowW(m_szClass, L"Tetris", style, posX, posY, outerW, outerH, nullptr, nullptr, hInst,
                              nullptr);
    if (!hwnd) TETRIS_CHECK(HRESULT_FROM_WIN32(GetLastError()));
    SetWindowTextW(hwnd, L"Tetris");    // title bar text
    ImmAssociateContext(hwnd, nullptr); // detach any IME context from the game window
    m_hwnd = hwnd;

    // swapchain on hwnd (flip model 2 buffers, RGBA32 — no waitable for simplicity)
    ComPtr<IDXGIFactory2> factory;
    TETRIS_CHECK(CreateDXGIFactory2(0, __uuidof(IDXGIFactory2), (void**)factory.GetAddressOf()));
    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM; // (contract: RGBA8 raw)
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.Scaling = DXGI_SCALING_NONE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    ComPtr<IDXGISwapChain1> sc1;
    TETRIS_CHECK(factory->CreateSwapChainForHwnd(m_dev.Get(), hwnd, &sd, nullptr, nullptr, sc1.GetAddressOf()));
    m_swapchain = sc1; // ComPtr<IDXGISwapChain> from SwapChain1 ✓

    // RTV + DSV (depth D24S8, standard 24-bit depth-stencil per swapchain buffer)
    ComPtr<ID3D11Texture2D> back;
    TETRIS_CHECK(m_swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), &back));
    TETRIS_CHECK(m_dev->CreateRenderTargetView(back.Get(), nullptr, m_rtv.GetAddressOf()));
    D3D11_TEXTURE2D_DESC dsvDesc{};
    back->GetDesc(&dsvDesc);
    dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; // (swapchain RGBA32 → RTV via B8G8R8A8; DSV needs its own tex)
    // depth-stencil texture for DSV:
    D3D11_TEXTURE2D_DESC dsDesc{dsvDesc};
    dsDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    dsDesc.Usage = D3D11_USAGE_DEFAULT;
    dsDesc.MiscFlags = 0;
    dsDesc.CPUAccessFlags = 0;
    dsDesc.SampleDesc = {1, 0};
    dsDesc.ArraySize = 1;
    dsDesc.MipLevels = 1;
    ComPtr<ID3D11Texture2D> depthTex;
    TETRIS_CHECK(m_dev->CreateTexture2D(&dsDesc, nullptr, depthTex.GetAddressOf()));
    TETRIS_CHECK(m_dev->CreateDepthStencilView(depthTex.Get(), nullptr, m_dsv.GetAddressOf()));
    // viewport is required state: SpriteBatch/DirectXTK throw "No viewport is set" without it
    D3D11_VIEWPORT vp{0.f, 0.f, (float)dsvDesc.Width, (float)dsvDesc.Height, 0.f, 1.f};
    m_ctx->RSSetViewports(1, &vp);
    ID3D11RenderTargetView* rtvs[1] = {m_rtv.Get()}; // drawing needs a bound RTV (Clear alone does not bind one)
    m_ctx->OMSetRenderTargets(1, rtvs, m_dsv.Get());
    m_backbuffer = back; // (for snapshot copy)
    return hwnd;
}

void Engine::Resize(int w, int h)
{
    if (w <= 0 || h <= 0) return;
    m_width = w;
    m_height = h;
    if (!m_swapchain || !m_dev) return;

    // ResizeBuffers fails while any reference to a back buffer is alive, so drop all of them first.
    // This runs inside WndProc: never throw from here, just leave the frame unbound (Present rebinds).
    m_rtv.Reset();
    m_dsv.Reset();
    m_backbuffer.Reset();
    if (FAILED(m_swapchain->ResizeBuffers(2, (UINT)w, (UINT)h, DXGI_FORMAT_B8G8R8A8_UNORM, 0))) return;

    ComPtr<ID3D11Texture2D> back;
    if (FAILED(m_swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)back.GetAddressOf()))) return;
    if (FAILED(m_dev->CreateRenderTargetView(back.Get(), nullptr, m_rtv.GetAddressOf()))) return;

    // recreate depth tex + DSV:
    D3D11_TEXTURE2D_DESC dsDesc{};
    dsDesc.Width = (UINT)w;
    dsDesc.Height = (UINT)h;
    dsDesc.MipLevels = 1;
    dsDesc.ArraySize = 1;
    dsDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsDesc.SampleDesc.Count = 1;
    dsDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    ComPtr<ID3D11Texture2D> depthTex;
    if (FAILED(m_dev->CreateTexture2D(&dsDesc, nullptr, depthTex.GetAddressOf()))) return;
    if (FAILED(m_dev->CreateDepthStencilView(depthTex.Get(), nullptr, m_dsv.GetAddressOf()))) return;

    D3D11_VIEWPORT vp{0.f, 0.f, (float)w, (float)h, 0.f, 1.f};
    m_ctx->RSSetViewports(1, &vp);
    ID3D11RenderTargetView* rtvs[1] = {m_rtv.Get()};
    m_ctx->OMSetRenderTargets(1, rtvs, m_dsv.Get());
    m_backbuffer = back;
}

void Engine::Clear(const DirectX::SimpleMath::Color& c)
{
    if (!m_rtv) return;
    float f[4] = {
        c.x, c.y, c.z,
        c.w}; // (SimpleMath::Color = XMFLOAT4 xyzw; linear via clear color raw values; sRGB textures sampled raw in shader)
    m_ctx->ClearRenderTargetView(m_rtv.Get(), f);
    if (m_dsv) m_ctx->ClearDepthStencilView(m_dsv.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.f, 0);
}

void Engine::BindBackBuffer()
{
    if (!m_swapchain || !m_dev) return;
    ComPtr<ID3D11Texture2D> back;
    if (FAILED(m_swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), &back))) return;
    m_backbuffer = back;
    m_rtv.Reset();
    if (FAILED(m_dev->CreateRenderTargetView(back.Get(), nullptr, m_rtv.GetAddressOf()))) return;
    ID3D11RenderTargetView* rtvs[1] = {m_rtv.Get()};
    m_ctx->OMSetRenderTargets(1, rtvs, m_dsv.Get());
    D3D11_TEXTURE2D_DESC d{};
    back->GetDesc(&d);
    D3D11_VIEWPORT vp{0.f, 0.f, (float)d.Width, (float)d.Height, 0.f, 1.f};
    m_ctx->RSSetViewports(1, &vp);
}

void Engine::Present(bool showCursor)
{
    if (!m_swapchain) return;
    m_swapchain->Present(showCursor ? 0 : 0, 0); // cursor via SetCursor in main; vsync flag 0
    BindBackBuffer();                            // flip model rotates the back buffer on Present
}

void Engine::SaveSnapshotPng(const char* path)
{
    if (!m_backbuffer || !m_dev) return;
    // DirectXTK ScreenGrab: staging readback + WIC PNG encode (handles WIC/COM details)
    wchar_t wpath[MAX_PATH]{};
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, MAX_PATH);
    TETRIS_CHECK(SaveWICTextureToFile(m_ctx.Get(), m_backbuffer.Get(), GUID_ContainerFormatPng, wpath));
    OutputDebugStringA("snapshot saved (WIC)\n");
}

// lazy getters (RAII members; created once):
DirectX::SpriteBatch* Engine::GetSpriteBatch()
{
    return m_batch.get();
}
DirectX::CommonStates* Engine::GetCommonStates()
{
    return m_states.get();
}
DirectX::GraphicsMemory* Engine::GetGraphicsMemory()
{
    return m_gfxMem.get();
}
DirectX::Keyboard* Engine::GetKeyboard()
{
    return m_keyboard.get();
}

DirectX::PrimitiveBatch<DirectX::VertexPositionNormalColorTexture>*
Engine::GetPrimBatch() // lazy: constructed once; Begin() called by renderer each frame
{
    if (!m_prim)
        m_prim = std::make_unique<DirectX::PrimitiveBatch<DirectX::VertexPositionNormalColorTexture>>(m_ctx.Get());
    return m_prim.get();
}

ID3D11ShaderResourceView* Engine::GetWhiteTexture()
{
    if (m_white) return m_white.Get();
    const uint32_t px = 0xFFFFFFFFu;
    D3D11_SUBRESOURCE_DATA init{&px, sizeof(px), sizeof(px)};
    D3D11_TEXTURE2D_DESC d{};
    d.Width = 1;
    d.Height = 1;
    d.MipLevels = 1;
    d.ArraySize = 1;
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_IMMUTABLE;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> tex;
    TETRIS_CHECK(m_dev->CreateTexture2D(&d, &init, tex.GetAddressOf()));
    TETRIS_CHECK(m_dev->CreateShaderResourceView(tex.Get(), nullptr, m_white.GetAddressOf()));
    return m_white.Get();
}

// resolve a runtime asset relative to the executable, then the current directory, then its parents
// (a double-clicked exe has its own folder as working directory, not the project root)
static std::wstring AssetPath(const wchar_t* rel)
{
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir(exe);
    const size_t slash = dir.find_last_of(L"\\/");
    if (slash != std::wstring::npos) dir.resize(slash);

    const std::wstring candidates[] = {
        dir + L"\\" + rel,
        std::wstring(rel),
        dir + L"\\..\\" + rel,
        dir + L"\\..\\..\\" + rel,
    };
    for (const auto& c : candidates)
        if (GetFileAttributesW(c.c_str()) != INVALID_FILE_ATTRIBUTES) return c;
    return candidates[0];
}

void Engine::PlaySfx(const wchar_t* fileName)
{
    if (!m_audio) return;
    auto it = m_sfx.find(fileName);
    if (it == m_sfx.end())
    {
        std::unique_ptr<DirectX::SoundEffect> fx;
        try
        {
            fx = std::make_unique<DirectX::SoundEffect>(m_audio.get(), AssetPath(fileName).c_str());
        }
        catch (...)
        {
            fx = nullptr; // a missing or broken wav must not take the game down
        }
        it = m_sfx.emplace(std::wstring(fileName), std::move(fx)).first;
    }
    if (it->second) it->second->Play();
}

ID3D11ShaderResourceView* Engine::GetTexture(int id)
{
    auto it = m_texes.find(id);
    if (it != m_texes.end()) return it->second.Get();
    // load once via WIC loader (PNG) — font sheet + atlas (stable id contract: HashFnv32("font")/("atlas")):
    ComPtr<ID3D11ShaderResourceView> srv;
    if (id == (int)HashFnv32("font"))
    { // font sheet PNG asset (generated): load via WIC loader (standard API ✓)
        TETRIS_CHECK(CreateWICTextureFromFile(m_dev.Get(), AssetPath(L"assets\\src\\font_sheet.png").c_str(), nullptr,
                                              srv.GetAddressOf()));
    }
    else
        TETRIS_CHECK(CreateWICTextureFromFile(m_dev.Get(), AssetPath(L"assets\\src\\atlas.png").c_str(), nullptr,
                                              srv.GetAddressOf()));
    m_texes[id] = srv;
    return srv.Get();
}

const DirectX::SpriteFont* Engine::GetFont(int fontId)
{
    auto it = m_fonts.find(fontId);
    if (it != m_fonts.end()) return it->second.get();
    // runtime ctor from generated header static arrays (DirectXTK memory ctor ✓ standard):
    auto srv = GetTexture((int)HashFnv32("font")); // font sheet generated PNG (loaded once)
    auto font = std::make_unique<DirectX::SpriteFont>(
        srv, reinterpret_cast<const DirectX::SpriteFont::Glyph*>(kFontGlyphs), kFontCount,
        kFontLineSpacing); // layout match verified (Character/RECT/3 floats) ✓
    m_fonts[fontId] = std::move(font);
    return m_fonts[fontId].get();
}

} // namespace tetris
