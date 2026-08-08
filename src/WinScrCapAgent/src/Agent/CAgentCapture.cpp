/**
 * @file         CAgentCapture.cpp
 * @author       eskeptor (yc.jeon)
 * @date         2026-08-08
 * @version      1.0.0
 */

#include <Windows.h>
#include <dxgi1_2.h>
#include <cassert>
#include <cstring>

#include "CAgentCapture.h"

CAgentCapture::CAgentCapture(ID3D11Device* pDevice, ID3D11DeviceContext* pDeviceContext)
    : m_pDevice(pDevice)
    , m_pDeviceContext(pDeviceContext)
    , m_pDuplication(nullptr)
    , m_pStagingTex(nullptr)
    , m_pCurrentFrame(nullptr)
    , m_stMapped{}
    , m_nTargetMonitor(0)
    , m_nWidth(0)
    , m_nHeight(0)
    , m_bFrameAcquired(false)
    , m_bMapped(false)
{
    assert(pDevice != nullptr);
    assert(pDeviceContext != nullptr);
}

CAgentCapture::~CAgentCapture()
{
    Shutdown();
}

bool CAgentCapture::Initialize(int32_t nMonitorIndex)
{
    Shutdown();

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

    // 첫 프레임으로 해상도 확인
    {
        DXGI_OUTDUPL_FRAME_INFO stInfo{};
        IDXGIResource* pRes = nullptr;
        // AcquireNextFrame timeout=200ms for initial frame
        hr = m_pDuplication->AcquireNextFrame(200, &stInfo, &pRes);
        if (SUCCEEDED(hr) &&
            pRes != nullptr)
        {
            ID3D11Texture2D* pTex = nullptr;
            if (SUCCEEDED(pRes->QueryInterface(IID_PPV_ARGS(&pTex))) &&
                pTex != nullptr)
            {
                D3D11_TEXTURE2D_DESC stDesc{};
                pTex->GetDesc(&stDesc);
                m_nWidth  = static_cast<int32_t>(stDesc.Width);
                m_nHeight = static_cast<int32_t>(stDesc.Height);
                pTex->Release();
            }
            pRes->Release();
            m_pDuplication->ReleaseFrame();
        }
    }

    if (m_nWidth <= 0 ||
        m_nHeight <= 0)
    {
        // 해상도 미확인 시 기본값 사용 — 첫 AcquireFrameBGRA에서 재생성됨
        m_nWidth  = 1920;
        m_nHeight = 1080;
    }

    if (!CreateStagingTexture(m_nWidth, m_nHeight))
    {
        Shutdown();
        return false;
    }

    m_nTargetMonitor = nMonitorIndex;
    return true;
}

void CAgentCapture::Shutdown()
{
    if (m_bMapped)
    {
        m_pDeviceContext->Unmap(m_pStagingTex, 0);
        m_bMapped = false;
    }
    if (m_bFrameAcquired)
    {
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
    if (m_pStagingTex != nullptr)
    {
        m_pStagingTex->Release();
        m_pStagingTex = nullptr;
    }
    if (m_pDuplication != nullptr)
    {
        m_pDuplication->Release();
        m_pDuplication = nullptr;
    }
    m_nWidth  = 0;
    m_nHeight = 0;
}

bool CAgentCapture::AcquireFrameBGRA(SCaptureBGRA* pOut)
{
    assert(pOut != nullptr);
    if (m_pDuplication == nullptr)
    {
        return false;
    }

    if (m_bMapped)
    {
        m_pDeviceContext->Unmap(m_pStagingTex, 0);
        m_bMapped = false;
    }
    if (m_bFrameAcquired)
    {
        if (m_pCurrentFrame != nullptr)
        {
            m_pCurrentFrame->Release();
            m_pCurrentFrame = nullptr;
        }
        m_pDuplication->ReleaseFrame();
        m_bFrameAcquired = false;
    }

    DXGI_OUTDUPL_FRAME_INFO stInfo{};
    IDXGIResource* pRes = nullptr;
    const HRESULT hr = m_pDuplication->AcquireNextFrame(0, &stInfo, &pRes);

    if (hr == static_cast<HRESULT>(DXGI_ERROR_WAIT_TIMEOUT))
    {
        return false;
    }
    if (hr == static_cast<HRESULT>(DXGI_ERROR_ACCESS_LOST))
    {
        // 해상도 변경 등 → 재초기화
        Shutdown();
        Initialize(m_nTargetMonitor);
        return false;
    }
    if (FAILED(hr))
    {
        return false;
    }

    ID3D11Texture2D* pTex = nullptr;
    const HRESULT hrQI = pRes->QueryInterface(IID_PPV_ARGS(&pTex));
    pRes->Release();
    if (FAILED(hrQI) ||
        pTex == nullptr)
    {
        m_pDuplication->ReleaseFrame();
        return false;
    }

    D3D11_TEXTURE2D_DESC stDesc{};
    pTex->GetDesc(&stDesc);
    const int32_t nW = static_cast<int32_t>(stDesc.Width);
    const int32_t nH = static_cast<int32_t>(stDesc.Height);
    if (nW != m_nWidth ||
        nH != m_nHeight)
    {
        if (!CreateStagingTexture(nW, nH))
        {
            pTex->Release();
            m_pDuplication->ReleaseFrame();
            return false;
        }
        m_nWidth  = nW;
        m_nHeight = nH;
    }

    m_pCurrentFrame  = pTex;
    m_bFrameAcquired = true;

    m_pDeviceContext->CopyResource(m_pStagingTex, m_pCurrentFrame);

    const HRESULT hrMap = m_pDeviceContext->Map(m_pStagingTex, 0, D3D11_MAP_READ, 0, &m_stMapped);
    if (FAILED(hrMap))
    {
        return false;
    }

    m_bMapped = true;

    pOut->pData  = static_cast<const uint8_t*>(m_stMapped.pData);
    pOut->Width  = m_nWidth;
    pOut->Height = m_nHeight;
    pOut->Stride = static_cast<int32_t>(m_stMapped.RowPitch);
    pOut->bValid = true;
    return true;
}

void CAgentCapture::ReleaseFrame()
{
    if (m_bMapped)
    {
        m_pDeviceContext->Unmap(m_pStagingTex, 0);
        m_bMapped = false;
    }
    if (m_bFrameAcquired)
    {
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
}

bool CAgentCapture::CreateStagingTexture(int32_t nWidth, int32_t nHeight)
{
    if (m_pStagingTex != nullptr)
    {
        m_pStagingTex->Release();
        m_pStagingTex = nullptr;
    }

    D3D11_TEXTURE2D_DESC stDesc{};
    {
        stDesc.Width = static_cast<UINT>(nWidth);
        stDesc.Height = static_cast<UINT>(nHeight);
        stDesc.MipLevels = 1;
        stDesc.ArraySize = 1;
        stDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        stDesc.SampleDesc.Count = 1;
        stDesc.SampleDesc.Quality = 0;
        stDesc.Usage = D3D11_USAGE_STAGING;
        stDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    }

    const HRESULT hr = m_pDevice->CreateTexture2D(&stDesc, nullptr, &m_pStagingTex);
    return SUCCEEDED(hr);
}
