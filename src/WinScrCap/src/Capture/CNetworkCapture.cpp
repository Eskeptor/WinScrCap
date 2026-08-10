/**
 * @file         CNetworkCapture.cpp
 * @author       eskeptor (yc.jeon)
 * @date         2026-08-10
 * @version      1.0.0
 */

extern "C"
{
    #include <libavcodec/avcodec.h>
    #include <libavutil/avutil.h>
    #include <libavutil/imgutils.h>
    #include <libswscale/swscale.h>
}

#include <winsock2.h>
#include <ws2tcpip.h>
#include <bcrypt.h>
#include <Windows.h>
// NT_SUCCESS는 ntstatus.h가 Windows.h와 충돌하므로 직접 정의
#ifndef NT_SUCCESS
    #define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif
#include <d3d11.h>
#include <cassert>
#include <cstring>
#include <chrono>

#include "CNetworkCapture.h"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "bcrypt.lib")

namespace
{
    constexpr uint32_t PROTO_MAGIC_HELLO = 0x52435357u;     // "WSCR"
    constexpr uint32_t PROTO_MAGIC_OK = 0x4F435357u;        // "WSCO"
    constexpr uint32_t PROTO_MAGIC_FAIL = 0x46435357u;      // "WSCF"
    constexpr uint32_t PROTO_MAGIC_FRAME = 0x4D415246u;     // "FRAM"
    constexpr uint32_t PROTO_MAGIC_CTRL = 0x4C545243u;      // "CTRL"
    constexpr uint8_t  PROTO_VERSION = 1;
    constexpr uint8_t  CTRL_PING = 0x01;
    constexpr uint8_t  CTRL_PONG = 0x02;
    constexpr uint8_t  CTRL_STOP = 0x03;
    constexpr int32_t  RECV_TIMEOUT_SEC = 15;
    constexpr int32_t  MAX_PACKET_SIZE = 10 * 1024 * 1024;  // 10 MB 상한
}

CNetworkCapture::CNetworkCapture(ID3D11Device* pDevice, ID3D11DeviceContext* pDeviceContext)
    : m_pDevice(pDevice)
    , m_pDeviceContext(pDeviceContext)
    , m_stTarget{}
    , m_hSocket(INVALID_SOCKET)
    , m_bCapturing(false)
    , m_bConnected(false)
    , m_pGpuTex(nullptr)
    , m_pStagingTex(nullptr)
    , m_bFrameReady(false)
    , m_nGpuTexWidth(0)
    , m_nGpuTexHeight(0)
    , m_nRemoteWidth(0)
    , m_nRemoteHeight(0)
    , m_nRemoteFps(0)
{
    assert(pDevice != nullptr);
    assert(pDeviceContext != nullptr);

    WSADATA stWsa{};
    int nResult = WSAStartup(MAKEWORD(2, 2), &stWsa);
    if (nResult != 0)
    {
        SetStatus("WSAStartup failed");
    }
}

CNetworkCapture::~CNetworkCapture()
{
    StopCapture();
    WSACleanup();
}

bool CNetworkCapture::Initialize()
{
    return true;
}

bool CNetworkCapture::InitializeRemote(const SRemoteTarget& stTarget)
{
    m_stTarget = stTarget;
    return true;
}

bool CNetworkCapture::StartCapture()
{
    if (m_bCapturing.load()) { return true; }

    m_bCapturing = true;
    SetStatus("Connecting...");
    m_thrReceive = std::thread(&CNetworkCapture::ThrReceive, this);
    return true;
}

