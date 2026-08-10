/**
 * @file         CCaptureManager.h
 * @author       eskeptor (yc.jeon)
 * @date         2026-08-08
 * @version      1.0.0
 * @brief        Manages capture sources and maintains the D3D11 preview texture/SRV
 * @details      - Update() must be called on the main thread every frame.
 *               - Switch sources via StartScreenCapture / StartWindowCapture.
 *               - Pass GetPreviewSRV() to ImGui::Image to display the live preview.
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
class CNetworkCapture;
class CVideoEncoder;

/**
 * @class    CCaptureManager
 * @brief    Manages capture sources and maintains the D3D11 preview texture/SRV
 * @details  - Update() must be called on the main thread every frame.
 *           - Switch capture sources via StartScreenCapture / StartWindowCapture.
 *           - Passing GetPreviewSRV() to ImGui::Image renders the live preview.
 */
class CCaptureManager
{
public:
    CCaptureManager(ID3D11Device* pDevice, ID3D11DeviceContext* pDeviceContext);
    ~CCaptureManager();

    /**
     * @brief       Initializes capture sources and builds the monitor list.
     * @return      Whether initialization succeeded
     */
    bool Initialize();

    /**
     * @brief       Starts screen capture on the specified monitor index.
     * @param[in]   nMonitorIndex       Index of the monitor to capture
     * @return      Whether capture started successfully
     */
    bool StartScreenCapture(int32_t nMonitorIndex);

    /**
     * @brief       Starts window capture on the specified window handle.
     * @param[in]   hWnd                Handle of the target window to capture
     * @return      Whether capture started successfully
     */
    bool StartWindowCapture(HWND hWnd);

    /**
     * @brief       Stops the currently active capture.
     */
    void StopCapture();

    /**
     * @brief       Called every frame — acquires a new frame and updates the preview texture.
     * @details     Must be called on the main thread every single frame.
     */
    void Update();

    /**
     * @brief       Connects to a remote agent and starts remote screen capture.
     * @param[in]   stTarget        Remote agent connection info (IP, port, password)
     * @return      Whether capture started successfully
     */
    bool StartNetworkCapture(const SRemoteTarget& stTarget);

    /**
     * @brief       Returns a string describing the remote agent connection status.
     * @return      Status description string
     */
    std::string GetNetworkCaptureStatus() const;

    /**
     * @brief       Returns whether the remote agent is currently connected.
     * @return      true if connected
     */
    bool IsNetworkConnected() const;

    /**
     * @brief       Re-enumerates the window list.
     */
    void RefreshWindowList();

    /**
     * @brief       Starts MP4 recording.
     * @details     Must be called while IsCapturing() is true.
     * @param[in]   stConfig            Encoder and output file settings
     * @return      Whether recording started successfully
     */
    bool StartRecording(const SEncodeConfig& stConfig);

    /**
     * @brief       Stops recording and finalizes the MP4 file.
     */
    void StopRecording();

    /**
     * @brief       Returns whether recording is currently in progress.
     * @return      true if recording
     */
    bool IsRecording() const;

    /**
     * @brief       Returns the number of frames encoded so far.
     * @return      Number of encoded frames
     */
    int64_t GetRecordedFrames() const;

    /**
     * @brief       Pauses the current recording.
     */
    void PauseRecording();

    /**
     * @brief       Resumes a paused recording.
     */
    void ResumeRecording();

    /**
     * @brief       Returns the current recording state.
     * @return      eRecordState enum value
     */
    eRecordState GetRecordState() const
    {
        return m_eRecordState;
    }

    /**
     * @brief       Returns the total actual recorded elapsed time in seconds (excluding paused time).
     * @return      Elapsed time (seconds)
     */
    double GetElapsedSeconds() const;

    /**
     * @brief       Returns the current output file size in KB.
     * @return      File size (KB)
     */
    int64_t GetOutputFileSizeKB() const;

    /**
     * @brief       Returns the currently active encoder type.
     * @return      eEncoderType enum value
     */
    eEncoderType GetActiveEncoderType() const;

    /**
     * @brief       Returns the current output file path.
     * @return      Reference to the output path string
     */
    const std::wstring& GetCurrentOutputPath() const
    {
        return m_strOutputPath;
    }

