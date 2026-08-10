/**
 * @file         CApplication.h
 * @author       eskeptor (yc.jeon)
 * @date         2026-07-23
 * @version      1.0.0
 * @brief        Definition of the CApplication class
 * @details      The CApplication class owns the DirectX 11 device, swap chain, and render target view,
 *               manages ImGui initialization and the main render loop, and handles the Win32 message loop.
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
 * @brief    Top-level application class
 * @details  Owns the DirectX 11 device, swap chain, and render target view,
 *           manages ImGui initialization and the main render loop,
 *           and handles the Win32 message loop.
 */
class CApplication
{
public:
    /**
     * @brief       Default constructor — initializes member pointers to nullptr.
     */
    CApplication();

    /**
     * @brief       Destructor — calls Shutdown() to release all resources.
     */
    ~CApplication();

    /**
     * @brief       Initializes the application.
     * @details     Performs, in order: Win32 window creation -> DirectX 11 initialization -> ImGui initialization.
     * @param[in]   hInstance       Win32 application instance handle
     * @return      Whether initialization succeeded
     */
    bool Initialize(HINSTANCE hInstance);

    /**
     * @brief       Runs the main loop.
     * @details     Pumps the message loop, builds an ImGui frame, and presents it to the screen.
     *              When WM_QUIT is received, the loop ends and the exit code is returned.
     * @return      Process exit code
     */
    int Run();

    /**
     * @brief       Returns the DX11 device pointer.
     * @return      ID3D11Device* (not owned)
     */
    ID3D11Device* GetDevice() const
    {
        return m_pDevice;
    }

    /**
     * @brief       Returns the DX11 device context pointer.
     * @return      ID3D11DeviceContext* (not owned)
     */
    ID3D11DeviceContext* GetDeviceContext() const
    {
        return m_pDeviceContext;
    }

private:
    /**
     * @brief       Creates the Win32 window and registers the window class.
     * @param[in]   hInstance       Win32 application instance handle
     * @return      Whether the operation succeeded
     */
    bool InitWindow(HINSTANCE hInstance);

    /**
     * @brief       Initializes the DirectX 11 device, swap chain, and render target view.
     * @return      Whether the operation succeeded
     */
    bool InitDirectX();

    /**
     * @brief       Initializes the ImGui context and the Win32/DX11 backends.
     * @return      Whether the operation succeeded
     */
    bool InitImGui();

    /**
     * @brief       Releases all resources and shuts down ImGui.
     */
    void Shutdown();

    /**
     * @brief       Starts a new ImGui frame and clears the render target.
     */
    void BeginFrame();

    /**
     * @brief       Draws the ImGui render data via DX11 and calls SwapChain::Present.
     */
    void EndFrame();

    /**
     * @brief       Releases the current render target view.
     */
    void CleanupRenderTarget();

    /**
     * @brief       Creates a render target view from the swap chain's back buffer.
     */
    void CreateRenderTarget();

    /**
     * @brief       Win32 window message callback procedure.
     * @param[in]   hWnd        Handle of the window that received the message
     * @param[in]   nMsg        Message identifier
     * @param[in]   wParam      Additional message parameter (word)
     * @param[in]   lParam      Additional message parameter (long)
     * @return      Message processing result
     */
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam);

private:
    HWND m_hWnd;                                                      /**< Main window handle */
    ID3D11Device* m_pDevice;                                          /**< DirectX 11 device (owned) */
    ID3D11DeviceContext* m_pDeviceContext;                            /**< DirectX 11 immediate device context (owned) */
    IDXGISwapChain* m_pSwapChain;                                     /**< DXGI swap chain (owned) */
    ID3D11RenderTargetView* m_pRenderTargetView;                      /**< Render target view for the back buffer (owned) */

    std::unique_ptr<CMainWindow> m_cMainWindow;                       /**< Main ImGui window/UI logic */
    std::unique_ptr<CCaptureManager> m_cCaptureManager;               /**< Capture/encoding manager */

    bool m_bImGuiInitialized = false;                                 /**< Whether ImGui has been initialized */

    static constexpr int32_t WINDOW_WIDTH = 1280;                     /**< Default window width in pixels */
    static constexpr int32_t WINDOW_HEIGHT = 720;                     /**< Default window height in pixels */

    static constexpr wchar_t WINDOW_CLASS_NAME[] = L"WinScrCapClass"; /**< Win32 window class name */
    static constexpr wchar_t WINDOW_TITLE[] = L"WinScrCap";           /**< Window title text */
};
