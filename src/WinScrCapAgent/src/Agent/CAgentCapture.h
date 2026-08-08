/**
 * @file         CAgentCapture.h
 * @author       eskeptor (yc.jeon)
 * @date         2026-08-08
 * @version      1.0.0
 */

#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <cstdint>

struct SCaptureBGRA
{
    const uint8_t* pData = nullptr;
    int32_t Width = 0;
    int32_t Height = 0;
    int32_t Stride = 0;
    bool bValid = false;
};

class CAgentCapture
{
public:
    explicit CAgentCapture(ID3D11Device* pDevice, ID3D11DeviceContext* pDeviceContext);
    ~CAgentCapture();

    bool Initialize(int32_t nMonitorIndex);
    void Shutdown();

    /**
     * @brief       최신 프레임을 스테이징으로 복사하고 BGRA 뷰를 반환한다.
     * @param[in]   pOut SCaptureBGRA 구조체에 프레임 데이터를 수신할 포인터
     * @return      true 새 프레임을 획득한 경우, false 그렇지 않은 경우
     */
    bool AcquireFrameBGRA(SCaptureBGRA* pOut);
    void ReleaseFrame();

    int32_t GetWidth() const
    {
        return m_nWidth;
    }
    int32_t GetHeight() const
    {
        return m_nHeight;
    }

private:
    bool CreateStagingTexture(int32_t nWidth, int32_t nHeight);

private:
    ID3D11Device* m_pDevice;
    ID3D11DeviceContext* m_pDeviceContext;
    IDXGIOutputDuplication* m_pDuplication;
    ID3D11Texture2D* m_pStagingTex;
    ID3D11Texture2D* m_pCurrentFrame;
    D3D11_MAPPED_SUBRESOURCE m_stMapped;

    int32_t m_nTargetMonitor;
    int32_t m_nWidth;
    int32_t m_nHeight;
    bool m_bFrameAcquired;
    bool m_bMapped;
};
