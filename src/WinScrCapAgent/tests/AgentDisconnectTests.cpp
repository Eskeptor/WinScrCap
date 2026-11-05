#include <winsock2.h>
#include <ws2tcpip.h>
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <thread>
#include <vector>

#include "Agent/CAgentServer.h"
#include "Agent/CAgentCapture.h"
#include "Agent/CAgentEncoder.h"

using namespace std::chrono_literals;

namespace
{
    std::atomic<bool> g_bProduceFrames{ false };
    std::atomic<bool> g_bEncoding{ false };
    std::atomic<int32_t> g_nReleased{ 0 };

    void Require(bool bCondition, const char* pszMessage)
    {
        if (!bCondition)
        {
            std::fprintf(stderr, "FAIL: %s (WSA=%d)\n", pszMessage, WSAGetLastError());
            std::exit(1);
        }
    }

    template<typename Fn>
    bool WaitUntil(Fn fn, std::chrono::milliseconds tTimeout = 2s)
    {
        const auto tDeadline = std::chrono::steady_clock::now() + tTimeout;
        while (!fn())
        {
            if (std::chrono::steady_clock::now() >= tDeadline)
            {
                return false;
            }
            std::this_thread::sleep_for(5ms);
        }
        return true;
    }

    void Receive(SOCKET hSocket, void* pBuffer, int32_t nSize)
    {
        auto* pBytes = static_cast<char*>(pBuffer);
        while (nSize > 0)
        {
            const int32_t nReceived = recv(hSocket, pBytes, nSize, 0);
            Require(nReceived > 0, "receive protocol bytes");
            pBytes += nReceived;
            nSize -= nReceived;
        }
    }

    void Send(SOCKET hSocket, const void* pBuffer, int32_t nSize)
    {
        const auto* pBytes = static_cast<const char*>(pBuffer);
        while (nSize > 0)
        {
            const int32_t nSent = send(hSocket, pBytes, nSize, 0);
            Require(nSent > 0, "send protocol bytes");
            pBytes += nSent;
            nSize -= nSent;
        }
    }

    struct SServer
    {
        CAgentCapture Capture{ nullptr, nullptr };
        CAgentEncoder Encoder;
        CAgentServer Server;
        uint16_t Port = 0;

        SServer()
        {
            g_bProduceFrames = false;
            g_bEncoding = false;
            g_nReleased = 0;
            SOCKET hProbe = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            Require(hProbe != INVALID_SOCKET, "create port probe");
            sockaddr_in stAddress{};
            stAddress.sin_family = AF_INET;
            stAddress.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            Require(bind(hProbe, reinterpret_cast<sockaddr*>(&stAddress), sizeof(stAddress)) == 0,
                "bind ephemeral port");
            int nLength = sizeof(stAddress);
            Require(getsockname(hProbe, reinterpret_cast<sockaddr*>(&stAddress), &nLength) == 0,
                "get ephemeral port");
            Port = ntohs(stAddress.sin_port);
            closesocket(hProbe);
            Require(Server.Initialize(Port, "", &Capture, &Encoder), "start agent server");
        }

        SOCKET Connect(bool bHandshake = true)
        {
            SOCKET hClient = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            Require(hClient != INVALID_SOCKET, "create client");
            const DWORD dwTimeout = 3000;
            Require(setsockopt(hClient, SOL_SOCKET, SO_RCVTIMEO,
                reinterpret_cast<const char*>(&dwTimeout), sizeof(dwTimeout)) == 0, "set receive timeout");
            const int nBufferSize = 4096;
            Require(setsockopt(hClient, SOL_SOCKET, SO_RCVBUF,
                reinterpret_cast<const char*>(&nBufferSize), sizeof(nBufferSize)) == 0, "set receive buffer");
            sockaddr_in stAddress{};
            stAddress.sin_family = AF_INET;
            stAddress.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            stAddress.sin_port = htons(Port);
            Require(connect(hClient, reinterpret_cast<sockaddr*>(&stAddress), sizeof(stAddress)) == 0,
                "connect client");
            uint8_t arrHello[21]{};
            Receive(hClient, arrHello, sizeof(arrHello));
            if (bHandshake)
            {
                const uint8_t arrHash[32]{};
                Send(hClient, arrHash, sizeof(arrHash));
                uint32_t arrReply[4]{};
                Receive(hClient, arrReply, sizeof(arrReply));
                Require(arrReply[0] == 0x4F435357u && arrReply[1] == 64 && arrReply[2] == 64,
                    "handshake response shape");
                Require(WaitUntil([&] { return Server.IsConnected(); }), "connected status");
            }
            return hClient;
        }

