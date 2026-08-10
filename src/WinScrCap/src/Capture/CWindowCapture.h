/**
 * @file         CWindowCapture.h
 * @author       eskeptor (yc.jeon)
 * @date         2026-07-23
 * @version      1.0.0
 * @brief        Definition of the specific-window capture class based on the Windows Graphics Capture API (WGC)
 */

#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <memory>
#include <vector>

#include "ICapture.h"
#include "Common/Types.h"


/**
 * @class    CWindowCapture
 * @brief    Specific-window capture class based on the Windows Graphics Capture API (WGC)
 * @details  Captures on a per-HWND basis via WinRT COM,
 *           and returns a GPU texture using Direct3D11CaptureFramePool.
 */
class CWindowCapture final : public ICapture
{
    struct SImpl;

public:
    explicit CWindowCapture(ID3D11Device* pDevice);
    ~CWindowCapture() override;

    /**
     * @brief       Initializes the capture source.
     * @copydoc     ICapture::Initialize()
     */
    bool Initialize() override;

    /**
     * @brief       Starts capture on the last-specified window handle.
     * @copydoc     ICapture::StartCapture()
     */
    bool StartCapture() override;

    /**
     * @brief       Stops capture.
     * @copydoc     ICapture::StopCapture()
     */
    void StopCapture() override;

    /**
     * @brief       Acquires the next frame.
     * @copydoc     ICapture::AcquireFrame()
     */
    bool AcquireFrame(SFrameData* pOutFrame) override;

    /**
     * @brief       Releases the currently held frame.
     * @copydoc     ICapture::ReleaseFrame()
     */
    void ReleaseFrame() override;

    /**
     * @brief       Returns whether capture is currently in progress.
     * @copydoc     ICapture::IsCapturing()
     */
    bool IsCapturing() const override;

    /**
     * @brief       Starts capture on a specific window handle.
     * @details     Can be called after Initialize().
     * @param[in]   hWnd        Handle of the target window to capture
     * @return      Whether capture started successfully
     */
    bool StartCapture(HWND hWnd);

    /**
     * @brief       Refreshes the list of windows currently displayed on screen.
     */
    void RefreshWindowList();

    /**
     * @brief       Returns the refreshed window list.
     * @return      Reference to the SWindowDesc array (valid after RefreshWindowList())
     */
    const std::vector<SWindowDesc>& GetWindowList() const
    {
        return m_arrWindows;
    }

private:
    std::unique_ptr<SImpl> m_stImpl;                 /**< Opaque implementation details (WinRT COM objects) */

    ID3D11Device* m_pDevice;                         /**< DirectX 11 device (not owned) */
    HWND m_hTargetWnd;                               /**< Handle of the window currently being captured */
    bool m_bCapturing;                               /**< Whether capture is currently active */

    std::vector<SWindowDesc> m_arrWindows;           /**< List of windows enumerated by RefreshWindowList() */
};