void CNetworkCapture::StopCapture()
{
    m_bCapturing = false;
    if (m_hSocket != INVALID_SOCKET)
    {
        // CTRL_STOP 전송 후 소켓 닫기
        const uint32_t nMagic = PROTO_MAGIC_CTRL;
        const uint8_t nType = CTRL_STOP;
        send(m_hSocket, reinterpret_cast<const char*>(&nMagic), 4, 0);
        send(m_hSocket, reinterpret_cast<const char*>(&nType), 1, 0);
        closesocket(m_hSocket);
        m_hSocket = INVALID_SOCKET;
    }
    if (m_thrReceive.joinable())
    {
        m_thrReceive.join();
    }
    m_bConnected = false;
    SetStatus("Disconnected");

    std::lock_guard<std::mutex> lk(m_mtxFrame);
    if (m_pGpuTex != nullptr)
    {
        m_pGpuTex->Release();
        m_pGpuTex = nullptr;
    }
    if (m_pStagingTex != nullptr)
    {
        m_pStagingTex->Release();
        m_pStagingTex = nullptr;
    }
    m_bFrameReady = false;
    m_nGpuTexWidth = 0;
    m_nGpuTexHeight = 0;
}

bool CNetworkCapture::AcquireFrame(SFrameData* pOutFrame)
{
    assert(pOutFrame != nullptr);
    std::lock_guard<std::mutex> lk(m_mtxFrame);
    if (!m_bFrameReady ||
        m_pGpuTex == nullptr)
    {
        return false;
    }

    pOutFrame->pTexture = m_pGpuTex;
    pOutFrame->Width = m_nGpuTexWidth;
    pOutFrame->Height = m_nGpuTexHeight;
    pOutFrame->Timestamp = 0;
    m_bFrameReady = false;
    return true;
}

void CNetworkCapture::ReleaseFrame()
{
    // GPU 텍스처 소유권은 CNetworkCapture가 가짐 — 해제 불필요
}

bool CNetworkCapture::IsCapturing() const
{
    return m_bCapturing.load();
}