        void ExpectDisconnected()
        {
            Require(WaitUntil([&] { return !Server.IsConnected() && Server.GetClientIP().empty(); }),
                "return to disconnected state and clear client IP");
            Require(Server.IsRunning(), "agent continues listening");
        }
    };

    void TestIdleDisconnect(bool bReset)
    {
        SServer st;
        SOCKET hClient = st.Connect();
        if (bReset)
        {
            const linger stLinger{ 1, 0 };
            Require(setsockopt(hClient, SOL_SOCKET, SO_LINGER,
                reinterpret_cast<const char*>(&stLinger), sizeof(stLinger)) == 0, "enable reset");
        }
        closesocket(hClient);
        st.ExpectDisconnected();
        hClient = st.Connect();
        closesocket(hClient);
        st.ExpectDisconnected();
        std::puts(bReset ? "PASS: idle reset and reconnect" : "PASS: idle EOF and reconnect");
    }

    void TestFragmentedStop()
    {
        SServer st;
        SOCKET hClient = st.Connect();
        const uint8_t arrStop[]{ 'C', 'R', 'T', 'L', 3 };
        for (const uint8_t nByte : arrStop)
        {
            Send(hClient, &nByte, 1);
            std::this_thread::sleep_for(20ms);
        }
        st.ExpectDisconnected();
        closesocket(hClient);
        std::puts("PASS: fragmented stop without frames");
    }

    void TestHeartbeat(bool bReply)
    {
        SServer st;
        SOCKET hClient = st.Connect();
        const auto tStart = std::chrono::steady_clock::now();
        for (int nPing = 0; nPing < (bReply ? 2 : 1); ++nPing)
        {
            uint8_t arrPing[5]{};
            // Pings are sent every five seconds.
            const DWORD dwTimeout = 7000;
            Require(setsockopt(hClient, SOL_SOCKET, SO_RCVTIMEO,
                reinterpret_cast<const char*>(&dwTimeout), sizeof(dwTimeout)) == 0, "set ping timeout");
            Receive(hClient, arrPing, sizeof(arrPing));
            const uint8_t arrExpected[]{ 'C', 'R', 'T', 'L', 1 };
            Require(std::equal(std::begin(arrPing), std::end(arrPing), std::begin(arrExpected)),
                "ping packet shape");
            if (bReply)
            {
                arrPing[4] = 2;
                Send(hClient, arrPing, 2);
                std::this_thread::sleep_for(20ms);
                Send(hClient, arrPing + 2, 3);
            }
        }
        if (bReply)
        {
            std::this_thread::sleep_for(100ms);
            Require(st.Server.IsConnected(), "fragmented pongs keep idle connection alive");
        }
        else
        {
            Require(WaitUntil([&] { return !st.Server.IsConnected(); }, 6s), "heartbeat timeout");
            const auto tElapsed = std::chrono::steady_clock::now() - tStart;
            Require(tElapsed >= 9s && tElapsed < 11s, "heartbeat deadline is ten seconds");
            st.ExpectDisconnected();
        }
        closesocket(hClient);
        std::puts(bReply ? "PASS: fragmented heartbeat survives ten seconds" : "PASS: heartbeat timeout at ten seconds");
    }

