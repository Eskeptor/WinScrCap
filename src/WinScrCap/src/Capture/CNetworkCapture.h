/**
 * @file         CNetworkCapture.h
 * @author       eskeptor (yc.jeon)
 * @date         2026-08-08
 * @version      1.0.0
 */

#pragma once

#include <winsock2.h>
#include <Windows.h>
#include <d3d11.h>
#include <cstdint>
#include <string>
#include <atomic>
#include <thread>
#include <mutex>
#include <queue>
#include <vector>

#include "ICapture.h"
#include "Common/Types.h"

class CNetworkCapture final : public ICapture
{
public:
    CNetworkCapture(ID3D11Device* pDevice, ID3D11DeviceContext* pDeviceContext);
    ~CNetworkCapture() override;

    /**
     * @brief       Initializes the capture source.
     * @copydoc     ICapture::Initialize()
     */
    bool Initialize() override;

    /**
     * @brief       Starts capture.
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
     * @brief       Initializes the remote capture target.
     * @param[in]   stTarget Remote capture target info (IP, port, password)
     * @return      Whether initialization succeeded
     */
    bool InitializeRemote(const SRemoteTarget& stTarget);

    // Remote resolution/FPS confirmed after handshake
    /**
     * @brief       Returns the remote frame width confirmed after the handshake.
     * @return      Remote frame width (pixels)
     */
    int32_t GetRemoteWidth() const
    {
        return m_nRemoteWidth;
    }

    /**
     * @brief       Returns the remote frame height confirmed after the handshake.
     * @return      Remote frame height (pixels)
     */
    int32_t GetRemoteHeight() const
    {
        return m_nRemoteHeight;
    }

    /**
     * @brief       Returns the remote frame rate confirmed after the handshake.
     * @return      Remote FPS
     */
    int32_t GetRemoteFps() const
    {
        return m_nRemoteFps;
    }

    /**
     * @brief       Returns whether the TCP connection is actually active.
     * @details     Unlike IsCapturing(), this returns false while a reconnect attempt is in progress.
     * @return      true if connected
     */
    bool IsConnected() const
    {
        return m_bConnected.load();
    }

    /**
     * @brief       Returns the connection status string.
     * @return      Connection status string
     */
    std::string GetStatusString() const
    {
        std::lock_guard<std::mutex> lk(m_mtxStatus);
        return m_strStatus;
    }

private:
    /**
     * @brief       Receive-thread entry point — decodes incoming H.264 packets and uploads frames to the GPU texture.
     */
    void ThrReceive();

    /**
     * @brief       Computes SHA256(password_bytes + nonce_16bytes) -> hash_32bytes for authentication.
     * @param[in]   pszPassword     Null-terminated password string
     * @param[in]   pNonce          16-byte nonce received from the server
     * @param[out]  pOutHash        32-byte output buffer receiving the computed hash
     * @return      Whether the hash was computed successfully
     */
    static bool ComputeAuthHash(const char* pszPassword, const uint8_t pNonce[16], uint8_t pOutHash[32]);

    /**
     * @brief       Sends the exact number of requested bytes over the socket, looping until complete.
     * @param[in]   hSock   Target socket
     * @param[in]   pData   Buffer to send
     * @param[in]   nLen    Number of bytes to send
     * @return      Whether all bytes were sent successfully
     */
    static bool SendAll(SOCKET hSock, const uint8_t* pData, int32_t nLen);

    /**
     * @brief       Receives the exact number of requested bytes from the socket, looping until complete.
     * @param[in]   hSock   Source socket
     * @param[out]  pData   Buffer receiving the data
     * @param[in]   nLen    Number of bytes to receive
     * @return      Whether all bytes were received successfully
     */
    static bool RecvAll(SOCKET hSock, uint8_t* pData, int32_t nLen);

    /**
     * @brief       Connects to the remote agent and performs the authentication handshake.
     * @return      Whether the connection and handshake succeeded
     */
    bool ConnectAndHandshake();

    /**
     * @brief       Updates the current status string.
     * @param[in]   pszMsg      Status message to set
     */
    void SetStatus(const char* pszMsg);

    /**
     * @brief       Creates (or re-creates) the GPU texture and staging texture used to receive decoded frames.
     * @param[in]   nWidth      Texture width (pixels)
     * @param[in]   nHeight     Texture height (pixels)
     * @return      Whether the textures were created successfully
     */
    bool CreateGpuTexture(int32_t nWidth, int32_t nHeight);

    /**
     * @brief       Uploads a BGRA buffer into the GPU texture.
     * @param[in]   pBGRA       Source BGRA pixel buffer
     * @param[in]   nWidth      Frame width (pixels)
     * @param[in]   nHeight     Frame height (pixels)
     * @return      Whether the upload succeeded
     */
    bool UploadBGRAToTexture(const uint8_t* pBGRA, int32_t nWidth, int32_t nHeight);

    /**
     * @brief       Disconnects the socket and updates the status with the given reason.
     * @param[in]   pszReason   Reason for the disconnection
     */
    void Disconnect(const char* pszReason);

private:
    ID3D11Device* m_pDevice;                    /**< DirectX 11 device (not owned) */
    ID3D11DeviceContext* m_pDeviceContext;      /**< DirectX 11 device context (not owned) */

    SRemoteTarget m_stTarget;                   /**< Remote agent connection target info */

    SOCKET m_hSocket;                           /**< TCP socket used to communicate with the remote agent */

    std::thread m_thrReceive;                   /**< Background thread that receives and decodes frames */
    std::atomic<bool> m_bCapturing;             /**< Whether capture is active (drives the receive thread loop) */
    std::atomic<bool> m_bConnected;             /**< Whether the TCP connection is currently established */

    // Decoded frame queue (keeps only the latest frame)
    mutable std::mutex m_mtxFrame;              /**< Protects access to the decoded frame/texture members */
    ID3D11Texture2D* m_pGpuTex;                 /**< GPU texture holding the latest decoded frame (owned) */
    ID3D11Texture2D* m_pStagingTex;             /**< Staging texture used to upload CPU data to the GPU texture (owned) */
    bool m_bFrameReady;                         /**< Whether a new frame is ready to be acquired */
    int32_t m_nGpuTexWidth;                     /**< Width of the current GPU texture */
    int32_t m_nGpuTexHeight;                    /**< Height of the current GPU texture */

    // Remote info
    int32_t m_nRemoteWidth;                     /**< Remote frame width confirmed after handshake */
    int32_t m_nRemoteHeight;                    /**< Remote frame height confirmed after handshake */
    int32_t m_nRemoteFps;                       /**< Remote frame rate confirmed after handshake */

    mutable std::mutex m_mtxStatus;             /**< Protects access to m_strStatus */
    std::string m_strStatus;                    /**< Human-readable connection status string */
};