// ─── 수신 스레드 ────────────────────────────────────────────────────────────
void CNetworkCapture::ThrReceive()
{
    // FFmpeg 디코더 (H.264)
    const AVCodec* pCodec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (pCodec == nullptr)
    {
        SetStatus("H264 decoder not found");
        return;
    }

    AVCodecContext* pDecCtx = avcodec_alloc_context3(pCodec);
    if (pDecCtx == nullptr)
    {
        SetStatus("Decoder alloc failed");
        return;
    }
    pDecCtx->flags2 |= AV_CODEC_FLAG2_FAST;

    if (avcodec_open2(pDecCtx, pCodec, nullptr) < 0)
    {
        avcodec_free_context(&pDecCtx);
        SetStatus("Decoder open failed");
        return;
    }

    AVPacket* pPkt = av_packet_alloc();
    AVFrame* pFrm = av_frame_alloc();
    SwsContext* pSwsCtx = nullptr;
    AVPixelFormat eSwsSrcFmt = AV_PIX_FMT_NONE;
    int32_t nSwsW = 0;
    int32_t nSwsH = 0;

    std::vector<uint8_t> arrPayload;
    arrPayload.reserve(512 * 1024);

    while (m_bCapturing.load())
    {
        // 연결 시도 (재시도 루프)
        if (!m_bConnected.load())
        {
            if (!ConnectAndHandshake())
            {
                SetStatus("Connection failed, retrying...");
                Sleep(3000);
                continue;
            }
            m_bConnected = true;
            SetStatus("Connected");

            // 실제 디코딩된 프레임의 픽셀 포맷을 확인해야 하므로
            // SwsContext는 첫 프레임 수신 후 지연 생성한다.
            if (pSwsCtx != nullptr)
            {
                sws_freeContext(pSwsCtx);
                pSwsCtx = nullptr;
            }
            eSwsSrcFmt = AV_PIX_FMT_NONE;
            nSwsW = 0;
            nSwsH = 0;
        }

        // 수신 타임아웃 설정
        DWORD dwTimeout = static_cast<DWORD>(RECV_TIMEOUT_SEC) * 1000UL;
        setsockopt(m_hSocket, SOL_SOCKET, SO_RCVTIMEO,
            reinterpret_cast<const char*>(&dwTimeout), sizeof(dwTimeout));

        // 패킷 헤더 수신 (4B magic)
        uint32_t nMagic = 0;
        if (!RecvAll(m_hSocket, reinterpret_cast<uint8_t*>(&nMagic), 4))
        {
            Disconnect("Disconnected (recv error)");
            continue;
        }

        if (nMagic == PROTO_MAGIC_CTRL)
        {
            uint8_t nType = 0;
            RecvAll(m_hSocket, &nType, 1);
            if (nType == CTRL_PING)
            {
                // Pong 응답
                const uint32_t nPongMagic = PROTO_MAGIC_CTRL;
                const uint8_t nPong = CTRL_PONG;
                SendAll(m_hSocket, reinterpret_cast<const uint8_t*>(&nPongMagic), 4);
                SendAll(m_hSocket, &nPong, 1);
            }
            else if (nType == CTRL_STOP)
            {
                Disconnect("Disconnected (server stopped)");
            }
            continue;
        }

        if (nMagic != PROTO_MAGIC_FRAME)
        {
            // 예상치 못한 매직 값 — 스트림 오염으로 간주하고 재연결한다.
            // (TCP 스트림은 바이트 단위 재동기화가 신뢰할 수 없으므로 연결을 새로 맺는다)
            Disconnect("Stream corrupted, reconnecting...");
            continue;
        }

        // FRAM: size(4) + timestamp(8) + payload
        int32_t nSize = 0;
        int64_t nTs = 0;
        if (!RecvAll(m_hSocket, reinterpret_cast<uint8_t*>(&nSize), 4))
        {
            Disconnect("Disconnected (recv error)");
            continue;
        }
        if (!RecvAll(m_hSocket, reinterpret_cast<uint8_t*>(&nTs), 8))
        {
            Disconnect("Disconnected (recv error)");
            continue;
        }

        if (nSize <= 0 ||
            nSize > MAX_PACKET_SIZE)
        {
            Disconnect("Stream corrupted, reconnecting...");
            continue;
        }

        arrPayload.resize(static_cast<size_t>(nSize));
        if (!RecvAll(m_hSocket, arrPayload.data(), nSize))
        {
            Disconnect("Disconnected (recv error)");
            continue;
        }

        // FFmpeg 디코딩
        av_packet_unref(pPkt);
        pPkt->data = arrPayload.data();
        pPkt->size = nSize;

        if (avcodec_send_packet(pDecCtx, pPkt) < 0)
        {
            continue;
        }

        while (avcodec_receive_frame(pDecCtx, pFrm) == 0)
        {
            const int32_t nW = pFrm->width;
            const int32_t nH = pFrm->height;
            const AVPixelFormat eFrmFmt = static_cast<AVPixelFormat>(pFrm->format);

            // 디코더의 실제 출력 포맷/해상도가 바뀌면 SwsContext를 재생성한다.
            if (pSwsCtx == nullptr ||
                eFrmFmt != eSwsSrcFmt ||
                nW != nSwsW ||
                nH != nSwsH)
            {
                if (pSwsCtx != nullptr)
                {
                    sws_freeContext(pSwsCtx);
                    pSwsCtx = nullptr;
                }
                pSwsCtx = sws_getContext(
                    nW, nH, eFrmFmt,
                    nW, nH, AV_PIX_FMT_BGRA,
                    SWS_BILINEAR, nullptr, nullptr, nullptr);
                eSwsSrcFmt = eFrmFmt;
                nSwsW = nW;
                nSwsH = nH;
            }

            if (pSwsCtx == nullptr)
            {
                av_frame_unref(pFrm);
                continue;
            }

            // BGRA 임시 버퍼
            const int32_t nBGRAStride = nW * 4;
            std::vector<uint8_t> arrBGRA(static_cast<size_t>(nBGRAStride * nH));
            uint8_t* arrDst[1] = { arrBGRA.data() };
            const int32_t arrDstStr[1] = { nBGRAStride };

            sws_scale(pSwsCtx, pFrm->data, pFrm->linesize, 0, nH, arrDst, arrDstStr);

            av_frame_unref(pFrm);

            // D3D11 텍스처 업로드
            std::lock_guard<std::mutex> lk(m_mtxFrame);
            if (nW != m_nGpuTexWidth ||
                nH != m_nGpuTexHeight)
            {
                CreateGpuTexture(nW, nH);
            }
            if (UploadBGRAToTexture(arrBGRA.data(), nW, nH))
            {
                m_bFrameReady = true;
            }
        }
    }

    if (pSwsCtx != nullptr)
    {
        sws_freeContext(pSwsCtx);
    }
    av_frame_free(&pFrm);
    av_packet_free(&pPkt);
    avcodec_free_context(&pDecCtx);

    if (m_hSocket != INVALID_SOCKET)
    {
        closesocket(m_hSocket);
        m_hSocket = INVALID_SOCKET;
    }
    m_bConnected = false;
}

