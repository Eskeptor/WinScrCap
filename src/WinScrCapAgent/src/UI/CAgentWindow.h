/**
 * @file         CAgentWindow.h
 * @author       eskeptor (yc.jeon)
 * @date         2026-08-10
 * @version      1.0.0
 * @brief        Definition of the agent's ImGui-based tray/settings UI window class
 */

#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <cstdint>
#include <string>

#include "Common/AgentConfig.h"

class CAgentServer;

/**
 * @class    CAgentWindow
 * @brief    Manages the agent's main window, tray icon, and settings dialog UI
 * @details  Owns the DX11 render target view used to draw the ImGui UI and delegates
 *           server start/stop and configuration persistence to the caller via SAgentConfig.
 */
class CAgentWindow
{
public:
    CAgentWindow(ID3D11Device* pDevice,
        ID3D11DeviceContext* pDeviceContext,
        IDXGISwapChain* pSwapChain,
        HWND hWnd,
        CAgentServer* pServer);
    ~CAgentWindow();

    /**
     * @brief       Initializes ImGui resources for this window.
     * @details     Must be called after CApplication has finished setting up ImGui.
     * @return      Whether initialization succeeded
     */
    bool Initialize();

    /**
     * @brief       Releases all ImGui resources owned by this window.
     */
    void Shutdown();

    /**
     * @brief       Renders the window UI for the current frame.
     * @param[in,out] stConfig      Current agent configuration, may be edited by the UI
     * @param[in]     lpszIniPath   Path to the INI file used to persist configuration changes
     * @return      true to keep running, false to request application exit
     */
    bool Render(SAgentConfig& stConfig, const wchar_t* lpszIniPath);

    /**
     * @brief       Hides the window and minimizes it to the system tray.
     */
    void HideToTray();

    /**
     * @brief       Requests that the settings dialog be opened (e.g. from the tray menu's Settings click).
     */
    void RequestOpenSettings()
    {
        m_bOpenSettings = true;
    }

private:
    /**
     * @brief       Renders the main window body (server status, start/stop controls, etc.).
     * @param[in]   stConfig    Current agent configuration used for display
     */
    void RenderMainBody(const SAgentConfig& stConfig);

    /**
     * @brief       Renders the settings dialog and writes edited values back to stConfig/the INI file.
     * @param[in,out] stConfig      Agent configuration to edit and persist
     * @param[in]     lpszIniPath   Path to the INI file to save changes to
     */
    void RenderSettingsDialog(SAgentConfig& stConfig, const wchar_t* lpszIniPath);

private:
    HWND m_hWnd;                                  /**< Window handle */
    ID3D11Device* m_pDevice;                      /**< DirectX 11 device (not owned) */
    ID3D11DeviceContext* m_pDeviceContext;        /**< DirectX 11 device context (not owned) */
    IDXGISwapChain* m_pSwapChain;                 /**< DXGI swap chain (not owned) */
    ID3D11RenderTargetView* m_pRTV;               /**< Render target view for the swap chain back buffer (owned) */
    CAgentServer* m_pServer;                      /**< Agent TCP server (not owned) */

    bool m_bImGuiInitialized;                     /**< Whether ImGui has been initialized for this window */
    bool m_bOpenSettings;                         /**< Whether the settings dialog should be opened this frame */
    bool m_bExitRequested;                        /**< Whether the user has requested application exit */

    SAgentConfig m_stEditConfig;                  /**< Working copy of the configuration being edited */
    char m_szEditPassword[128];                   /**< Plain-text password edit buffer */
};
