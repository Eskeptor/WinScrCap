/**
 * @file         CAgentServer.cpp
 * @author       eskeptor (yc.jeon)
 * @date         2026-08-10
 * @version      1.0.0
 */

#include <winsock2.h>
#include <ws2tcpip.h>
#include <bcrypt.h>
#include <Windows.h>
// NT_SUCCESS는 ntstatus.h가 Windows.h와 충돌하므로 직접 정의
#ifndef NT_SUCCESS
    #define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif
#include <cassert>
#include <chrono>
#include <cstring>

#include "CAgentServer.h"
#include "CAgentCapture.h"
#include "CAgentEncoder.h"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "bcrypt.lib")

// 프로토콜 매직 상수
namespace
{
    constexpr uint32_t PROTO_MAGIC_HELLO = 0x52435357u; // "WSCR"
    constexpr uint32_t PROTO_MAGIC_OK = 0x4F435357u; // "WSCO"
    constexpr uint32_t PROTO_MAGIC_FAIL = 0x46435357u; // "WSCF"
    constexpr uint32_t PROTO_MAGIC_FRAME = 0x4D415246u; // "FRAM"
    constexpr uint32_t PROTO_MAGIC_CTRL = 0x4C545243u; // "CTRL"
    constexpr uint8_t  PROTO_VERSION = 1;
    constexpr uint8_t  CTRL_PING = 0x01;
    constexpr uint8_t  CTRL_PONG = 0x02;
    constexpr uint8_t  CTRL_STOP = 0x03;
    constexpr int32_t  PING_INTERVAL_SEC = 5;
    constexpr int32_t  TIMEOUT_SEC = 10;
}

CAgentServer::CAgentServer()
    : m_hListenSocket(INVALID_SOCKET)
    , m_szPassword{}
    , m_pCapture(nullptr)
    , m_pEncoder(nullptr)
    , m_bRunning(false)
    , m_bConnected(false)
{
}

CAgentServer::~CAgentServer()
{
    Shutdown();
}

bool CAgentServer::Initialize(uint16_t nPort, const char* pszPassword, CAgentCapture* pCapture, CAgentEncoder* pEncoder)
{
    assert(pCapture != nullptr);
    assert(pEncoder != nullptr);

    WSADATA stWsa{};
    if (WSAStartup(MAKEWORD(2, 2), &stWsa) != 0)
    {
        return false;
    }

    m_pCapture = pCapture;
    m_pEncoder = pEncoder;
    if (pszPassword != nullptr)
    {
        strncpy_s(m_szPassword, pszPassword, _TRUNCATE);
    }

    m_hListenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_hListenSocket == INVALID_SOCKET)
    {
        WSACleanup();
        return false;
    }

    // SO_REUSEADDR 설정으로 재시작 시 포트 즉시 재사용
    const int32_t nReuse = 1;
    setsockopt(m_hListenSocket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&nReuse), sizeof(nReuse));

    sockaddr_in stAddr{};
    {
        stAddr.sin_family = AF_INET;
        stAddr.sin_addr.s_addr = INADDR_ANY;
        stAddr.sin_port = htons(nPort);
    }

    if (bind(m_hListenSocket, reinterpret_cast<sockaddr*>(&stAddr), sizeof(stAddr)) != 0)
    {
        // 원인 진단을 위해 에러 코드 출력 (예: WSAEADDRINUSE=10048, WSAEACCES=10013)
        // const int32_t nErr = WSAGetLastError();
        // char szBuf[128] = {};
        // sprintf_s(szBuf, "bind() failed. WSAGetLastError=%d, port=%u\n", nErr, nPort);
        // OutputDebugStringA(szBuf);

        closesocket(m_hListenSocket);
        m_hListenSocket = INVALID_SOCKET;
        WSACleanup();
        return false;
    }

    if (listen(m_hListenSocket, 1) != 0)
    {
        closesocket(m_hListenSocket);
        m_hListenSocket = INVALID_SOCKET;
        WSACleanup();
        return false;
    }

    m_bRunning = true;
    m_thrWorker = std::thread(&CAgentServer::ThrWorker, this);
    return true;
}

void CAgentServer::Shutdown()
{
    m_bRunning = false;
    if (m_hListenSocket != INVALID_SOCKET)
    {
        closesocket(m_hListenSocket);
        m_hListenSocket = INVALID_SOCKET;
    }
    if (m_thrWorker.joinable())
    {
        m_thrWorker.join();
    }
    WSACleanup();
    m_bConnected = false;
}

