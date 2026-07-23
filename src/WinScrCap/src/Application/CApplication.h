/**
 * @file         CApplication.h
 * @author       eskeptor (yc.jeon)
 * @date         2026-07-23
 * @version      1.0.0
 * @brief        CApplication 클래스 정의
 * @details      CApplication 클래스는 DirectX 11 장치, 스왑 체인, 렌더 타겟 뷰를 소유하고, ImGui 초기화 및 메인 렌더 루프를 관리하며, Win32 메시지 루프를 처리합니다.
 */

#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <memory>
#include <cstdint>

class CMainWindow;
class CCaptureManager;

/**
 * @class    CApplication
 * @brief    애플리케이션 최상위 클래스
 * @details  DirectX 11 디바이스 · 스왑 체인 · RTV를 소유하고,
 *           ImGui 초기화 및 메인 렌더 루프를 관리하며,
 *           Win32 메시지 루프를 처리한다.
 */
class CApplication
{
public:
    /**
     * @brief       기본 생성자 — 멤버 포인터를 nullptr 로 초기화한다.
     */
    CApplication();

    /**
     * @brief       소멸자 — Shutdown() 을 호출하여 모든 리소스를 해제한다.
     */
    ~CApplication();

    /**
     * @brief       애플리케이션을 초기화한다.
     * @details     Win32 창 생성 → DirectX 11 초기화 → ImGui 초기화 순서로 수행한다.
     * @param[in]   hInstance       Win32 애플리케이션 인스턴스 핸들
     * @return      초기화 성공 여부
     */
    bool Initialize(HINSTANCE hInstance);

    /**
     * @brief       메인 루프를 실행한다.
     * @details     메시지 루프를 돌면서 ImGui 프레임을 생성하고 화면에 Present 한다.
     *              WM_QUIT 를 수신하면 루프를 종료하고 종료 코드를 반환한다.
     * @return      프로세스 종료 코드
     */
    int Run();

    /**
     * @brief       DX11 디바이스 포인터를 반환한다.
     * @return      ID3D11Device* (소유권 없음)
     */
    ID3D11Device* GetDevice() const
    {
        return m_pDevice;
    }

    /**
     * @brief       DX11 디바이스 컨텍스트 포인터를 반환한다.
     * @return      ID3D11DeviceContext* (소유권 없음)
     */
    ID3D11DeviceContext* GetDeviceContext() const
    {
        return m_pDeviceContext;
    }

private:
    /**
     * @brief       Win32 창을 생성하고 윈도우 클래스를 등록한다.
     * @param[in]   hInstance       Win32 애플리케이션 인스턴스 핸들
     * @return      성공 여부
     */
    bool InitWindow(HINSTANCE hInstance);

    /**
     * @brief       DirectX 11 디바이스, 스왑 체인, RTV를 초기화한다.
     * @return      성공 여부
     */
    bool InitDirectX();

    /**
     * @brief       ImGui 컨텍스트 및 Win32 · DX11 백엔드를 초기화한다.
     * @return      성공 여부
     */
    bool InitImGui();

    /**
     * @brief       모든 리소스를 해제하고 ImGui를 종료한다.
     */
    void Shutdown();

    /**
     * @brief       ImGui 새 프레임을 시작하고 렌더 타겟을 클리어한다.
     */
    void BeginFrame();

    /**
     * @brief       ImGui 렌더 데이터를 DX11로 드로우하고 SwapChain::Present 를 호출한다.
     */
    void EndFrame();

    /**
     * @brief       현재 렌더 타겟 뷰를 해제한다.
     */
    void CleanupRenderTarget();

    /**
     * @brief       스왑 체인의 백 버퍼로부터 렌더 타겟 뷰를 생성한다.
     */
    void CreateRenderTarget();

    /**
     * @brief       Win32 창 메시지 콜백 프로시저
     * @param[in]   hWnd        메시지를 받은 창 핸들
     * @param[in]   nMsg        메시지 식별자
     * @param[in]   wParam      메시지 부가 파라미터 (word)
     * @param[in]   lParam      메시지 부가 파라미터 (long)
     * @return      메시지 처리 결과
     */
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam);

private:
    HWND m_hWnd;
    ID3D11Device* m_pDevice;
    ID3D11DeviceContext* m_pDeviceContext;
    IDXGISwapChain* m_pSwapChain;
    ID3D11RenderTargetView* m_pRenderTargetView;

    std::unique_ptr<CMainWindow> m_cMainWindow;
    std::unique_ptr<CCaptureManager> m_cCaptureManager;

    bool m_bImGuiInitialized = false;

    static constexpr int32_t WINDOW_WIDTH = 1280;
    static constexpr int32_t WINDOW_HEIGHT = 720;

    static constexpr wchar_t WINDOW_CLASS_NAME[] = L"WinScrCapClass";
    static constexpr wchar_t WINDOW_TITLE[] = L"WinScrCap";
};
