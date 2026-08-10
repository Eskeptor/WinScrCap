/**
 * @file         CScreenCapture.h
 * @author       eskeptor (yc.jeon)
 * @date         2026-07-23
 * @version      1.0.0
 * @brief        DXGI Desktop Duplication API-based full-monitor capture
 * @details      - Initialize() enumerates all monitors of the DX11 device adapter.
 *               - StartCapture(nMonitorIndex) starts capturing a specific monitor.
 *               - Returns the texture directly from GPU memory (no CPU copy).
 */

#pragma once

#include <dxgi1_2.h>
#include <d3d11.h>
#include <vector>

#include "ICapture.h"
#include "Common/Types.h"

/**
 * @class    CScreenCapture
 * @brief    DXGI Desktop Duplication API-based full-monitor capture class
 * @details  - Initialize() enumerates all monitors of the DX11 device adapter.
 *           - StartCapture(nMonitorIndex) starts capturing a specific monitor.
 *           - Returns the texture directly from GPU memory, so there is no CPU copy.
 */
class CScreenCapture final : public ICapture
{
public:
    explicit CScreenCapture(ID3D11Device* pDevice);
    ~CScreenCapture() override;

    /**
     * @brief       Initializes the capture source.
     * @copydoc     ICapture::Initialize()
     */
    bool Initialize() override;

    /**
     * @brief       Starts capture on the default monitor (index 0).
     * @copydoc     ICapture::StartCapture()
     */
    bool StartCapture() override;

    /**
     * @brief       Stops capture.
     * @copydoc     ICapture::StopCapture()
     */
    void StopCapture() override;

    /**
     * @brief       Acquires a frame.
     * @copydoc     ICapture::AcquireFrame()
     */
    bool AcquireFrame(SFrameData* pOutFrame) override;

    /**
     * @brief       Releases a frame.
     * @copydoc     ICapture::ReleaseFrame()
     */
    void ReleaseFrame() override;

    /**
     * @brief       Returns the capture state.
     * @copydoc     ICapture::IsCapturing()
     */
    bool IsCapturing() const override;

    /**
     * @brief       Starts capture on a specific monitor index.
     * @details     Can be called after Initialize().
     * @param[in]   nMonitorIndex   Index of the monitor to capture
     * @return      Whether capture started successfully
     */
    bool StartCapture(int32_t nMonitorIndex);

    /**
     * @brief       Returns the enumerated monitor list.
     * @return      Reference to the SMonitorDesc array (valid after Initialize())
     */
    const std::vector<SMonitorDesc>& GetMonitorList() const
    {
        return m_arrMonitors;
    }

private:
    /**
     * @brief       Iterates over DXGI adapters and builds the list of connected monitors.
     * @return      Whether the list was built successfully
     */
    bool BuildMonitorList();

private:
    ID3D11Device* m_pDevice;                        /**< DirectX 11 device (not owned) */
    IDXGIOutputDuplication* m_pDuplication;          /**< DXGI desktop duplication interface for the active monitor */
    ID3D11Texture2D* m_pCurrentFrame;                /**< Texture of the most recently acquired frame */

    int32_t m_nTargetMonitor;                        /**< Index of the monitor currently being captured */
    bool m_bCapturing;                               /**< Whether capture is currently active */
    bool m_bFrameAcquired;                           /**< Whether a frame is currently acquired and not yet released */

    std::vector<SMonitorDesc> m_arrMonitors;         /**< List of monitors enumerated during Initialize() */
};
