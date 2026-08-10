/**
 * @file         CAgentCapture.h
 * @author       eskeptor (yc.jeon)
 * @date         2026-08-08
 * @version      1.0.0
 * @brief        Definition of the agent-side monitor capture class
 */

#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <cstdint>

/**
 * @struct   SCaptureBGRA
 * @brief    Structure holding a single captured BGRA frame mapped for CPU access
 */
struct SCaptureBGRA
{
    const uint8_t* pData = nullptr;    /**< Pointer to the mapped BGRA pixel data */
    int32_t Width = 0;                 /**< Frame width (pixels) */
    int32_t Height = 0;                /**< Frame height (pixels) */
    int32_t Stride = 0;                /**< Row pitch of the mapped data (bytes) */
    bool bValid = false;               /**< Whether this frame contains valid data */
};

/**
 * @class    CAgentCapture
 * @brief    DXGI Desktop Duplication-based monitor capture class used by the agent
 * @details  Captures a single monitor via Desktop Duplication and exposes the frame
 *           as a CPU-mapped BGRA buffer for encoding.
 */
class CAgentCapture
{
public:
    explicit CAgentCapture(ID3D11Device* pDevice, ID3D11DeviceContext* pDeviceContext);
    ~CAgentCapture();

    /**
     * @brief       Initializes Desktop Duplication for the specified monitor.
     * @param[in]   nMonitorIndex   Index of the monitor to capture
     * @return      Whether initialization succeeded
     */
    bool Initialize(int32_t nMonitorIndex);

    /**
     * @brief       Releases all capture resources.
     */
    void Shutdown();

    /**
     * @brief       Copies the latest frame into the staging texture and returns a BGRA view.
     * @param[out]  pOut Pointer to the SCaptureBGRA structure receiving the frame data
     * @return      true if a new frame was acquired, false otherwise
     */
    bool AcquireFrameBGRA(SCaptureBGRA* pOut);

    /**
     * @brief       Unmaps the staging texture and releases the currently held frame.
     */
    void ReleaseFrame();

    /**
     * @brief       Returns the current capture width.
     * @return      Width (pixels)
     */
    int32_t GetWidth() const
    {
        return m_nWidth;
    }

    /**
     * @brief       Returns the current capture height.
     * @return      Height (pixels)
     */
    int32_t GetHeight() const
    {
        return m_nHeight;
    }

private:
    /**
     * @brief       Creates (or re-creates) the CPU-readable staging texture used to map frames.
     * @param[in]   nWidth      Texture width (pixels)
     * @param[in]   nHeight     Texture height (pixels)
     * @return      Whether creation succeeded
     */
    bool CreateStagingTexture(int32_t nWidth, int32_t nHeight);

private:
    ID3D11Device* m_pDevice;                          /**< DirectX 11 device (not owned) */
    ID3D11DeviceContext* m_pDeviceContext;            /**< DirectX 11 device context (not owned) */
    IDXGIOutputDuplication* m_pDuplication;           /**< Desktop duplication interface for the target monitor */
    ID3D11Texture2D* m_pStagingTex;                   /**< CPU-readable staging texture (owned) */
    ID3D11Texture2D* m_pCurrentFrame;                 /**< Texture of the most recently acquired frame */
    D3D11_MAPPED_SUBRESOURCE m_stMapped;              /**< Mapped subresource info for the staging texture */

    int32_t m_nTargetMonitor;                          /**< Index of the monitor currently being captured */
    int32_t m_nWidth;                                  /**< Current capture width */
    int32_t m_nHeight;                                 /**< Current capture height */
    bool m_bFrameAcquired;                             /**< Whether a frame is currently acquired and not yet released */
    bool m_bMapped;                                    /**< Whether the staging texture is currently mapped */
};
