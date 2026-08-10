/**
 * @file         ICapture.h
 * @author       eskeptor (yc.jeon)
 * @date         2026-07-23
 * @version      1.0.0
 * @brief        Definition of the capture source interface
 */

#pragma once

#include "Common/Types.h"

struct SFrameData; // Defined in Types.h

/**
 * @class    ICapture
 * @brief    Abstract interface for capture sources
 * @details  A pure virtual interface that monitor capture (CScreenCapture) and
 *           window capture (CWindowCapture) must implement in common.
 */
class ICapture
{
public:
    virtual ~ICapture() = default;

    /**
     * @brief       Initializes the capture source.
     * @return      Whether initialization succeeded
     */
    virtual bool Initialize()  = 0;

    /**
     * @brief       Starts capture.
     * @return      Whether capture started successfully
     */
    virtual bool StartCapture() = 0;

    /**
     * @brief       Stops capture.
     */
    virtual void StopCapture() = 0;

    /**
     * @brief       Acquires the next frame.
     * @param[out]  pOutFrame       Pointer to the SFrameData that receives the acquired frame info
     * @return      Whether a frame was acquired successfully (false if no new frame is available)
     */
    virtual bool AcquireFrame(SFrameData* pOutFrame) = 0;

    /**
     * @brief       Releases the currently held frame.
     * @details     Must always be called after a successful AcquireFrame() so the internal buffer is returned normally.
     */
    virtual void ReleaseFrame() = 0;

    /**
     * @brief       Returns whether capture is currently in progress.
     * @return      true if capturing
     */
    virtual bool IsCapturing() const = 0;
};
