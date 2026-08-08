/**
 * @file         CMainWindow.h
 * @author       eskeptor (yc.jeon)
 * @date         2026-08-08
 * @version      1.0.0
 * @brief        메인 윈도우 UI 클래스 정의
 */

#pragma once

#include <string>
#include <imgui.h>
#include "Common/Types.h"

class CCaptureManager;

/**
 * @class    CMainWindow
 * @brief    ImGui 기반 메인 윈도우 UI 클래스
 * @details  DockSpace 레이아웃 위에 소스 선택, 프리뼷, 녹화 제어 패널을 렌더링한다.
 */
class CMainWindow
{
public:
    explicit CMainWindow(CCaptureManager* pCaptureManager);
    ~CMainWindow() = default;

    /**
     * @brief       매 프레임 ImGui UI 전체를 렌더링한다.
     */
    void Render();

private:
    /**
     * @brief       ImGui DockSpace를 생성하고 메뉴 바를 렌더링한다.
     */
    void RenderDockSpace();

    /**
     * @brief       캡처 소스(모니터/창) 선택 패널을 렌더링한다.
     */
    void RenderSourcePanel();

    /**
     * @brief       실시간 캡처 프리뷰 패널을 렌더링한다.
     */
    void RenderPreviewPanel();

    /**
     * @brief       녹화 시작/중지/일시정지 제어 패널을 렌더링한다.
     */
    void RenderControlPanel();

    /**
     * @brief       모니터 목록 선택 UI를 렌더링한다.
     */
    void RenderMonitorList();

    /**
     * @brief       창 목록 선택 UI를 렌더링한다.
     */
    void RenderWindowList();

    /**
     * @brief       FPS, 비트레이트, 출력 경로 등 인코더 설정 UI를 렌더링한다.
     */
    void RenderRecordingSettings();

    /**
     * @brief       Win32 저장 파일 대화상자를 열어 출력 경로를 설정한다.
     */
    void OpenSaveFileDialog();

    /**
     * @brief       Remote 소스 연결 설정 UI를 렌더링한다.
     */
    void RenderRemotePanel();

    /**
     * @brief       오류 메시지 팝업 창을 렌더링한다.
     */
    void RenderErrorPopup();

    /**
     * @brief       오류 메시지를 설정하고 팝업을 표시한다.
     * @param[in]   pszMsg          표시할 오류 메시지 문자열
     */
    void ShowError(const char* pszMsg);

    /**
     * @brief       초기 Dock 레이아웃을 설정한다.
     * @details     imgui.ini 파일이 없을 때 자동으로 적용된다.
     * @param[in]   nDockID         설정할 DockSpace ID
     */
    void SetupInitialLayout(ImGuiID nDockID);

private:
    CCaptureManager* m_pCaptureManager;

    eCaptureType m_eCaptureType;
    int32_t m_nSelectedMonitor;
    int32_t m_nSelectedWindow;

    // 녹화 설정 UI 상태
    char m_arrOutputPath[512];
    int32_t m_nTargetFps;
    int32_t m_nBitrateMbps;

    bool m_bShowImGuiDemo;
    std::string m_strLastError;
    bool m_bShowErrorPopup;

    // 분할 녹화 UI 상태
    bool m_bSplitEnabled;
    int32_t m_nSplitMinutes;

    // Remote 연결 UI 상태
    char m_arrRemoteHost[256];
    int32_t m_nRemotePort;
    char m_arrRemotePassword[128];

    // DockSpace ID (Reset Layout 버튼에서 슬롯 재배치 시 사용)
    ImGuiID m_nMainDockID;
};
