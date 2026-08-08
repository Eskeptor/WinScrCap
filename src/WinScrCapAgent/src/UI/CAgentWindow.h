#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <cstdint>
#include <string>

#include "Common/AgentConfig.h"

class CAgentServer;

class CAgentWindow
{
public:
    CAgentWindow(ID3D11Device* pDevice,
                 ID3D11DeviceContext* pDeviceContext,
                 IDXGISwapChain* pSwapChain,
                 HWND hWnd,
                 CAgentServer* pServer);
    ~CAgentWindow();

    // ImGui 초기화 (CApplication에서 ImGui 설정 완료 후 호출)
    bool Initialize();
    void Shutdown();

    // 매 프레임 호출 — true 반환 시 계속, false 반환 시 Exit 요청
    bool Render(SAgentConfig& stConfig, const wchar_t* lpszIniPath);

    // 창을 숨기고 트레이로 최소화
    void HideToTray();

    // 설정 대화상자 열기 플래그 설정 (트레이 메뉴 → Settings 클릭 시)
    void RequestOpenSettings() { m_bOpenSettings = true; }

private:
    void RenderMainBody(const SAgentConfig& stConfig);
    void RenderSettingsDialog(SAgentConfig& stConfig, const wchar_t* lpszIniPath);

private:
    HWND                  m_hWnd;
    ID3D11Device*         m_pDevice;
    ID3D11DeviceContext*  m_pDeviceContext;
    IDXGISwapChain*       m_pSwapChain;
    ID3D11RenderTargetView* m_pRTV;
    CAgentServer*         m_pServer;

    bool m_bImGuiInitialized;
    bool m_bOpenSettings;
    bool m_bExitRequested;

    // 설정 대화상자 편집 버퍼 (설정 팝업이 열릴 때 복사해서 사용)
    SAgentConfig m_stEditConfig;
    char         m_szEditPassword[128];  // 평문 편집 버퍼
};
