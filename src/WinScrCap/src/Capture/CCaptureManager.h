/**
 * @file         CCaptureManager.h
 * @author       eskeptor (yc.jeon)
 * @date         2026-07-23
 * @version      1.0.0
 * @brief        캡처 소스 관리 + D3D11 프리뷰 텍스처/SRV 유지
 * @details      - Update()를 매 프레임 메인 스레드에서 호출해야 함
 *               - StartScreenCapture / StartWindowCapture 로 소스 전환
 *               - GetPreviewSRV()를 ImGui::Image에 전달하여 프리뷰 출력
 */

#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <memory>
#include <cstdint>
#include <chrono>
#include <string>

#include "ICapture.h"
#include "Common/Types.h"

class CScreenCapture;
class CWindowCapture;
class CVideoEncoder;

/**
 * @class    CCaptureManager
 * @brief    캡처 소스 관리 + D3D11 프리뷰 텍스처/SRV 유지
 * @details  - Update() 를 매 프레임 메인 스레드에서 호출해야 한다.
 *           - StartScreenCapture / StartWindowCapture 로 캡쳐 소스를 전환한다.
 *           - GetPreviewSRV() 를 ImGui::Image 에 전달하면 실시간 프리뷰가 출력된다.
 */
class CCaptureManager
{
public:
    CCaptureManager(ID3D11Device* pDevice, ID3D11DeviceContext* pDeviceContext);
    ~CCaptureManager();

    /**
     * @brief       캡쳐 소스를 초기화하고 모니터 목록을 빌드한다.
     * @return      초기화 성공 여부
     */
    bool Initialize();

    /**
     * @brief       지정 모니터 인덱스로 화면 캡쳐를 시작한다.
     * @param[in]   nMonitorIndex       캡쳐할 모니터의 인덱스
     * @return      캡쳐 시작 성공 여부
     */
    bool StartScreenCapture(int32_t nMonitorIndex);

    /**
     * @brief       지정 창 핸들로 창 캡쳐를 시작한다.
     * @param[in]   hWnd                캡쳐할 대상 창 핸들
     * @return      캡쳐 시작 성공 여부
     */
    bool StartWindowCapture(HWND hWnd);

    /**
     * @brief       현재 활성 캡쳐를 중지한다.
     */
    void StopCapture();

    /**
     * @brief       매 프레임 호출 — 새 프레임 획득 및 프리뷰 텍스처 갱신
     * @details     메인 스레드에서 반드시 매 프레임 호출해야 한다.
     */
    void Update();

    /**
     * @brief       창 목록을 다시 열거한다.
     */
    void RefreshWindowList();

    /**
     * @brief       MP4 녹화를 시작한다.
     * @details     IsCapturing() 이 true 인 상태에서 호출해야 한다.
     * @param[in]   stConfig            인코더 및 출력 파일 설정
     * @return      녹화 시작 성공 여부
     */
    bool StartRecording(const SEncodeConfig& stConfig);

    /**
     * @brief       녹화를 중지하고 MP4 파일을 완성한다.
     */
    void StopRecording();

    /**
     * @brief       현재 녹화 중인지 여부를 반환한다.
     * @return      녹화 중이면 true
     */
    bool IsRecording() const;

    /**
     * @brief       현재까지 인코딩된 수 프레임 수를 반환한다.
     * @return      인코딩된 프레임 수
     */
    int64_t GetRecordedFrames() const;

    /**
     * @brief       녹화를 일시 정지한다.
     */
    void PauseRecording();

    /**
     * @brief       일시 정지된 녹화를 재개한다.
     */
    void ResumeRecording();

    /**
     * @brief       현재 녹화 상태를 반환한다.
     * @return      eRecordState 열거값
     */
    eRecordState GetRecordState() const
    {
        return m_eRecordState;
    }

    /**
     * @brief       실제 녹화된 총 경과 시간을 초 단위로 반환한다. (일시 정지 시간 제외)
     * @return      경과 시간 (초)
     */
    double GetElapsedSeconds() const;

    /**
     * @brief       현재 출력 파일 크기를 KB 단위로 반환한다.
     * @return      파일 크기 (KB)
     */
    int64_t GetOutputFileSizeKB() const;