void CAgentServer::ThrWorker()
{
    using namespace std::chrono;

    while (m_bRunning.load())
    {
        // ── 1. 클라이언트 연결 대기 (100ms 타임아웃으로 폴링) ──────────
        fd_set stReadSet{};
        FD_ZERO(&stReadSet);
        FD_SET(m_hListenSocket, &stReadSet);
        timeval stTimeout{ 0, 100000 };  // 100ms

        const int32_t nSel = select(0, &stReadSet, nullptr, nullptr, &stTimeout);
        if (nSel <= 0 ||
            !FD_ISSET(m_hListenSocket, &stReadSet))
        {
            continue;
        }

        sockaddr_in stClientAddr{};
        int32_t nAddrLen = static_cast<int32_t>(sizeof(stClientAddr));
        SOCKET hClient = accept(m_hListenSocket,
            reinterpret_cast<sockaddr*>(&stClientAddr),
            &nAddrLen);

        if (hClient == INVALID_SOCKET)
        {
            continue;
        }

        // 클라이언트 IP 저장
        char szClientIP[64] = {};
        inet_ntop(AF_INET, &stClientAddr.sin_addr, szClientIP, sizeof(szClientIP));
        {
            std::lock_guard<std::mutex> lk(m_mtxStatus);
            m_strClientIP = szClientIP;
        }

        // 수신 타임아웃 설정
        DWORD dwRecvTimeout = static_cast<DWORD>(TIMEOUT_SEC) * 1000UL;
        setsockopt(hClient, SOL_SOCKET, SO_RCVTIMEO,
            reinterpret_cast<const char*>(&dwRecvTimeout), sizeof(dwRecvTimeout));

        // ── 2. 핸드셰이크 ──────────────────────────────────────────────
        if (!DoHandshake(hClient))
        {
            closesocket(hClient);
            continue;
        }

        m_bConnected = true;

        // ── 3. 캡처·인코딩·전송 루프 ───────────────────────────────────
        //const int64_t TARGET_US = 1000000LL / static_cast<int64_t>(m_pEncoder->GetWidth() > 0 ? 30 : 30);
        steady_clock::time_point tLast = steady_clock::now();
        steady_clock::time_point tLastPing = tLast;
        bool bError = false;

        while (m_bRunning.load() &&
            !bError)
        {
            const steady_clock::time_point tNow = steady_clock::now();
            const int64_t nElapsedUs = duration_cast<microseconds>(tNow - tLast).count();

            // Ping 전송
            const int64_t nPingSec = duration_cast<seconds>(tNow - tLastPing).count();
            if (nPingSec >= PING_INTERVAL_SEC)
            {
                const uint32_t nCtrlMagic = PROTO_MAGIC_CTRL;
                const uint8_t nType = CTRL_PING;
                if (!SendAll(hClient, reinterpret_cast<const uint8_t*>(&nCtrlMagic), 4) ||
                    !SendAll(hClient, &nType, 1))
                {
                    bError = true;
                    break;
                }
                tLastPing = tNow;
            }

            // FPS 타이밍 대기
            const int64_t FPS_TARGET_US = 1000000LL / 30LL;
            if (nElapsedUs < FPS_TARGET_US)
            {
                Sleep(1);
                continue;
            }
            tLast = tNow;

            // 프레임 캡처
            SCaptureBGRA stBGRA{};
            if (!m_pCapture->AcquireFrameBGRA(&stBGRA))
            {
                // 변화 없음 → 루프 지속
                continue;
            }

            // 인코딩 + 전송
            const int64_t nTimestampUs =
                duration_cast<microseconds>(tNow.time_since_epoch()).count();

            m_pEncoder->EncodeFrame(
                stBGRA.pData,
                stBGRA.Stride,
                nTimestampUs,
                [&](const uint8_t* pData, int32_t nSize)
                {
                    if (!bError)
                    {
                        SendFramePacket(hClient, pData, nSize, nTimestampUs);
                        // 전송 오류는 다음 반복에서 감지됨
                    }
                }
            );

            m_pCapture->ReleaseFrame();

            // Pong 수신 확인 (논블로킹 peek)
            fd_set stFd{};
            FD_ZERO(&stFd);
            FD_SET(hClient, &stFd);
            timeval stZero{ 0, 0 };
            if (select(0, &stFd, nullptr, nullptr, &stZero) > 0)
            {
                uint8_t arrCtrl[5] = {};
                const int32_t nRcv = recv(hClient, reinterpret_cast<char*>(arrCtrl), sizeof(arrCtrl), 0);
                if (nRcv <= 0)
                {
                    bError = true;
                }
                // CTRL_STOP 수신 시 정상 종료
                else if (nRcv >= 5 &&
                    *reinterpret_cast<const uint32_t*>(arrCtrl) == PROTO_MAGIC_CTRL &&
                    arrCtrl[4] == CTRL_STOP)
                {
                    bError = true;  // 클라이언트가 요청한 종료
                }
            }
        }

        closesocket(hClient);
        m_bConnected = false;
        {
            std::lock_guard<std::mutex> lk(m_mtxStatus);
            m_strClientIP.clear();
        }
    }
}

