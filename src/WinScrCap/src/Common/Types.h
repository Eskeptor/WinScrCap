/**
 * @file         Types.h
 * @author       eskeptor (yc.jeon)
 * @date         2026-08-08
 * @version      1.0.0
 * @brief        Definition of common types
 */

#pragma once

#include <Windows.h>
#include <cstdint>
#include <string>

// Forward declaration - avoids including d3d11.h in this common header
struct ID3D11Texture2D;

/**
 * @struct   SFrameData
 * @brief    Structure holding information for a single captured frame
 */
struct SFrameData
{
    ID3D11Texture2D* pTexture = nullptr;    /**< Pointer to the captured VRAM texture (not owned) */
    int64_t Timestamp = 0;                  /**< Frame acquisition time (microseconds) */
    int32_t Width = 0;                      /**< Texture width (pixels) */
    int32_t Height = 0;                     /**< Texture height (pixels) */
};

/**
 * @struct   SMonitorDesc
 * @brief    Structure holding information for an enumerated monitor
 */
struct SMonitorDesc
{
    int32_t Index = 0;                      /**< Monitor index based on the DXGI adapter */
    std::wstring Name;                      /**< Monitor device name (e.g. L"\\\\.\\DISPLAY1") */
    int32_t Width = 0;                      /**< Monitor resolution width (pixels) */
    int32_t Height = 0;                     /**< Monitor resolution height (pixels) */
    bool IsPrimary = false;                 /**< Whether this is the primary monitor */
};

/**
 * @struct   SWindowDesc
 * @brief    Structure holding information for a capturable window
 */
struct SWindowDesc
{
    HWND hWnd = nullptr;                    /**< Window handle */
    std::wstring Title;                     /**< Window title string */
};

/**
 * @struct   SEncodeConfig
 * @brief    Structure holding FFmpeg encoder and output file settings
 */
struct SEncodeConfig
{
    std::wstring OutputPath;                /**< Output MP4 file path */
    int32_t Width = 1920;                   /**< Output video width (pixels) */
    int32_t Height = 1080;                  /**< Output video height (pixels) */
    int32_t FrameRate = 30;                 /**< Target frame rate (fps) */
    int32_t BitRate = 8000000;              /**< Target bit rate (bps, default 8 Mbps) */

    bool bSplitEnabled = false;             /**< Whether split recording is enabled */
    int32_t nSplitMinutes = 60;             /**< Split interval (minutes, valid range 1 ~ 1440) */
};

/**
 * @struct   SRemoteTarget
 * @brief    Remote agent connection target information
 */
struct SRemoteTarget
{
    char szHost[256] = {};      /**< Agent IP or host name */
    uint16_t Port = 9201;       /**< Agent port */
    char szPassword[128] = {};  /**< Authentication password (plain text, used for challenge-response) */
};

/**
 * @enum     eCaptureType
 * @brief    Capture source type
 */
enum class eCaptureType
{
    Monitor,                                /**< Full-monitor capture (DXGI Desktop Duplication) */
    Window,                                 /**< Specific-window capture (Windows Graphics Capture API) */
    Remote,                                 /**< Remote agent capture (WinScrCapAgent TCP) */
};

/**
 * @enum     eEncoderType
 * @brief    H.264 encoder type
 */
enum class eEncoderType
{
    NVENC,                                  /**< NVIDIA GPU encoder (h264_nvenc) */
    AMF,                                    /**< AMD GPU encoder (h264_amf) */
    QSV,                                    /**< Intel Quick Sync encoder (h264_qsv) */
    MediaFoundation,                        /**< Windows MF software encoder (h264_mf, fallback) */
};

/**
 * @enum     eRecordState
 * @brief    Recording state machine state value
 */
enum class eRecordState
{
    Idle,                                   /**< Waiting to record (not started) */
    Recording,                              /**< Recording in progress */
    Paused,                                 /**< Recording paused */
    Stopping,                               /**< Finalizing the recording */
};
