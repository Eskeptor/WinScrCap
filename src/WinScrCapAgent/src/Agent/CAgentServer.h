/**
 * @file         CAgentServer.h
 * @author       eskeptor (yc.jeon)
 * @date         2026-08-08
 * @version      1.0.0
 * @brief        Definition of the agent-side TCP server that streams captured frames to a remote client
 */

#pragma once

#include <Windows.h>
#include <winsock2.h>
#include <cstdint>
#include <string>
#include <atomic>
#include <thread>
#include <mutex>
#include <functional>

class CAgentCapture;
class CAgentEncoder;

/**
 * @enum     eHandshakeResult
 * @brief    Result of the client authentication handshake
 */
enum class eHandshakeResult : uint8_t
{
    Ok = 0,         /**< Authentication succeeded */
    BadAuth = 1,    /**< Authentication failed */
};

/**
 * @class    CAgentServer
 * @brief    TCP server that accepts a single remote client, authenticates it, and streams encoded frames
 * @details  Runs a worker thread that listens for and accepts a client connection, performs a
 *           challenge-response handshake, then continuously captures/encodes frames and sends them
 *           over the socket.
 */
class CAgentServer
{
public:
    CAgentServer();
    ~CAgentServer();

    /**
     * @brief       Binds the listen socket and starts the worker thread.
     * @param[in]   nPort           TCP port to listen on
     * @param[in]   pszPassword     Authentication password used for the handshake
     * @param[in]   pCapture        Capture source used to acquire frames (not owned)
     * @param[in]   pEncoder        Encoder used to compress frames (not owned)
     * @return      Whether initialization succeeded
     */
    bool Initialize(uint16_t nPort, const char* pszPassword, CAgentCapture* pCapture, CAgentEncoder* pEncoder);

    /**
     * @brief       Stops the worker thread and releases all socket resources.
     */
    void Shutdown();

    /**
     * @brief       Returns whether the server worker thread is currently running.
     * @return      true if running
     */
    bool IsRunning() const
    {
        return m_bRunning.load();
    }

    /**
     * @brief       Returns whether a client is currently connected.
     * @return      true if a client is connected
     */
    bool IsConnected() const
    {
        return m_bConnected.load();
    }

    /**
     * @brief       Returns the IP address of the currently (or most recently) connected client.
     * @return      Client IP address string
     */
    std::string GetClientIP() const
    {
        std::lock_guard<std::mutex> lk(m_mtxStatus);
        return m_strClientIP;
    }

private:
    /**
     * @brief       Worker thread entry point — accepts a client, performs the handshake, and streams frames.
     */
    void ThrWorker();

    /**
     * @brief       Computes SHA256(password bytes + 16-byte nonce) -> 32-byte hash for authentication.
     * @param[in]   pszPassword     Null-terminated password string
     * @param[in]   pNonce          16-byte nonce
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
     * @brief       Performs the challenge-response authentication handshake with a connected client.
     * @param[in]   hClient     Connected client socket
     * @return      Whether the handshake succeeded
     */
    bool DoHandshake(SOCKET hClient);

    /**
     * @brief       Sends a single encoded frame packet to the client, prefixed with a header.
     * @param[in]   hClient         Connected client socket
     * @param[in]   pData           Encoded H.264 packet data
     * @param[in]   nSize           Size of pData in bytes
     * @param[in]   nTimestampUs    Frame timestamp (microseconds)
     */
    void SendFramePacket(SOCKET hClient, const uint8_t* pData, int32_t nSize, int64_t nTimestampUs);

private:
    SOCKET m_hListenSocket;              /**< Listening TCP socket */
    char m_szPassword[128];              /**< Authentication password used for the handshake */

    CAgentCapture* m_pCapture;           /**< Capture source used to acquire frames (not owned) */
    CAgentEncoder* m_pEncoder;           /**< Encoder used to compress frames (not owned) */

    std::thread m_thrWorker;             /**< Background thread running accept/handshake/stream loop */
    std::atomic<bool> m_bRunning;        /**< Whether the worker thread is currently running */
    std::atomic<bool> m_bConnected;      /**< Whether a client is currently connected */

    mutable std::mutex m_mtxStatus;      /**< Protects access to m_strClientIP */
    std::string m_strClientIP;           /**< IP address of the current/last connected client */
};
