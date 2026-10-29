#pragma once
// core.h — shared minimal core (MINE, final; no subagents).
// RAII contract: all D3D objects are COM (ComPtr, DirectXTK headers); UniqueHandle for non-CCOM handles.
// No WRL::Range, no WRL event masks, no dynamic JSON — generated header data only.

#ifndef NOMINMAX // windows.h's min/max macros would break std::min/std::max/std::clamp
#define NOMINMAX
#endif

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cmath>
#include <stdexcept>
#include <string>
#include <wrl/client.h> // Microsoft::WRL::ComPtr only (no Event client headers)

// FNV-1a 32-bit id hash (stable ids across runs — contract)
inline constexpr uint32_t HashFnv32(const char* s)
{
    uint32_t h = 2166136261u;
    for (; *s; ++s)
        h = (h ^ (unsigned char)*s) * 16777619u;
    return h;
}

// sRGB8 -> linear (0..1), for shader-side lighting (contract: linear light on RGBA8 textures)
inline float SrgbToLinear(float c)
{
    return c <= 0.0031308f ? c * 12.9232101f : 1.055f * (c < 0.0f ? 0.0f : c) - 0.055f;
}

inline float Saturate(float c)
{
    return c < 0.f ? 0.f : (c > 1.f ? 1.f : c);
}
inline float SrgbToLinear01(float c)
{
    return Saturate(c) <= 0.0031308f ? c * 12.9232101f : 1.055f * (c < 0.0f ? 0.0f : c) - 0.055f;
}
inline float LinearToSrgb(float c)
{
    c = Saturate(c);
    return c < 0.00197421f ? c * 12.9232101f : 1.055f * powf(c, 1.f / 2.4f) - 0.055f;
}

// init failures throw a real, catchable exception; bare `throw;` has no active exception and aborts the process
[[noreturn]] inline void TetrisThrowCheck(long hr, const char* expr)
{
    char buf[256];
    snprintf(buf, sizeof(buf), "TETRIS_CHECK failed: %s (hr=0x%08lX)", expr, (unsigned long)hr);
    throw std::runtime_error(buf);
}
#define TETRIS_CHECK(hr)                                                                                               \
    {                                                                                                                  \
        HRESULT hr_ = (hr);                                                                                            \
        if (FAILED(hr_)) TetrisThrowCheck(hr_, #hr);                                                                   \
    } // RAII contract: init failures throw early; main catches
#define TETRIS_CHECK_W(hr)                                                                                             \
    {                                                                                                                  \
        HRESULT hr_ = (hr);                                                                                            \
        if (hr_ != S_OK)                                                                                               \
        {                                                                                                              \
            OutputDebugStringA("check failed: " #hr "\n");                                                             \
        }                                                                                                              \
    }

// UniqueHandle — RAII for non-CCOM handles (traits-based, contract)
template <typename T> struct HandleTraits; // specializations below

template <typename T> class UniqueHandle
{
public:
    UniqueHandle() = default;
    explicit UniqueHandle(T h) : h_(h) {}
    ~UniqueHandle()
    {
        if (!h_) return;
        Close();
    }
    UniqueHandle(UniqueHandle&& o) noexcept : h_(o.h_) { o.h_ = {}; }
    UniqueHandle& operator=(UniqueHandle&& o) noexcept
    {
        if (this != &o)
        {
            if (h_) Close();
            h_ = o.h_;
            o.h_ = {};
        }
        return *this;
    }
    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;
    explicit operator bool() const { return h_ != 0; }
    T get() const { return h_; }
    T* operator->() { return &h_; } // storage
    T& operator*() { return h_; }
    void reset(T h = {})
    {
        if (h_) Close();
        h_ = h;
    }
    T release()
    {
        T h = h_;
        h_ = {};
        return h;
    }

private:
    void Close() { HandleTraits<T>::Close(h_); }
    T h_{};
};

#include <windows.h> // for handle traits (HGDIP HMON...); GDI traits in cpp as needed

template <> struct HandleTraits<HANDLE>
{
    static void Close(HANDLE h)
    {
        if (h) CloseHandle(h);
    }
};

#include <SimpleMath.h>
namespace tetris
{
namespace Math = DirectX::SimpleMath;
}
