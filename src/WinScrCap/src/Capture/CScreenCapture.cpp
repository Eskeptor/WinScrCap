/**
 * @file         CCaptureManager.cpp
 * @author       eskeptor (yc.jeon)
 * @date         2026-07-23
 * @version      1.0.0
 */

#include <Windows.h>
#include <dxgi1_2.h>
#include <cassert>

#include "CScreenCapture.h"

CScreenCapture::CScreenCapture(ID3D11Device* pDevice)
    : m_pDevice(pDevice)
    , m_pDuplication(nullptr)
    , m_pCurrentFrame(nullptr)
    , m_nTargetMonitor(0)
    , m_bCapturing(false)
    , m_bFrameAcquired(false)
{
    assert(pDevice != nullptr);
}

CScreenCapture::~CScreenCapture()
{
    StopCapture();
}

bool CScreenCapture::Initialize()
{
    return BuildMonitorList();
}

bool CScreenCapture::StartCapture()
{
    return StartCapture(m_nTargetMonitor);
}

bool CScreenCapture::StartCapture(int32_t nMonitorIndex)
{
    StopCapture();

    // DX11 디바이스 → DXGI 어댑터 취득
    IDXGIDevice* pDXGIDevice = nullptr;
    HRESULT hr = m_pDevice->QueryInterface(IID_PPV_ARGS(&pDXGIDevice));
    if (FAILED(hr))
    {
        return false;
    }

    IDXGIAdapter* pAdapter = nullptr;
    hr = pDXGIDevice->GetAdapter(&pAdapter);
    pDXGIDevice->Release();
    if (FAILED(hr))
    {
        return false;
    }

    // 지정 인덱스의 출력(모니터) 취득
    IDXGIOutput* pOutput = nullptr;
    hr = pAdapter->EnumOutputs(static_cast<UINT>(nMonitorIndex), &pOutput);
    pAdapter->Release();
    if (FAILED(hr))
    {
        return false;
    }

    IDXGIOutput1* pOutput1 = nullptr;
    hr = pOutput->QueryInterface(IID_PPV_ARGS(&pOutput1));
    pOutput->Release();
    if (FAILED(hr))
    {
        return false;
    }

    hr = pOutput1->DuplicateOutput(m_pDevice, &m_pDuplication);
    pOutput1->Release();
    if (FAILED(hr))
    {
        return false;
    }

    m_nTargetMonitor = nMonitorIndex;
    m_bCapturing     = true;
    return true;
}

void CScreenCapture::StopCapture()
{
    if (m_bFrameAcquired)
    {
        ReleaseFrame();
    }

    if (m_pDuplication != nullptr)
    {
        m_pDuplication->Release();
        m_pDuplication = nullptr;
    }

    m_bCapturing = false;
}

bool CScreenCapture::AcquireFrame(SFrameData* pOutFrame)
{
    assert(pOutFrame != nullptr);

    if (!m_bCapturing ||
        m_pDuplication == nullptr)
    {
        return false;
    }

    DXGI_OUTDUPL_FRAME_INFO stFrameInfo{};
    IDXGIResource* pResource = nullptr;

    // timeout=0: 새 프레임이 없으면 즉시 반환
    const HRESULT hr = m_pDuplication->AcquireNextFrame(0, &stFrameInfo, &pResource);
    if (hr == static_cast<HRESULT>(DXGI_ERROR_WAIT_TIMEOUT))
    {
        return false;
    }

    if (hr == static_cast<HRESULT>(DXGI_ERROR_ACCESS_LOST))
    {
        // 해상도/DPI 변경 → Duplication 재생성
        StopCapture();
        StartCapture(m_nTargetMonitor);
        return false;
    }

    if (FAILED(hr))
    {
        return false;
    }

    ID3D11Texture2D* pTexture = nullptr;
    const HRESULT hrQI = pResource->QueryInterface(IID_PPV_ARGS(&pTexture));
    pResource->Release();

    if (FAILED(hrQI) ||
        pTexture == nullptr)
    {
        m_pDuplication->ReleaseFrame();
        return false;
    }

    D3D11_TEXTURE2D_DESC stDesc{};
    pTexture->GetDesc(&stDesc);

    // 현재 프레임 포인터 보관 (ReleaseFrame()에서 해제)
    m_pCurrentFrame = pTexture;
    m_bFrameAcquired = true;

    pOutFrame->pTexture = pTexture;
    pOutFrame->Timestamp = stFrameInfo.LastPresentTime.QuadPart;
    pOutFrame->Width = static_cast<int32_t>(stDesc.Width);
    pOutFrame->Height = static_cast<int32_t>(stDesc.Height);

    return true;
}

void CScreenCapture::ReleaseFrame()
{
    if (!m_bFrameAcquired)
    {
        return;
    }

    if (m_pCurrentFrame != nullptr)
    {
        m_pCurrentFrame->Release();
        m_pCurrentFrame = nullptr;
    }

    if (m_pDuplication != nullptr)
    {
        m_pDuplication->ReleaseFrame();
    }

    m_bFrameAcquired = false;
}

bool CScreenCapture::IsCapturing() const
{
    return m_bCapturing;
}

// ============================================================
//  Private
// ============================================================

bool CScreenCapture::BuildMonitorList()
{
    m_arrMonitors.clear();

    IDXGIDevice* pDXGIDevice = nullptr;
    HRESULT hr = m_pDevice->QueryInterface(IID_PPV_ARGS(&pDXGIDevice));
    if (FAILED(hr))
    {
        return false;
    }

    IDXGIAdapter* pAdapter = nullptr;
    hr = pDXGIDevice->GetAdapter(&pAdapter);
    pDXGIDevice->Release();
    if (FAILED(hr))
    {
        return false;
    }

    for (UINT nIdx = 0; ; ++nIdx)
    {
        IDXGIOutput* pOutput = nullptr;
        hr = pAdapter->EnumOutputs(nIdx, &pOutput);

        if (hr == static_cast<HRESULT>(DXGI_ERROR_NOT_FOUND))
        {
            break;
        }
        if (FAILED(hr))
        {
            break;
        }

        DXGI_OUTPUT_DESC stOutDesc = {};
        pOutput->GetDesc(&stOutDesc);
        pOutput->Release();

        SMonitorDesc stMonitor;
        {
            stMonitor.Index = static_cast<int32_t>(nIdx);
            stMonitor.Name = stOutDesc.DeviceName;
            stMonitor.Width = stOutDesc.DesktopCoordinates.right - stOutDesc.DesktopCoordinates.left;
            stMonitor.Height = stOutDesc.DesktopCoordinates.bottom - stOutDesc.DesktopCoordinates.top;
            stMonitor.IsPrimary = (stOutDesc.DesktopCoordinates.left == 0
                && stOutDesc.DesktopCoordinates.top == 0);
        }

        m_arrMonitors.push_back(std::move(stMonitor));
    }

    pAdapter->Release();
    return !m_arrMonitors.empty();
}