    /**
     * @brief       Returns the preview SRV pointer to pass to ImGui::Image.
     * @return      ID3D11ShaderResourceView* (not owned)
     */
    ID3D11ShaderResourceView* GetPreviewSRV() const
    {
        return m_pPreviewSRV;
    }

    /**
     * @brief       Returns the preview texture width.
     * @return      Width (pixels)
     */
    int32_t GetPreviewWidth() const
    {
        return m_nPreviewWidth;
    }

    /**
     * @brief       Returns the preview texture height.
     * @return      Height (pixels)
     */
    int32_t GetPreviewHeight() const
    {
        return m_nPreviewHeight;
    }

    /**
     * @brief       Returns whether capture is currently in progress.
     * @return      true if capturing
     */
    bool IsCapturing() const;

    /**
     * @brief       Returns the internal CScreenCapture instance pointer.
     * @return      CScreenCapture* (not owned)
     */
    CScreenCapture* GetScreenCapture() const
    {
        return m_cScreenCapture.get();
    }

    /**
     * @brief       Returns the internal CWindowCapture instance pointer.
     * @return      CWindowCapture* (not owned)
     */
    CWindowCapture* GetWindowCapture() const
    {
        return m_cWindowCapture.get();
    }

    /**
     * @brief       Returns the internal CNetworkCapture instance pointer.
     * @return      CNetworkCapture* (not owned)
     */
    CNetworkCapture* GetNetworkCapture() const
    {
        return m_cNetworkCapture.get();
    }

private:
    /**
     * @brief       Creates the D3D11 texture and SRV used for the preview.
     * @param[in]   nWidth          Texture width (pixels)
     * @param[in]   nHeight         Texture height (pixels)
     */
    void CreatePreviewResources(int32_t nWidth, int32_t nHeight);

    /**
     * @brief       Releases the preview texture and SRV.
     */
    void ReleasePreviewResources();

    /**
     * @brief       Performs a split recording — finalizes the current file and switches the encoder to a new file.
     */
    void PerformSplit();

    /**
     * @brief       Generates a timestamp-based split file path.
     * @param[in]   strBase         Base file name (without extension)
     * @param[in]   strExt          File extension (e.g. L".mp4")
     * @return      Split file path including the timestamp
     */
    static std::wstring GenerateSplitPath(const std::wstring& strBase, const std::wstring& strExt);

private:
    ID3D11Device* m_pDevice;                    /**< DirectX 11 device (not owned) */
    ID3D11DeviceContext* m_pDeviceContext;      /**< DirectX 11 device context (not owned) */

    std::unique_ptr<CScreenCapture>  m_cScreenCapture;   /**< Screen capture source */
    std::unique_ptr<CWindowCapture>  m_cWindowCapture;   /**< Window capture source */
    std::unique_ptr<CNetworkCapture> m_cNetworkCapture;  /**< Network (remote agent) capture source */
    std::unique_ptr<CVideoEncoder>   m_cVideoEncoder;    /**< Video encoder used for recording */

    ICapture* m_pActiveCapture;                  /**< Currently active capture source (not owned) */

    ID3D11Texture2D* m_pPreviewTexture;          /**< Preview texture (owned) */
    ID3D11ShaderResourceView* m_pPreviewSRV;     /**< Preview shader resource view (owned) */
    int32_t m_nPreviewWidth;                     /**< Preview texture width */
    int32_t m_nPreviewHeight;                    /**< Preview texture height */

    // Encoding FPS control
    SEncodeConfig m_stEncodeConfig;
    std::chrono::steady_clock::time_point m_tLastEncodeTime;
    bool m_bFirstEncodeFrame;

    // Recording state machine + elapsed time tracking
    eRecordState m_eRecordState;
    std::chrono::steady_clock::time_point m_tRecordStart;
    std::chrono::steady_clock::time_point m_tPauseStart;
    std::chrono::steady_clock::duration m_dPausedDuration;
    std::wstring m_strOutputPath;

    // Split recording: base path/extension (used to generate timestamped file names)
    std::wstring m_strSplitBasePath;
    std::wstring m_strSplitExtension;
};