bool CNetworkCapture::ConnectAndHandshake()
{
    if (m_hSocket != INVALID_SOCKET)
    {
        closesocket(m_hSocket);
        m_hSocket = INVALID_SOCKET;
    }

    m_hSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_hSocket == INVALID_SOCKET)
    {
        return false;
    }

    // 연결 타임아웃 설정 (5초)
    DWORD dwTimeout = 5000UL;
    setsockopt(m_hSocket, SOL_SOCKET, SO_RCVTIMEO,
        reinterpret_cast<const char*>(&dwTimeout), sizeof(dwTimeout));
    setsockopt(m_hSocket, SOL_SOCKET, SO_SNDTIMEO,
        reinterpret_cast<const char*>(&dwTimeout), sizeof(dwTimeout));

    sockaddr_in stAddr{};
    stAddr.sin_family = AF_INET;
    stAddr.sin_port = htons(m_stTarget.Port);
    inet_pton(AF_INET, m_stTarget.szHost, &stAddr.sin_addr);

    if (connect(m_hSocket, reinterpret_cast<sockaddr*>(&stAddr), sizeof(stAddr)) != 0)
    {
        closesocket(m_hSocket);
        m_hSocket = INVALID_SOCKET;
        return false;
    }

    // 실패 시 소켓 정리 후 false 반환하는 헬퍼
    const auto fnFail = [this]() -> bool
    {
        closesocket(m_hSocket);
        m_hSocket = INVALID_SOCKET;
        return false;
    };

    // Server → Client: magic(4) + version(1) + nonce(16)
    uint32_t nMagic = 0;
    uint8_t nVer = 0;
    uint8_t arrNonce[16] = {};
    if (!RecvAll(m_hSocket, reinterpret_cast<uint8_t*>(&nMagic), 4))
    {
        return fnFail();
    }
    if (nMagic != PROTO_MAGIC_HELLO)
    {
        return fnFail();
    }
    if (!RecvAll(m_hSocket, &nVer, 1))
    {
        return fnFail();
    }
    if (!RecvAll(m_hSocket, arrNonce, 16))
    {
        return fnFail();
    }

    // Client → Server: auth_hash(32)
    {
        uint8_t arrHash[32] = {};
        ComputeAuthHash(m_stTarget.szPassword, arrNonce, arrHash);
        if (!SendAll(m_hSocket, arrHash, 32))
        {
            return fnFail();
        }
    }

    // Server → Client: ok(4) + width(4) + height(4) + fps(4)
    if (!RecvAll(m_hSocket, reinterpret_cast<uint8_t*>(&nMagic), 4))
    {
        return fnFail();
    }
    if (nMagic == PROTO_MAGIC_FAIL)
    {
        return fnFail();
    }
    if (nMagic != PROTO_MAGIC_OK)
    {
        return fnFail();
    }

    if (!RecvAll(m_hSocket, reinterpret_cast<uint8_t*>(&m_nRemoteWidth),  4))
    {
        return fnFail();
    }
    if (!RecvAll(m_hSocket, reinterpret_cast<uint8_t*>(&m_nRemoteHeight), 4))
    {
        return fnFail();
    }
    if (!RecvAll(m_hSocket, reinterpret_cast<uint8_t*>(&m_nRemoteFps),    4))
    {
        return fnFail();
    }
    return true;
}

