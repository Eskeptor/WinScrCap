/**
 * @file         CVideoEncoder.h
 * @author       eskeptor (yc.jeon)
 * @date         2026-07-23
 * @version      1.0.0
 * @brief        Definition of the FFmpeg-based H.264/MP4 encoder class
 * @details      - Supports multiple encoders such as NVENC, AMF, QSV, h264_mf
 *               - Path: D3D11 BGRA -> staging buffer -> NV12 -> encoder
 *               - third_party\ffmpeg\include\  (headers)
 *               - third_party\ffmpeg\lib\      (.lib files)
 *               - third_party\ffmpeg\bin\      (.dll files, required at runtime)
 */

#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <memory>
#include <cstdint>

#include "Common/Types.h"

/**
 * @class    CVideoEncoder
 * @brief    Definition of the FFmpeg-based H.264/MP4 encoder class
 * @details  - Automatically selects the encoder in the order NVENC → AMF → QSV → h264_mf.
 *           - Converts D3D11 BGRA textures to NV12 via a staging buffer before encoding.
 */
class CVideoEncoder
{
    struct SImpl;

public:
    CVideoEncoder(ID3D11Device* pDevice, ID3D11DeviceContext* pDeviceContext);
    ~CVideoEncoder();

    /**
     * @brief       Opens the encoder and prepares the MP4 output file.
     * @details     Attempts to initialize an HW encoder (NVENC/AMF/QSV), falling back to h264_mf on failure.
     * @param[in]   stConfig        Encoder and output file settings
     * @return      Whether initialization succeeded
     */
    bool Open(const SEncodeConfig& stConfig);

    /**
     * @brief       Submits a D3D11 texture to the encoder.
     * @details     Copies the texture to a staging buffer, then processes it in order: NV12 conversion -> FFmpeg AVFrame submission.
     * @param[in]   pTexture        Pointer to the D3D11 texture to encode
     * @return      Whether the frame was submitted successfully
     */
    bool SubmitFrame(ID3D11Texture2D* pTexture);

    /**
     * @brief       Closes the encoder, writes the MP4 trailer, and releases all resources.
     */
    void Close();

    /**
     * @brief       Returns whether the encoder is open (currently writing an MP4 file).
     * @return      true if open
     */
    bool IsOpen() const
    {
        return m_bOpen;
    }

    /**
     * @brief       Returns the currently active encoder type.
     * @return      eEncoderType enum value
     */
    eEncoderType GetActiveEncoder() const
    {
        return m_eActiveEncoder;
    }

    /**
     * @brief       Returns the number of frames encoded so far.
     * @return      Frame count
     */
    int64_t GetFrameCount() const
    {
        return m_nFrameCount;
    }

private:
    /**
     * @brief       Attempts to initialize the FFmpeg encoder with the specified codec name.
     * @param[in]   pszCodecName    FFmpeg codec name (e.g. "h264_nvenc")
     * @param[in]   stConfig        Encoder settings
     * @return      Whether initialization succeeded
     */
    bool TryOpenEncoder(const char* pszCodecName, const SEncodeConfig& stConfig);

    /**
     * @brief       Creates the staging texture used for CPU mapping.
     * @param[in]   nWidth          Texture width (pixels)
     * @param[in]   nHeight         Texture height (pixels)
     * @return      Whether creation succeeded
     */
    bool CreateStagingTexture(int32_t nWidth, int32_t nHeight);

    /**
     * @brief       Sends the converted AVFrame to the FFmpeg encoder.
     * @param[in]   pFrame          Pointer to the AVFrame to send
     * @return      Whether the frame was sent successfully
     */
    bool SendAvFrame(struct AVFrame* pFrame);

private:
    std::unique_ptr<SImpl> m_stImpl;                 /**< Opaque implementation details (FFmpeg contexts) */

    ID3D11Device* m_pDevice;                         /**< DirectX 11 device (not owned) */
    ID3D11DeviceContext* m_pDeviceContext;           /**< DirectX 11 device context (not owned) */
    ID3D11Texture2D* m_pStagingTex;                  /**< CPU-readable staging texture used before conversion (owned) */

    int32_t m_nStagingWidth;                         /**< Width of the current staging texture */
    int32_t m_nStagingHeight;                        /**< Height of the current staging texture */
    int64_t m_nFrameCount;                           /**< Number of frames encoded so far */
    bool m_bOpen;                                    /**< Whether the encoder is currently open */
    eEncoderType m_eActiveEncoder;                   /**< Currently active encoder type */
};
