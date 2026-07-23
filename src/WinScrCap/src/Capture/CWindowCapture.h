/**
 * @file         CWindowCapture.h
 * @author       eskeptor (yc.jeon)
 * @date         2026-07-23
 * @version      1.0.0
 * @brief        Windows Graphics Capture API (WGC) 기반 특정 창 캡처 클래스 정의
 */

#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <memory>
#include <vector>

#include "ICapture.h"
#include "Common/Types.h"


/**
 * @class    CWindowCapture
 * @brief    Windows Graphics Capture API (WGC) 기반 특정 창 캡처 클래스
 * @details  WinRT COM 을 통해 HWND 단위로 캡처를 수행하며,
 *           Direct3D11CaptureFramePool 을 이용해 GPU 텍스처를 반환한다.
 */
class CWindowCapture final : public ICapture
{
    struct SImpl;

public:
    explicit CWindowCapture(ID3D11Device* pDevice);
    ~CWindowCapture() override;

    /**
     * @brief       초기화한다.
     * @copydoc     ICapture::Initialize()
     */
    bool Initialize() override;

    /**
     * @brief       마지막으로 지정된 창 핸들로 캡처를 시작한다.
     * @copydoc     ICapture::StartCapture()
     */
    bool StartCapture() override;

    /**
     * @brief       캡처를 중지한다.
     * @copydoc     ICapture::StopCapture()
     */
    void StopCapture() override;

    /**
     * @brief       다음 프레임을 획득한다.
     * @copydoc     ICapture::AcquireFrame()
     */
    bool AcquireFrame(SFrameData* pOutFrame) override;

    /**
     * @brief       현재 보유 중인 프레임을 해제한다.
     * @copydoc     ICapture::ReleaseFrame()
     */
    void ReleaseFrame() override;

    /**
     * @brief       현재 캡처 중인지 여부를 반환한다.
     * @copydoc     ICapture::IsCapturing()
     */
    bool IsCapturing() const override;

    /**
     * @brief       특정 창 핸들로 캡처를 시작한다.
     * @details     Initialize() 이후 호출 가능하다.
     * @param[in]   hWnd        캡처할 대상 창 핸들
     * @return      캡처 시작 성공 여부
     */
    bool StartCapture(HWND hWnd);

    /**
     * @brief       현재 화면에 표시 중인 창 목록을 갱신한다.
     */
    void RefreshWindowList();

    /**
     * @brief       갱신된 창 목록을 반환한다.
     * @return      SWindowDesc 배열 참조 (RefreshWindowList() 이후 유효)
     */
    const std::vector<SWindowDesc>& GetWindowList() const
    {
        return m_arrWindows;
    }

private:
    std::unique_ptr<SImpl> m_stImpl;

    ID3D11Device* m_pDevice;
    HWND m_hTargetWnd;
    bool m_bCapturing;

    std::vector<SWindowDesc> m_arrWindows;
};