bool CNetworkCapture::CreateGpuTexture(int32_t nWidth, int32_t nHeight)
{
    if (m_pGpuTex != nullptr)
    {
        m_pGpuTex->Release();
        m_pGpuTex = nullptr;
    }
    if (m_pStagingTex != nullptr)
    {
        m_pStagingTex->Release();
        m_pStagingTex = nullptr;
    }

    // GPU DEFAULT 텍스처 (SRV + CopyResource 소스)
    D3D11_TEXTURE2D_DESC stDefault{};
    {
        stDefault.Width = static_cast<UINT>(nWidth);
        stDefault.Height = static_cast<UINT>(nHeight);
        stDefault.MipLevels = 1;
        stDefault.ArraySize = 1;
        stDefault.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        stDefault.SampleDesc.Count = 1;
        stDefault.Usage = D3D11_USAGE_DEFAULT;
        stDefault.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    }
    if (FAILED(m_pDevice->CreateTexture2D(&stDefault, nullptr, &m_pGpuTex)))
    {
        return false;
    }

    // STAGING 텍스처 (CPU 쓰기용)
    D3D11_TEXTURE2D_DESC stStaging = stDefault;
    {
        stStaging.Usage = D3D11_USAGE_STAGING;
        stStaging.BindFlags = 0;
        stStaging.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    }
    if (FAILED(m_pDevice->CreateTexture2D(&stStaging, nullptr, &m_pStagingTex)))
    {
        m_pGpuTex->Release();
        m_pGpuTex = nullptr;
        return false;
    }

    m_nGpuTexWidth  = nWidth;
    m_nGpuTexHeight = nHeight;
    return true;
}

bool CNetworkCapture::UploadBGRAToTexture(const uint8_t* pBGRA, int32_t nWidth, int32_t nHeight)
{
    if (m_pStagingTex == nullptr ||
        m_pGpuTex == nullptr)
    {
        return false;
    }

    D3D11_MAPPED_SUBRESOURCE stMapped{};
    const HRESULT hr = m_pDeviceContext->Map(m_pStagingTex, 0, D3D11_MAP_WRITE, 0, &stMapped);
    if (FAILED(hr))
    {
        return false;
    }

    const uint8_t* pSrc = pBGRA;
    uint8_t* pDst = static_cast<uint8_t*>(stMapped.pData);
    const int32_t nSrcStride = nWidth * 4;
    const int32_t nDstStride = static_cast<int32_t>(stMapped.RowPitch);

    for (int32_t nRow = 0; nRow < nHeight; ++nRow)
    {
        memcpy(pDst + static_cast<ptrdiff_t>(nRow) * nDstStride,
            pSrc + static_cast<ptrdiff_t>(nRow) * nSrcStride,
            static_cast<size_t>(nSrcStride));
    }

    m_pDeviceContext->Unmap(m_pStagingTex, 0);
    m_pDeviceContext->CopyResource(m_pGpuTex, m_pStagingTex);
    return true;
}

void CNetworkCapture::Disconnect(const char* pszReason)
{
    SetStatus(pszReason);
    if (m_hSocket != INVALID_SOCKET)
    {
        closesocket(m_hSocket);
        m_hSocket = INVALID_SOCKET;
    }
    m_bConnected = false;
}

void CNetworkCapture::SetStatus(const char* pszMsg)
{
    std::lock_guard<std::mutex> lk(m_mtxStatus);
    m_strStatus = (pszMsg != nullptr) ? pszMsg : "";
}

bool CNetworkCapture::ComputeAuthHash(const char* pszPassword, const uint8_t pNonce[16], uint8_t pOutHash[32])
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

bool CNetworkCapture::SendAll(SOCKET hSock, const uint8_t* pData, int32_t nLen)
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

bool CNetworkCapture::RecvAll(SOCKET hSock, uint8_t* pData, int32_t nLen)
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