bool CAgentServer::DoHandshake(SOCKET hClient)
{
    // Server → Client: magic(4) + version(1) + nonce(16)
    uint8_t arrNonce[16] = {};
    BCryptGenRandom(nullptr, arrNonce, static_cast<ULONG>(sizeof(arrNonce)), BCRYPT_USE_SYSTEM_PREFERRED_RNG);

    const uint32_t nMagic = PROTO_MAGIC_HELLO;
    if (!SendAll(hClient, reinterpret_cast<const uint8_t*>(&nMagic), 4))
    {
        return false;
    }
    if (!SendAll(hClient, &PROTO_VERSION, 1))
    {
        return false;
    }
    if (!SendAll(hClient, arrNonce, 16))
    {
        return false;
    }

    // Client → Server: auth_hash(32)
    uint8_t arrClientHash[32] = {};
    if (!RecvAll(hClient, arrClientHash, 32))
    {
        return false;
    }

    // 패스워드가 없으면 auth 스킵
    const bool bNoPassword = (m_szPassword[0] == '\0');
    if (!bNoPassword)
    {
        uint8_t arrExpected[32] = {};
        if (!ComputeAuthHash(m_szPassword, arrNonce, arrExpected))
        {
            return false;
        }
        if (memcmp(arrClientHash, arrExpected, 32) != 0)
        {
            const uint32_t nFail = PROTO_MAGIC_FAIL;
            SendAll(hClient, reinterpret_cast<const uint8_t*>(&nFail), 4);
            return false;
        }
    }

    // Server → Client: ok(4) + width(4) + height(4) + fps(4)
    const uint32_t nOk = PROTO_MAGIC_OK;
    const int32_t nW = m_pCapture->GetWidth();
    const int32_t nH = m_pCapture->GetHeight();
    const int32_t nFps = 30;
    if (!SendAll(hClient, reinterpret_cast<const uint8_t*>(&nOk), 4))
    {
        return false;
    }
    if (!SendAll(hClient, reinterpret_cast<const uint8_t*>(&nW), 4))
    {
        return false;
    }
    if (!SendAll(hClient, reinterpret_cast<const uint8_t*>(&nH), 4))
    {
        return false;
    }
    if (!SendAll(hClient, reinterpret_cast<const uint8_t*>(&nFps), 4))
    {
        return false;
    }

    return true;
}

void CAgentServer::SendFramePacket(SOCKET hClient, const uint8_t* pData, int32_t nSize, int64_t nTimestampUs)
{
    const uint32_t nMagic = PROTO_MAGIC_FRAME;
    SendAll(hClient, reinterpret_cast<const uint8_t*>(&nMagic), 4);
    SendAll(hClient, reinterpret_cast<const uint8_t*>(&nSize), 4);
    SendAll(hClient, reinterpret_cast<const uint8_t*>(&nTimestampUs), 8);
    SendAll(hClient, pData, nSize);
}

bool CAgentServer::ComputeAuthHash(const char* pszPassword, const uint8_t pNonce[16], uint8_t pOutHash[32])
{
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    NTSTATUS sts = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (!NT_SUCCESS(sts))
    {
        return false;
    }

    BCRYPT_HASH_HANDLE hHash = nullptr;
    sts = BCryptCreateHash(hAlg, &hHash, nullptr, 0, nullptr, 0, 0);
    if (!NT_SUCCESS(sts))
    {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return false;
    }

    const ULONG nPwdLen = static_cast<ULONG>(strlen(pszPassword));
    if (NT_SUCCESS(sts))
    {
        sts = BCryptHashData(hHash, reinterpret_cast<PUCHAR>(const_cast<char*>(pszPassword)), nPwdLen, 0);
    }
    if (NT_SUCCESS(sts))
    {
        sts = BCryptHashData(hHash, const_cast<PUCHAR>(pNonce), 16, 0);
    }
    if (NT_SUCCESS(sts))
    {
        sts = BCryptFinishHash(hHash, pOutHash, 32, 0);
    }

    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return NT_SUCCESS(sts);
}

bool CAgentServer::SendAll(SOCKET hSock, const uint8_t* pData, int32_t nLen)
{
    int32_t nSent = 0;
    while (nSent < nLen)
    {
        const int32_t nResult = send(hSock, reinterpret_cast<const char*>(pData + nSent), nLen - nSent, 0);
        if (nResult == SOCKET_ERROR)
        {
            return false;
        }
        nSent += nResult;
    }
    return true;
}

bool CAgentServer::RecvAll(SOCKET hSock, uint8_t* pData, int32_t nLen)
{
    int32_t nRecv = 0;
    while (nRecv < nLen)
    {
        const int32_t nResult = recv(hSock, reinterpret_cast<char*>(pData + nRecv), nLen - nRecv, 0);
        if (nResult <= 0)
        {
            return false;
        }
        nRecv += nResult;
    }
    return true;
}
