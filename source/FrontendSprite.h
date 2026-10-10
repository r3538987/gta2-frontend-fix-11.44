#pragma once

#include "CSprite2d.h"
#include "Image.h"
#include "Other.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

// The GBH renderer caches device state. Restore the actual state after every
// custom primitive so the next native font/sprite draw agrees with that cache.
class FrontendRenderState {
    IDirect3DDevice3* device = GetD3DDevice();
    LPDIRECT3DTEXTURE2 texture = nullptr;
    static constexpr D3DRENDERSTATETYPE states[] = {
        D3DRENDERSTATE_SHADEMODE, D3DRENDERSTATE_ZENABLE,
        D3DRENDERSTATE_ZWRITEENABLE, D3DRENDERSTATE_ALPHABLENDENABLE,
        D3DRENDERSTATE_SRCBLEND, D3DRENDERSTATE_DESTBLEND,
        D3DRENDERSTATE_ALPHATESTENABLE, D3DRENDERSTATE_FOGENABLE,
        D3DRENDERSTATE_CULLMODE
    };
    static constexpr D3DTEXTURESTAGESTATETYPE stages[] = {
        D3DTSS_COLOROP, D3DTSS_COLORARG1, D3DTSS_COLORARG2,
        D3DTSS_ALPHAOP, D3DTSS_ALPHAARG1, D3DTSS_ALPHAARG2,
        D3DTSS_MINFILTER, D3DTSS_MAGFILTER
    };
    std::array<DWORD, std::size(states)> values{};
    std::array<DWORD, std::size(stages)> stageValues{};

public:
    FrontendRenderState() {
        device->GetTexture(0, &texture);
        for (size_t i = 0; i < values.size(); ++i)
            device->GetRenderState(states[i], &values[i]);
        for (size_t i = 0; i < stageValues.size(); ++i)
            device->GetTextureStageState(0, stages[i], &stageValues[i]);
    }
    ~FrontendRenderState() {
        device->SetTexture(0, texture);
        for (size_t i = 0; i < values.size(); ++i)
            device->SetRenderState(states[i], values[i]);
        for (size_t i = 0; i < stageValues.size(); ++i)
            device->SetTextureStageState(0, stages[i], stageValues[i]);
        if (texture)
            texture->Release();
    }
    FrontendRenderState(const FrontendRenderState&) = delete;
    FrontendRenderState& operator=(const FrontendRenderState&) = delete;

    static void Prepare(LPDIRECT3DTEXTURE2 texture, bool blend,
        D3DBLEND src = D3DBLEND_SRCALPHA, D3DBLEND dst = D3DBLEND_INVSRCALPHA) {
        auto device = GetD3DDevice();
        device->SetTexture(0, texture);
        device->SetRenderState(D3DRENDERSTATE_SHADEMODE, D3DSHADE_GOURAUD);
        device->SetRenderState(D3DRENDERSTATE_ZENABLE, FALSE);
        device->SetRenderState(D3DRENDERSTATE_ZWRITEENABLE, FALSE);
        device->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, blend);
        device->SetRenderState(D3DRENDERSTATE_SRCBLEND, src);
        device->SetRenderState(D3DRENDERSTATE_DESTBLEND, dst);
        device->SetRenderState(D3DRENDERSTATE_ALPHATESTENABLE, FALSE);
        device->SetRenderState(D3DRENDERSTATE_FOGENABLE, FALSE);
        device->SetRenderState(D3DRENDERSTATE_CULLMODE, D3DCULL_NONE);
        device->SetTextureStageState(0, D3DTSS_COLOROP, texture ? D3DTOP_MODULATE : D3DTOP_SELECTARG2);
        device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
        device->SetTextureStageState(0, D3DTSS_ALPHAOP, texture ? D3DTOP_MODULATE : D3DTOP_SELECTARG2);
        device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
        device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
        device->SetTextureStageState(0, D3DTSS_MINFILTER, D3DTFN_LINEAR);
        device->SetTextureStageState(0, D3DTSS_MAGFILTER, D3DTFG_LINEAR);
    }
};