    void TestShutdownHandshake()
    {
        SServer st;
        SOCKET hClient = st.Connect(false);
        const auto tStart = std::chrono::steady_clock::now();
        st.Server.Shutdown();
        Require(std::chrono::steady_clock::now() - tStart < 1s, "cancel handshake within one second");
        closesocket(hClient);
        std::puts("PASS: shutdown cancels handshake");
    }

    void TestBlockedSend(bool bShutdown)
    {
        SServer st;
        SOCKET hClient = st.Connect();
        g_bProduceFrames = true;
        Require(WaitUntil([] { return g_bEncoding.load(); }), "begin frame transmission");
        Require(WaitUntil([] {
            const int32_t nReleased = g_nReleased.load();
            std::this_thread::sleep_for(100ms);
            return g_bEncoding.load() && g_nReleased.load() == nReleased;
        }), "frame send is blocked by backpressure");
        const int32_t nReleasedBefore = g_nReleased.load();
        const auto tStart = std::chrono::steady_clock::now();
        if (bShutdown)
        {
            st.Server.Shutdown();
            Require(std::chrono::steady_clock::now() - tStart < 1s, "cancel blocked send within one second");
        }
        else
        {
            Require(WaitUntil([&] { return !st.Server.IsConnected(); }, 11s), "blocked send times out");
            const auto tElapsed = std::chrono::steady_clock::now() - tStart;
            Require(tElapsed >= 9s && tElapsed < 11s, "send timeout is ten seconds");
            st.ExpectDisconnected();
        }
        Require(g_nReleased == nReleasedBefore + 1, "failed transmission releases captured frame");
        closesocket(hClient);
        std::puts(bShutdown ? "PASS: shutdown cancels blocked send" : "PASS: blocked send timeout releases frame");
    }
}

// Link the real server to deterministic capture/encoder substitutes, without a GPU or FFmpeg.
CAgentCapture::CAgentCapture(ID3D11Device*, ID3D11DeviceContext*)
    : m_pDevice(nullptr), m_pDeviceContext(nullptr), m_pDuplication(nullptr),
      m_pStagingTex(nullptr), m_pCurrentFrame(nullptr), m_stMapped{},
      m_nTargetMonitor(0), m_nWidth(64), m_nHeight(64),
      m_bFrameAcquired(false), m_bMapped(false)
{
}
CAgentCapture::~CAgentCapture() = default;
bool CAgentCapture::AcquireFrameBGRA(SCaptureBGRA* pOut)
{
    static const uint8_t nPixel = 0;
    pOut->pData = &nPixel;
    pOut->Stride = 256;
    return g_bProduceFrames.load();
}
void CAgentCapture::ReleaseFrame() { ++g_nReleased; }

CAgentEncoder::CAgentEncoder()
    : m_pImpl(nullptr), m_nWidth(64), m_nHeight(64), m_nFps(30), m_nPts(0), m_bOpen(true)
{
}
CAgentEncoder::~CAgentEncoder() = default;
bool CAgentEncoder::EncodeFrame(const uint8_t*, int32_t, int64_t, const FnPacketCallback& fnCallback)
{
    static const std::vector<uint8_t> arrPacket(8 * 1024 * 1024, 0);
    g_bEncoding = true;
    fnCallback(arrPacket.data(), static_cast<int32_t>(arrPacket.size()));
    g_bEncoding = false;
    return true;
}

int main()
{
    WSADATA stWsa{};
    Require(WSAStartup(MAKEWORD(2, 2), &stWsa) == 0, "initialize test Winsock");
    TestIdleDisconnect(false);
    TestIdleDisconnect(true);
    TestFragmentedStop();
    TestHeartbeat(false);
    TestHeartbeat(true);
    TestShutdownHandshake();
    TestBlockedSend(false);
    TestBlockedSend(true);
    WSACleanup();
    std::puts("All agent disconnect tests passed.");
}
