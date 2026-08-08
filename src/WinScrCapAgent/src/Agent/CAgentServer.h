/**
 * @file         CAgentServer.h
 * @author       eskeptor (yc.jeon)
 * @date         2026-08-08
 * @version      1.0.0
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

enum class eHandshakeResult : uint8_t
{
    Ok = 0,
    BadAuth = 1,
};

class CAgentServer
{
public:
    CAgentServer();
    ~CAgentServer();

    // 소켓 바인딩 + 리슨 시작
    bool Initialize(uint16_t nPort, const char* pszPassword, CAgentCapture* pCapture, CAgentEncoder* pEncoder);
    void Shutdown();

    bool IsRunning() const
    {
        return m_bRunning.load();
    }
    bool IsConnected() const
    {
        return m_bConnected.load();
    }

    std::string GetClientIP() const
    {
        std::lock_guard<std::mutex> lk(m_mtxStatus);
        return m_strClientIP;
    }

private:
    void ThrWorker();

    // SHA256(pszPassword의 바이트 + pNonce 16바이트) → pOutHash 32바이트
    static bool ComputeAuthHash(const char* pszPassword, const uint8_t pNonce[16], uint8_t pOutHash[32]);

    static bool SendAll(SOCKET hSock, const uint8_t* pData, int32_t nLen);
    static bool RecvAll(SOCKET hSock, uint8_t* pData, int32_t nLen);

    bool DoHandshake(SOCKET hClient);
    void SendFramePacket(SOCKET hClient, const uint8_t* pData, int32_t nSize, int64_t nTimestampUs);

private:
    SOCKET m_hListenSocket;
    char m_szPassword[128];

    CAgentCapture* m_pCapture;
    CAgentEncoder* m_pEncoder;

    std::thread m_thrWorker;
    std::atomic<bool> m_bRunning;
    std::atomic<bool> m_bConnected;

    mutable std::mutex m_mtxStatus;
    std::string m_strClientIP;
};
