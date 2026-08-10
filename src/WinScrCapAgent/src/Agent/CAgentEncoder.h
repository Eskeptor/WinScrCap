/**
 * @file         CAgentEncoder.h
 * @author       eskeptor (yc.jeon)
 * @date         2026-08-08
 * @version      1.0.0
 * @brief        Definition of the agent-side H.264 packet encoder class
 */

#pragma once

#include <cstdint>
#include <vector>
#include <functional>

// H.264 packet callback: used externally to send pData/nSize over TCP
using FnPacketCallback = std::function<void(const uint8_t* pData, int32_t nSize)>;

/**
 * @class    CAgentEncoder
 * @brief    FFmpeg-based H.264 encoder that produces raw packets for network streaming
 * @details  Encodes raw BGRA frames captured on the agent side into H.264 packets and
 *           delivers each encoded packet to the caller via a callback, rather than writing to a file.
 */
class CAgentEncoder
{
public:
    CAgentEncoder();
    ~CAgentEncoder();

    /**
     * @details     Initializes the H.264 encoder using the FFmpeg library.
     * @param[in]   nWidth Frame width
     * @param[in]   nHeight Frame height
     * @param[in]   nFps Frame rate
     * @param[in]   nCrf CRF value
     * @return      true on successful initialization, false on failure
     */
    bool Open(int32_t nWidth, int32_t nHeight, int32_t nFps, int32_t nCrf);

    /**
     * @brief       Closes the encoder and releases all resources.
     */
    void Close();

    /**
     * @brief       Returns whether the encoder is currently open.
     * @return      true if open
     */
    bool IsOpen() const
    {
        return m_bOpen;
    }

    /**
     * @brief       Returns the configured encoder width.
     * @return      Width (pixels)
     */
    int32_t GetWidth() const
    {
        return m_nWidth;
    }

    /**
     * @brief       Returns the configured encoder height.
     * @return      Height (pixels)
     */
    int32_t GetHeight() const
    {
        return m_nHeight;
    }

    /**
     * @brief       Encodes BGRA CPU data and delivers the resulting H.264 packet via callback.
     * @param[in]   pBGRA BGRA CPU data pointer
     * @param[in]   nStride Number of bytes per row of the BGRA CPU data (usually Width * 4)
     * @param[in]   nTimestampUs Frame timestamp (in microseconds)
     * @param[in]   fnCallback H.264 packet callback function
     * @return      true on successful encoding, false on failure
     */
    bool EncodeFrame(
        const uint8_t* pBGRA,
        int32_t nStride,
        int64_t nTimestampUs,
        const FnPacketCallback& fnCallback);

private:
    /**
     * @brief       Attempts to open the FFmpeg codec with the specified codec name.
     * @param[in]   pszName     FFmpeg codec name
     * @param[in]   nWidth      Frame width
     * @param[in]   nHeight     Frame height
     * @param[in]   nFps        Frame rate
     * @param[in]   nCrf        CRF value
     * @return      Whether initialization succeeded
     */
    bool TryOpenCodec(const char* pszName, int32_t nWidth, int32_t nHeight,
        int32_t nFps, int32_t nCrf);

    struct SImpl;
    SImpl* m_pImpl;                  /**< Opaque implementation details (FFmpeg contexts) */

    int32_t m_nWidth;                /**< Configured frame width */
    int32_t m_nHeight;               /**< Configured frame height */
    int32_t m_nFps;                  /**< Configured frame rate */
    int64_t m_nPts;                  /**< Running presentation timestamp counter */
    bool m_bOpen;                    /**< Whether the encoder is currently open */
};