// Bypass the SDK's palette-format selection and GDI BitBlt upload. A direct
// surface copy preserves all BGRA channels, including the cursor's alpha.
class FrontendSprite : public CSprite2d {
    std::vector<unsigned char> pixels;
    DWORD width = 0, height = 0;
    DWORD textureWidth = 0, textureHeight = 0;
    LPDIRECTDRAWSURFACE4 surface = nullptr;

    void ReleaseTexture() {
        if (m_pTexture) {
            m_pTexture->Release();
            m_pTexture = nullptr;
        }
        if (surface) {
            surface->Release();
            surface = nullptr;
        }
    }

public:
    FrontendSprite() = default;
    FrontendSprite(const FrontendSprite&) = delete;
    FrontendSprite& operator=(const FrontendSprite&) = delete;
    ~FrontendSprite() { ReleaseTexture(); }
    void Delete() {
        ReleaseTexture();
        pixels.clear();
    }

    bool SetTexture(const TCHAR*, unsigned short w, unsigned short h,
        unsigned int bitCount, unsigned char* data) {
        if (!data || !w || !h || bitCount != 32)
            return false;
        width = w;
        height = h;
        pixels.assign(data, data + static_cast<size_t>(w) * h * 4);
        return Reset();
    }

    bool SetTexture(const TCHAR* name) {
        plugin::Image* image = nullptr;
        if (!plugin::CreateImageFromFile(name, image))
            return false;
        if (image->width <= 0 || image->width > 0xffff ||
            image->height <= 0 || image->height > 0xffff) {
            image->Release();
            return false;
        }
        for (size_t i = 0; i < static_cast<size_t>(image->width) * image->height; ++i)
            std::swap(image->pixels[i * 4], image->pixels[i * 4 + 2]);
        bool result = SetTexture(name, image->width, image->height, 32, image->pixels);
        image->Release();
        return result;
    }

    bool Reset() {
        ReleaseTexture();
        if (pixels.empty())
            return false;
        auto device = GetD3DDevice();
        LPDIRECT3D3 d3d = nullptr;
        LPDIRECTDRAW4 dd = nullptr;
        if (FAILED(device->GetDirect3D(&d3d)))
            return false;
        HRESULT hr = d3d->QueryInterface(IID_IDirectDraw4, reinterpret_cast<void**>(&dd));
        d3d->Release();
        if (FAILED(hr))
            return false;

        DDSURFACEDESC2 desc{};
        desc.dwSize = sizeof(desc);
        desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
        desc.dwWidth = width;
        desc.dwHeight = height;
        D3DDEVICEDESC hardware{}, software{};
        hardware.dwSize = software.dwSize = sizeof(D3DDEVICEDESC);
        hr = device->GetCaps(&hardware, &software);
        if (FAILED(hr)) {
            dd->Release();
            return false;
        }
        const auto& caps = hardware.dwFlags ? hardware : software;
        if (caps.dpcTriCaps.dwTextureCaps & D3DPTEXTURECAPS_POW2) {
            for (desc.dwWidth = 1; desc.dwWidth < width; desc.dwWidth <<= 1) {}
            for (desc.dwHeight = 1; desc.dwHeight < height; desc.dwHeight <<= 1) {}
        }
        if (caps.dpcTriCaps.dwTextureCaps & D3DPTEXTURECAPS_SQUAREONLY)
            desc.dwWidth = desc.dwHeight = (std::max)(desc.dwWidth, desc.dwHeight);
        textureWidth = desc.dwWidth;
        textureHeight = desc.dwHeight;
        desc.ddsCaps.dwCaps = DDSCAPS_TEXTURE;
        desc.ddsCaps.dwCaps2 = DDSCAPS2_TEXTUREMANAGE;
        desc.ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
        desc.ddpfPixelFormat.dwFlags = DDPF_RGB | DDPF_ALPHAPIXELS;
        desc.ddpfPixelFormat.dwRGBBitCount = 32;
        desc.ddpfPixelFormat.dwRBitMask = 0x00ff0000;
        desc.ddpfPixelFormat.dwGBitMask = 0x0000ff00;
        desc.ddpfPixelFormat.dwBBitMask = 0x000000ff;
        desc.ddpfPixelFormat.dwRGBAlphaBitMask = 0xff000000;
        hr = dd->CreateSurface(&desc, &surface, nullptr);
        dd->Release();
        if (FAILED(hr))
            return false;

        DDSURFACEDESC2 locked{};
        locked.dwSize = sizeof(locked);
        hr = surface->Lock(nullptr, &locked, DDLOCK_WAIT | DDLOCK_WRITEONLY, nullptr);
        if (SUCCEEDED(hr)) {
            for (DWORD y = 0; y < textureHeight; ++y) {
                auto row = static_cast<unsigned char*>(locked.lpSurface) + static_cast<ptrdiff_t>(y) * locked.lPitch;
                auto source = pixels.data() + static_cast<size_t>((std::min)(y, height - 1)) * width * 4;
                std::memcpy(row, source, width * 4);
                // Extend edge pixels into padding for linear filtering.
                for (DWORD x = width; x < textureWidth; ++x)
                    std::memcpy(row + x * 4, source + (width - 1) * 4, 4);
            }
            hr = surface->Unlock(nullptr);
        }
        if (SUCCEEDED(hr))
            hr = surface->QueryInterface(IID_IDirect3DTexture2, reinterpret_cast<void**>(&m_pTexture));
        if (FAILED(hr))
            ReleaseTexture();
        return SUCCEEDED(hr);
    }

