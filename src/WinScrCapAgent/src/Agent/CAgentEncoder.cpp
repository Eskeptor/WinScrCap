extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include <cassert>
#include <cstring>

#include "CAgentEncoder.h"

struct CAgentEncoder::SImpl
{
    AVCodecContext* pCodecCtx = nullptr;
    AVFrame*        pFrame    = nullptr;
    AVPacket*       pPacket   = nullptr;
    SwsContext*     pSwsCtx   = nullptr;
};

CAgentEncoder::CAgentEncoder()
    : m_pImpl(new SImpl())
    , m_nWidth(0)
    , m_nHeight(0)
    , m_nFps(30)
    , m_nPts(0)
    , m_bOpen(false)
{
}

CAgentEncoder::~CAgentEncoder()
{
    Close();
    delete m_pImpl;
}

bool CAgentEncoder::Open(int32_t nWidth, int32_t nHeight, int32_t nFps, int32_t nCrf)
{
    Close();

    // 저지연 스트리밍용 코덱 우선순위: HW → h264_mf 폴백
    static constexpr const char* ARR_CODECS[] =
    {
        "h264_nvenc",
        "h264_amf",
        "h264_qsv",
        "h264_mf",
    };

    bool bFound = false;
    for (const char* pszName : ARR_CODECS)
    {
        if (TryOpenCodec(pszName, nWidth, nHeight, nFps, nCrf))
        {
            bFound = true;
            break;
        }
    }

    if (!bFound) { return false; }

    m_pImpl->pFrame = av_frame_alloc();
    if (m_pImpl->pFrame == nullptr) { Close(); return false; }
    m_pImpl->pFrame->format = AV_PIX_FMT_NV12;
    m_pImpl->pFrame->width  = nWidth;
    m_pImpl->pFrame->height = nHeight;
    if (av_frame_get_buffer(m_pImpl->pFrame, 0) < 0) { Close(); return false; }

    m_pImpl->pPacket = av_packet_alloc();
    if (m_pImpl->pPacket == nullptr) { Close(); return false; }

    // BGRA → NV12 변환 컨텍스트
    m_pImpl->pSwsCtx = sws_getContext(
        nWidth, nHeight, AV_PIX_FMT_BGRA,
        nWidth, nHeight, AV_PIX_FMT_NV12,
        SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (m_pImpl->pSwsCtx == nullptr) { Close(); return false; }

    m_nWidth  = nWidth;
    m_nHeight = nHeight;
    m_nFps    = nFps;
    m_nPts    = 0;
    m_bOpen   = true;
    return true;
}

void CAgentEncoder::Close()
{
    if (m_pImpl->pSwsCtx != nullptr)
    {
        sws_freeContext(m_pImpl->pSwsCtx);
        m_pImpl->pSwsCtx = nullptr;
    }
    if (m_pImpl->pFrame != nullptr)
    {
        av_frame_free(&m_pImpl->pFrame);
    }
    if (m_pImpl->pPacket != nullptr)
    {
        av_packet_free(&m_pImpl->pPacket);
    }
    if (m_pImpl->pCodecCtx != nullptr)
    {
        avcodec_free_context(&m_pImpl->pCodecCtx);
    }
    m_nWidth  = 0;
    m_nHeight = 0;
    m_nPts    = 0;
    m_bOpen   = false;
}

bool CAgentEncoder::EncodeFrame(
    const uint8_t* pBGRA,
    int32_t nStride,
    int64_t nTimestampUs,
    const FnPacketCallback& fnCallback)
{
    if (!m_bOpen || pBGRA == nullptr) { return false; }

    if (av_frame_make_writable(m_pImpl->pFrame) < 0) { return false; }

    const uint8_t* arrSrc[1]    = { pBGRA };
    const int32_t  arrStride[1] = { nStride };
    sws_scale(m_pImpl->pSwsCtx,
        arrSrc, arrStride, 0, m_nHeight,
        m_pImpl->pFrame->data, m_pImpl->pFrame->linesize);

    m_pImpl->pFrame->pts = m_nPts;
    ++m_nPts;

    int32_t nRet = avcodec_send_frame(m_pImpl->pCodecCtx, m_pImpl->pFrame);
    if (nRet < 0 && nRet != AVERROR_EOF) { return false; }

    while (true)
    {
        nRet = avcodec_receive_packet(m_pImpl->pCodecCtx, m_pImpl->pPacket);
        if (nRet == AVERROR(EAGAIN) || nRet == AVERROR_EOF) { break; }
        if (nRet < 0) { return false; }

        if (fnCallback && m_pImpl->pPacket->size > 0)
        {
            fnCallback(m_pImpl->pPacket->data, m_pImpl->pPacket->size);
        }
        av_packet_unref(m_pImpl->pPacket);
    }

    return true;
}

bool CAgentEncoder::TryOpenCodec(const char* pszName, int32_t nWidth, int32_t nHeight,
                                  int32_t nFps, int32_t nCrf)
{
    const AVCodec* pCodec = avcodec_find_encoder_by_name(pszName);
    if (pCodec == nullptr) { return false; }

    AVCodecContext* pCtx = avcodec_alloc_context3(pCodec);
    if (pCtx == nullptr) { return false; }

    pCtx->width      = nWidth;
    pCtx->height     = nHeight;
    pCtx->time_base  = AVRational{ 1, nFps };
    pCtx->framerate  = AVRational{ nFps, 1 };
    pCtx->pix_fmt    = AV_PIX_FMT_NV12;
    pCtx->gop_size   = nFps;
    pCtx->max_b_frames = 0;

    // 저지연 스트리밍 옵션
    if (strcmp(pszName, "h264_nvenc") == 0)
    {
        av_opt_set(pCtx->priv_data, "preset", "p1",          0);
        av_opt_set(pCtx->priv_data, "tune",   "ull",         0);
        av_opt_set(pCtx->priv_data, "rc",     "constqp",     0);
        av_opt_set_int(pCtx->priv_data, "qp", nCrf,          0);
        av_opt_set_int(pCtx->priv_data, "zerolatency", 1,    0);
    }
    else if (strcmp(pszName, "h264_amf") == 0)
    {
        av_opt_set(pCtx->priv_data, "quality", "speed",      0);
        av_opt_set_int(pCtx->priv_data, "qp_i", nCrf,        0);
        av_opt_set_int(pCtx->priv_data, "qp_p", nCrf,        0);
    }
    else if (strcmp(pszName, "h264_mf") == 0)
    {
        // h264_mf: rate_control=vbr, low_latency
        av_opt_set_int(pCtx->priv_data, "low_latency", 1,    0);
        pCtx->bit_rate = static_cast<int64_t>(nWidth) * nHeight * nFps / 10;
    }

    if (avcodec_open2(pCtx, pCodec, nullptr) < 0)
    {
        avcodec_free_context(&pCtx);
        return false;
    }

    m_pImpl->pCodecCtx = pCtx;
    return true;
}