    /**
     * @brief       현재 활성화된 인코더 유형을 반환한다.
     * @return      eEncoderType 열거값
     */
    eEncoderType GetActiveEncoderType() const;

    /**
     * @brief       현재 출력 파일 경로를 반환한다.
     * @return      출력 경로 문자열 참조
     */
    const std::wstring& GetCurrentOutputPath() const
    {
        return m_strOutputPath;
    }

    /**
     * @brief       ImGui::Image 에 전달할 프리뷰 SRV 포인터를 반환한다.
     * @return      ID3D11ShaderResourceView* (소유권 없음)
     */
    ID3D11ShaderResourceView* GetPreviewSRV() const
    {
        return m_pPreviewSRV;
    }

    /**
     * @brief       프리뷰 텍스처 너비를 반환한다.
     * @return      너비 (픽셀)
     */
    int32_t GetPreviewWidth() const
    {
        return m_nPreviewWidth;
    }

    /**
     * @brief       프리뷰 텍스처 높이를 반환한다.
     * @return      높이 (픽셀)
     */
    int32_t GetPreviewHeight() const
    {
        return m_nPreviewHeight;
    }

    /**
     * @brief       현재 캡쳐 중인지 여부를 반환한다.
     * @return      캡쳐 진행 중이면 true
     */
    bool IsCapturing() const;

    /**
     * @brief       내부 CScreenCapture 인스턴스 포인터를 반환한다.
     * @return      CScreenCapture* (소유권 없음)
     */
    CScreenCapture* GetScreenCapture() const
    {
        return m_cScreenCapture.get();
    }

    /**
     * @brief       내부 CWindowCapture 인스턴스 포인터를 반환한다.
     * @return      CWindowCapture* (소유권 없음)
     */
    CWindowCapture* GetWindowCapture() const
    {
        return m_cWindowCapture.get();
    }

private:
    /**
     * @brief       프리뷰용 D3D11 텍스처와 SRV를 생성한다.
     * @param[in]   nWidth          텍스처 너비 (픽셀)
     * @param[in]   nHeight         텍스처 높이 (픽셀)
     */
    void CreatePreviewResources(int32_t nWidth, int32_t nHeight);

    /**
     * @brief       프리뷰 텍스처와 SRV를 해제한다.
     */
    void ReleasePreviewResources();

    /**
     * @brief       분할 녹화 실행 — 현재 파일을 종료하고 새 파일로 인코더를 전환한다.
     */
    void PerformSplit();

    /**
     * @brief       타임스탬프 기반 분할 파일 경로를 생성한다.
     * @param[in]   strBase         기본 파일명 (확장자 제외)
     * @param[in]   strExt          파일 확장자 (예: L".mp4")
     * @return      타임스탬프가 포함된 분할 파일 경로
     */
    static std::wstring GenerateSplitPath(const std::wstring& strBase, const std::wstring& strExt);

private:
    ID3D11Device* m_pDevice;
    ID3D11DeviceContext* m_pDeviceContext;

    std::unique_ptr<CScreenCapture> m_cScreenCapture;
    std::unique_ptr<CWindowCapture> m_cWindowCapture;
    std::unique_ptr<CVideoEncoder> m_cVideoEncoder;

    ICapture* m_pActiveCapture;

    ID3D11Texture2D* m_pPreviewTexture;
    ID3D11ShaderResourceView* m_pPreviewSRV;
    int32_t m_nPreviewWidth;
    int32_t m_nPreviewHeight;

    // 인코딩 FPS 제어
    SEncodeConfig m_stEncodeConfig;
    std::chrono::steady_clock::time_point m_tLastEncodeTime;
    bool m_bFirstEncodeFrame;

    // 녹화 상태 머신 + 시간 추적
    eRecordState m_eRecordState;
    std::chrono::steady_clock::time_point m_tRecordStart;
    std::chrono::steady_clock::time_point m_tPauseStart;
    std::chrono::steady_clock::duration m_dPausedDuration;
    std::wstring m_strOutputPath;

    // 분할 녹화: 기본 경로/확장자 (타임스탬프 파일명 생성용)
    std::wstring m_strSplitBasePath;
    std::wstring m_strSplitExtension;
};
