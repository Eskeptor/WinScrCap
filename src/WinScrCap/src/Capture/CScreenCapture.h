/**
 * @file         CScreenCapture.h
 * @author       eskeptor (yc.jeon)
 * @date         2026-07-23
 * @version      1.0.0
 * @brief        DXGI Desktop Duplication API 기반 모니터 전체 캡처
 * @details      - Initialize() 시 DX11 디바이스 어댑터의 모든 모니터를 열거
 *               - StartCapture(nMonitorIndex)로 특정 모니터 캡처 시작
 *               - GPU 메모리에서 직접 텍스처 반환 (CPU 복사 없음)
 */

#pragma once

#include <dxgi1_2.h>
#include <d3d11.h>
#include <vector>

#include "ICapture.h"
#include "Common/Types.h"

/**
 * @class    CScreenCapture
 * @brief    DXGI Desktop Duplication API 기반 모니터 전체 캡처 클래스
 * @details  - Initialize() 시 DX11 디바이스 어댑터의 모든 모니터를 열거한다.
 *           - StartCapture(nMonitorIndex) 로 특정 모니터 캡처를 시작한다.
 *           - GPU 메모리에서 직접 텍스처를 반환하므로 CPU 복사가 없다.
 */
class CScreenCapture final : public ICapture
{
public:
    explicit CScreenCapture(ID3D11Device* pDevice);
    ~CScreenCapture() override;

    /**
     * @brief       캡처 소스를 초기화한다.
     * @copydoc     ICapture::Initialize()
     */
    bool Initialize() override;

    /**
     * @brief       기본 모니터(인덱스 0)으로 캡처를 시작한다.
     * @copydoc     ICapture::StartCapture()
     */
    bool StartCapture() override;

    /**
     * @brief       캡처를 중지한다.
     * @copydoc     ICapture::StopCapture()
     */
    void StopCapture() override;

    /**
     * @brief       프레임을 획득한다.
     * @copydoc     ICapture::AcquireFrame()
     */
    bool AcquireFrame(SFrameData* pOutFrame) override;

    /**
     * @brief       프레임을 해제한다.
     * @copydoc     ICapture::ReleaseFrame()
     */
    void ReleaseFrame() override;

    /**
     * @brief       캡처 상태를 반환한다.
     * @copydoc     ICapture::IsCapturing()
     */
    bool IsCapturing() const override;

    /**
     * @brief       특정 모니터 인덱스로 캡처를 시작한다.
     * @details     Initialize() 이후 호출 가능하다.
     * @param[in]   nMonitorIndex   캡처할 모니터 인덱스
     * @return      캡처 시작 성공 여부
     */
    bool StartCapture(int32_t nMonitorIndex);

    /**
     * @brief       열거된 모니터 목록을 반환한다.
     * @return      SMonitorDesc 배열 참조 (Initialize() 이후 유효)
     */
    const std::vector<SMonitorDesc>& GetMonitorList() const
    {
        return m_arrMonitors;
    }

private:
    /**
     * @brief       DXGI 어댑터를 순회하며 연결된 모니터 목록을 빌드한다.
     * @return      목록 빌드 성공 여부
     */
    bool BuildMonitorList();

private:
    ID3D11Device* m_pDevice;
    IDXGIOutputDuplication* m_pDuplication;
    ID3D11Texture2D* m_pCurrentFrame;

    int32_t m_nTargetMonitor;
    bool m_bCapturing;
    bool m_bFrameAcquired;

    std::vector<SMonitorDesc> m_arrMonitors;
};
