/**
 * @file         CWindowCapture.cpp
 * @author       eskeptor (yc.jeon)
 * @date         2026-07-23
 * @version      1.0.0
 */

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

#include <Windows.h>
#include <dxgi.h>
#include <d3d11.h>
#include <cassert>
#include <string>

#include "CWindowCapture.h"

struct CWindowCapture::SImpl
{
    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice D3DDevice{ nullptr };
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool FramePool{ nullptr };
    winrt::Windows::Graphics::Capture::GraphicsCaptureSession Session{ nullptr };
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame CurrentFrame{ nullptr };
    winrt::com_ptr<ID3D11Texture2D> CurrentTexture;
};

static BOOL CALLBACK EnumWindowsCallback(HWND hWnd, LPARAM lParam)
{
    if (!IsWindowVisible(hWnd))
    {
        return TRUE;
    }

    wchar_t szTitle[256]{};
    GetWindowTextW(hWnd, szTitle, ARRAYSIZE(szTitle));

    if (wcslen(szTitle) == 0)
    {
        return TRUE;
    }

    // 시스템 창(Program Manager 등) 제외
    if (GetShellWindow() == hWnd)
    {
        return TRUE;
    }

    std::vector<SWindowDesc>* pList = reinterpret_cast<std::vector<SWindowDesc>*>(lParam);
    SWindowDesc stDesc;
    {
        stDesc.hWnd = hWnd;
        stDesc.Title = szTitle;
    }
    pList->push_back(std::move(stDesc));

    return TRUE;
}

CWindowCapture::CWindowCapture(ID3D11Device* pDevice)
    : m_stImpl(std::make_unique<SImpl>())
    , m_pDevice(pDevice)
    , m_hTargetWnd(nullptr)
    , m_bCapturing(false)
{
    assert(pDevice != nullptr);
}

CWindowCapture::~CWindowCapture()
{
    StopCapture();
}

bool CWindowCapture::Initialize()
{
    try
    {
        // ID3D11Device → IDXGIDevice → WinRT IDirect3DDevice 변환
        IDXGIDevice* pDXGIDevice = nullptr;
        const HRESULT hr = m_pDevice->QueryInterface(IID_PPV_ARGS(&pDXGIDevice));
        if (FAILED(hr))
        {
            return false;
        }

        winrt::com_ptr<::IInspectable> pInspectable;
        const HRESULT hrCreate = CreateDirect3D11DeviceFromDXGIDevice(pDXGIDevice, pInspectable.put());
        pDXGIDevice->Release();

        if (FAILED(hrCreate))
        {
            return false;
        }

        m_stImpl->D3DDevice = pInspectable.as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();
    }
    catch (const winrt::hresult_error&)
    {
        return false;
    }

    RefreshWindowList();
    return true;
}

bool CWindowCapture::StartCapture()
{
    if (m_hTargetWnd == nullptr)
    {
        return false;
    }
    return StartCapture(m_hTargetWnd);
}

bool CWindowCapture::StartCapture(HWND hWnd)
{
    StopCapture();

    if (hWnd == nullptr)
    {
        return false;
    }

    try
    {
        // HWND → GraphicsCaptureItem (IGraphicsCaptureItemInterop 사용)
        winrt::com_ptr<IGraphicsCaptureItemInterop> pInterop = winrt::get_activation_factory<
            winrt::Windows::Graphics::Capture::GraphicsCaptureItem,
            IGraphicsCaptureItemInterop>();

        winrt::Windows::Graphics::Capture::GraphicsCaptureItem cItem{ nullptr };
        const HRESULT hr = pInterop->CreateForWindow(
            hWnd,
            winrt::guid_of<winrt::Windows::Graphics::Capture::GraphicsCaptureItem>(),
            winrt::put_abi(cItem));

        if (FAILED(hr) ||
            cItem == nullptr)
        {
            return false;
        }

        const winrt::Windows::Graphics::SizeInt32 cItemSize = cItem.Size();

        // FramePool 생성 (BGRA 포맷, 2 버퍼)
        m_stImpl->FramePool = winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool::Create(
            m_stImpl->D3DDevice,
            winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
            2,
            cItemSize);

        // Session 생성 및 캡처 시작
        m_stImpl->Session = m_stImpl->FramePool.CreateCaptureSession(cItem);
        m_stImpl->Session.IsCursorCaptureEnabled(false);
        m_stImpl->Session.StartCapture();
    }
    catch (const winrt::hresult_error&)
    {
        StopCapture();
        return false;
    }

    m_hTargetWnd = hWnd;
    m_bCapturing = true;
    return true;
}