    void Draw(float x, float y, float w, float h, CRGBA const& color,
        D3DBLEND src = D3DBLEND_SRCALPHA, D3DBLEND dst = D3DBLEND_INVSRCALPHA) {
        Draw(CRect(x, y, x + w, y + h), color, src, dst);
    }
    void Draw(CRect const& rect, CRGBA const& color,
        D3DBLEND src = D3DBLEND_SRCALPHA, D3DBLEND dst = D3DBLEND_INVSRCALPHA) {
        if (!m_pTexture)
            return;
        FrontendRenderState saved;
        SetVertices(rect, color, color, color, color);
        ms_aVertices[0].u = ms_aVertices[3].u = 0.5f / textureWidth;
        ms_aVertices[1].u = ms_aVertices[2].u = (width - 0.5f) / textureWidth;
        ms_aVertices[0].v = ms_aVertices[1].v = 0.5f / textureHeight;
        ms_aVertices[2].v = ms_aVertices[3].v = (height - 0.5f) / textureHeight;
        FrontendRenderState::Prepare(m_pTexture, true, src, dst);
        RenderPrimitive(D3DPT_TRIANGLEFAN, ms_aVertices, 4);
    }
    static void DrawRect(CRect const& rect, CRGBA const& color, bool blend = false,
        D3DBLEND src = D3DBLEND_SRCALPHA, D3DBLEND dst = D3DBLEND_INVSRCALPHA) {
        FrontendRenderState saved;
        SetVertices(rect, color, color, color, color);
        FrontendRenderState::Prepare(nullptr, blend || color.a != 255, src, dst);
        RenderPrimitive(D3DPT_TRIANGLEFAN, ms_aVertices, 4);
    }
    static void Draw2DPolygon(float x1, float y1, float x2, float y2,
        float x3, float y3, float x4, float y4, CRGBA const& color) {
        FrontendRenderState saved;
        SetVertices(x1, y1, x2, y2, x3, y3, x4, y4, color, color, color, color);
        FrontendRenderState::Prepare(nullptr, color.a != 255);
        RenderPrimitive(D3DPT_TRIANGLEFAN, ms_aVertices, 4);
    }
};
