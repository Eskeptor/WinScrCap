/**
 * @file         CMainWindow.h
 * @author       eskeptor (yc.jeon)
 * @date         2026-08-08
 * @version      1.0.0
 * @brief        Definition of the main window UI class
 */

#pragma once

#include <string>
#include <imgui.h>
#include "Common/Types.h"

class CCaptureManager;

/**
 * @class    CMainWindow
 * @brief    ImGui-based main window UI class
 * @details  Renders the source selection, preview, and recording control panels on top of a DockSpace layout.
 */
class CMainWindow
{
public:
    explicit CMainWindow(CCaptureManager* pCaptureManager);
    ~CMainWindow() = default;

    /**
     * @brief       Renders the entire ImGui UI every frame.
     */
    void Render();

private:
    /**
     * @brief       Creates the ImGui DockSpace and renders the menu bar.
     */
    void RenderDockSpace();

    /**
     * @brief       Renders the capture source (monitor/window) selection panel.
     */
    void RenderSourcePanel();

    /**
     * @brief       Renders the real-time capture preview panel.
     */
    void RenderPreviewPanel();

    /**
     * @brief       Renders the recording start/stop/pause control panel.
     */
    void RenderControlPanel();

    /**
     * @brief       Renders the monitor list selection UI.
     */
    void RenderMonitorList();

    /**
     * @brief       Renders the window list selection UI.
     */
    void RenderWindowList();

    /**
     * @brief       Renders the encoder settings UI, such as FPS, bit rate, and output path.
     */
    void RenderRecordingSettings();

    /**
     * @brief       Opens the Win32 save file dialog to set the output path.
     */
    void OpenSaveFileDialog();

    /**
     * @brief       Renders the remote source connection settings UI.
     */
    void RenderRemotePanel();

    /**
     * @brief       Renders the error message popup window.
     */
    void RenderErrorPopup();

    /**
     * @brief       Sets the error message and displays the popup.
     * @param[in]   pszMsg          Error message string to display
     */
    void ShowError(const char* pszMsg);

    /**
     * @brief       Sets up the initial dock layout.
     * @details     Applied automatically when the imgui.ini file does not exist.
     * @param[in]   nDockID         DockSpace ID to configure
     */
    void SetupInitialLayout(ImGuiID nDockID);

private:
    CCaptureManager* m_pCaptureManager;              /**< Capture/encoding manager (not owned) */

    eCaptureType m_eCaptureType;                      /**< Currently selected capture source type */
    int32_t m_nSelectedMonitor;                       /**< Index of the currently selected monitor */
    int32_t m_nSelectedWindow;                        /**< Index of the currently selected window */

    char m_arrOutputPath[512];                        /**< Output file path buffer */
    int32_t m_nTargetFps;                             /**< Target frame rate (fps) */
    int32_t m_nBitrateMbps;                           /**< Target bit rate (Mbps) */

    bool m_bShowImGuiDemo;                            /**< Whether to show the ImGui demo window */
    std::string m_strLastError;                       /**< Last error message text */
    bool m_bShowErrorPopup;                           /**< Whether the error popup is currently shown */

    bool m_bSplitEnabled;                             /**< Whether split recording is enabled */
    int32_t m_nSplitMinutes;                          /**< Split interval (minutes) */

    char m_arrRemoteHost[256];                        /**< Remote agent host/IP input buffer */
    int32_t m_nRemotePort;                            /**< Remote agent port input value */
    char m_arrRemotePassword[128];                    /**< Remote agent password input buffer */
    bool m_bRemoteWasConnected;                       /**< Whether the remote connection was established on the previous frame */

    ImGuiID m_nMainDockID;                            /**< Root ImGui DockSpace ID */
};