void CWindowCapture::StopCapture()
{
    try
    {
        if (m_stImpl->CurrentTexture)
        {
            m_stImpl->CurrentTexture = nullptr;
        }
        if (m_stImpl->CurrentFrame)
        {
            m_stImpl->CurrentFrame.Close();
            m_stImpl->CurrentFrame = nullptr;
        }
        if (m_stImpl->Session)
        {
            m_stImpl->Session.Close();
            m_stImpl->Session = nullptr;
        }
        if (m_stImpl->FramePool)
        {
            m_stImpl->FramePool.Close();
            m_stImpl->FramePool = nullptr;
        }
    }
    catch (const winrt::hresult_error&)
    {
        // 정리 중 예외는 무시
    }

    m_bCapturing = false;
    m_hTargetWnd = nullptr;
}

bool CWindowCapture::AcquireFrame(SFrameData* pOutFrame)
{
    assert(pOutFrame != nullptr);

    if (!m_bCapturing ||
        !m_stImpl->FramePool)
    {
        return false;
    }

    try
    {
        // 이전 프레임 해제
        if (m_stImpl->CurrentTexture)
        {
            m_stImpl->CurrentTexture = nullptr;
        }
        if (m_stImpl->CurrentFrame)
        {
            m_stImpl->CurrentFrame.Close();
            m_stImpl->CurrentFrame = nullptr;
        }

        m_stImpl->CurrentFrame = m_stImpl->FramePool.TryGetNextFrame();
        if (!m_stImpl->CurrentFrame)
        {
            return false;
        }

        // WinRT surface → ID3D11Texture2D 추출
        winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DSurface cSurface = m_stImpl->CurrentFrame.Surface();
        winrt::com_ptr<Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess> pDxgiAccess =
            cSurface.as<Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();

        winrt::com_ptr<ID3D11Texture2D> pTexture;
        const HRESULT hr = pDxgiAccess->GetInterface(IID_PPV_ARGS(pTexture.put()));
        if (FAILED(hr) ||
            !pTexture)
        {
            return false;
        }

        D3D11_TEXTURE2D_DESC stDesc{};
        pTexture->GetDesc(&stDesc);

        // SImpl에 소유권 보관 (ReleaseFrame()에서 해제)
        m_stImpl->CurrentTexture = pTexture;

        pOutFrame->pTexture = pTexture.get();
        pOutFrame->Timestamp = 0;
        pOutFrame->Width = static_cast<int32_t>(stDesc.Width);
        pOutFrame->Height = static_cast<int32_t>(stDesc.Height);
    }
    catch (const winrt::hresult_error&)
    {
        return false;
    }

    return true;
}

void CWindowCapture::ReleaseFrame()
{
    try
    {
        m_stImpl->CurrentTexture = nullptr;

        if (m_stImpl->CurrentFrame)
        {
            m_stImpl->CurrentFrame.Close();
            m_stImpl->CurrentFrame = nullptr;
        }
    }
    catch (const winrt::hresult_error&)
    {
        // 무시
    }
}

bool CWindowCapture::IsCapturing() const
{
    return m_bCapturing;
}

void CWindowCapture::RefreshWindowList()
{
    m_arrWindows.clear();
    EnumWindows(EnumWindowsCallback, reinterpret_cast<LPARAM>(&m_arrWindows));
}
