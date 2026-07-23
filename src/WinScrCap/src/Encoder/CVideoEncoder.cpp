/**
 * @file         CVideoEncoder.cpp
 * @author       eskeptor (yc.jeon)
 * @date         2026-07-23
 * @version      1.0.0
 */

extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include <Windows.h>
#include <d3d11.h>
#include <cassert>
#include <string>

#include "CVideoEncoder.h"

struct CVideoEncoder::SImpl
{
    AVFormatContext* pFormatCtx = nullptr;
    AVCodecContext* pCodecCtx  = nullptr;
    AVStream* pStream = nullptr;
    AVFrame* pFrame = nullptr;
    AVPacket* pPacket = nullptr;
    SwsContext* pSwsCtx = nullptr;
};

namespace
{
    std::string WStrToNarrow(const std::wstring& strWide)
    {
        if (strWide.empty())
        {
            return {};
        }
        const int32_t nLen = WideCharToMultiByte(
            CP_UTF8, 0, strWide.c_str(), -1, nullptr, 0, nullptr, nullptr
        );
        std::string strResult(static_cast<size_t>(nLen), '\0');
        WideCharToMultiByte(
            CP_UTF8, 0, strWide.c_str(), -1, strResult.data(), nLen, nullptr, nullptr
        );
        if (!strResult.empty() &&
            strResult.back() == '\0')
        {
            strResult.pop_back();
        }
        return strResult;
    }

    // 코덱 이름 → eEncoderType 변환
    eEncoderType CodecNameToEncoderType(const char* pszName)
    {
        if (strcmp(pszName, "h264_nvenc") == 0)
        {
            return eEncoderType::NVENC;
        }
        if (strcmp(pszName, "h264_amf") == 0)
        {
            return eEncoderType::AMF;
        }
        if (strcmp(pszName, "h264_qsv") == 0)
        {
            return eEncoderType::QSV;
        }
        return eEncoderType::MediaFoundation;
    }
}

CVideoEncoder::CVideoEncoder(ID3D11Device* pDevice, ID3D11DeviceContext* pDeviceContext)
    : m_stImpl(std::make_unique<SImpl>())
    , m_pDevice(pDevice)
    , m_pDeviceContext(pDeviceContext)
    , m_pStagingTex(nullptr)
    , m_nStagingWidth(0)
    , m_nStagingHeight(0)
    , m_nFrameCount(0)
    , m_bOpen(false)
    , m_eActiveEncoder(eEncoderType::MediaFoundation)
{
    assert(pDevice != nullptr);
    assert(pDeviceContext != nullptr);
}

CVideoEncoder::~CVideoEncoder()
{
    Close();
}

bool CVideoEncoder::Open(const SEncodeConfig& stConfig)
{
    Close();

    const std::string strPath = WStrToNarrow(stConfig.OutputPath);
    if (strPath.empty())
    {
        return false;
    }

    // 출력 포맷 컨텍스트 생성 (MP4)
    avformat_alloc_output_context2(&m_stImpl->pFormatCtx, nullptr, nullptr, strPath.c_str());
    if (m_stImpl->pFormatCtx == nullptr)
    {
        return false;
    }

    // 인코더 자동 선택: NVENC → AMF → QSV → h264_mf
    static constexpr const char* ARR_CODEC_NAMES[] =
    {
        "h264_nvenc",
        "h264_amf",
        "h264_qsv",
        "h264_mf",
    };

    bool bEncoderFound = false;
    for (const char* pszName : ARR_CODEC_NAMES)
    {
        if (TryOpenEncoder(pszName, stConfig))
        {
            m_eActiveEncoder = CodecNameToEncoderType(pszName);
            bEncoderFound = true;
            break;
        }
    }

    if (!bEncoderFound)
    {
        avformat_free_context(m_stImpl->pFormatCtx);
        m_stImpl->pFormatCtx = nullptr;
        return false;
    }

    // 비디오 스트림 생성
    m_stImpl->pStream = avformat_new_stream(m_stImpl->pFormatCtx, nullptr);
    if (m_stImpl->pStream == nullptr)
    {
        Close();
        return false;
    }
    m_stImpl->pStream->id = 0;
    m_stImpl->pStream->time_base = m_stImpl->pCodecCtx->time_base;
    avcodec_parameters_from_context(m_stImpl->pStream->codecpar, m_stImpl->pCodecCtx);

    // AVFrame 할당 (NV12)
    m_stImpl->pFrame = av_frame_alloc();
    if (m_stImpl->pFrame == nullptr)
    {
        Close();
        return false;
    }
    m_stImpl->pFrame->format = AV_PIX_FMT_NV12;
    m_stImpl->pFrame->width = stConfig.Width;
    m_stImpl->pFrame->height = stConfig.Height;
    if (av_frame_get_buffer(m_stImpl->pFrame, 0) < 0)
    {
        Close();
        return false;
    }

    // AVPacket 할당
    m_stImpl->pPacket = av_packet_alloc();
    if (m_stImpl->pPacket == nullptr)
    {
        Close();
        return false;
    }

    // 출력 파일 열기
    if (!(m_stImpl->pFormatCtx->oformat->flags & AVFMT_NOFILE))
    {
        if (avio_open(&m_stImpl->pFormatCtx->pb, strPath.c_str(), AVIO_FLAG_WRITE) < 0)
        {
            Close();
            return false;
        }
    }

    // MP4 헤더 기록
    if (avformat_write_header(m_stImpl->pFormatCtx, nullptr) < 0)
    {
        Close();
        return false;
    }

    m_nFrameCount = 0;
    m_bOpen = true;
    return true;
}

bool CVideoEncoder::SubmitFrame(ID3D11Texture2D* pTexture)
{
    if (!m_bOpen ||
        pTexture == nullptr)
    {
        return false;
    }

    // 텍스처 크기 확인
    D3D11_TEXTURE2D_DESC stDesc{};
    pTexture->GetDesc(&stDesc);
    const int32_t nWidth = static_cast<int32_t>(stDesc.Width);
    const int32_t nHeight = static_cast<int32_t>(stDesc.Height);

    // 스테이징 텍스처 준비 (크기 변경 시 재생성)
    if (nWidth != m_nStagingWidth ||
        nHeight != m_nStagingHeight)
    {
        if (!CreateStagingTexture(nWidth, nHeight))
        {
            return false;
        }
    }

    // GPU → 스테이징 텍스처 복사
    m_pDeviceContext->CopyResource(m_pStagingTex, pTexture);

    // 스테이징 텍스처 CPU 매핑
    D3D11_MAPPED_SUBRESOURCE stMapped{};
    const HRESULT hr = m_pDeviceContext->Map(m_pStagingTex, 0, D3D11_MAP_READ, 0, &stMapped);
    if (FAILED(hr))
    {
        return false;
    }

    // BGRA → NV12 변환 (swscale)
    if (av_frame_make_writable(m_stImpl->pFrame) < 0)
    {
        m_pDeviceContext->Unmap(m_pStagingTex, 0);
        return false;
    }

    const uint8_t* arrSrcData[1] = { static_cast<const uint8_t*>(stMapped.pData) };
    const int32_t arrSrcStride[1] = { static_cast<int32_t>(stMapped.RowPitch) };

    sws_scale(
        m_stImpl->pSwsCtx,
        arrSrcData, arrSrcStride,
        0, nHeight,
        m_stImpl->pFrame->data, m_stImpl->pFrame->linesize);

    m_pDeviceContext->Unmap(m_pStagingTex, 0);

    // PTS 설정
    m_stImpl->pFrame->pts = m_nFrameCount;

    return SendAvFrame(m_stImpl->pFrame);
}

void CVideoEncoder::Close()
{
    if (!m_bOpen)
    {
        // 미초기화 상태여도 할당된 리소스 정리
    }

    // 인코더 플러시 (남은 프레임 출력)
    if (m_stImpl->pCodecCtx != nullptr &&
        m_bOpen)
    {
        SendAvFrame(nullptr);
    }

    // MP4 트레일러 기록
    if (m_stImpl->pFormatCtx != nullptr &&
        m_bOpen)
    {
        av_write_trailer(m_stImpl->pFormatCtx);
    }

    // FFmpeg 리소스 해제 순서
    if (m_stImpl->pSwsCtx != nullptr)
    {
        sws_freeContext(m_stImpl->pSwsCtx);
        m_stImpl->pSwsCtx = nullptr;
    }
    if (m_stImpl->pFrame != nullptr)
    {
        av_frame_free(&m_stImpl->pFrame);
    }
    if (m_stImpl->pPacket != nullptr)
    {
        av_packet_free(&m_stImpl->pPacket);
    }
    if (m_stImpl->pCodecCtx != nullptr)
    {
        avcodec_free_context(&m_stImpl->pCodecCtx);
    }
    if (m_stImpl->pFormatCtx != nullptr)
    {
        if (!(m_stImpl->pFormatCtx->oformat->flags & AVFMT_NOFILE))
        {
            avio_closep(&m_stImpl->pFormatCtx->pb);
        }
        avformat_free_context(m_stImpl->pFormatCtx);
        m_stImpl->pFormatCtx = nullptr;
    }

    // D3D11 스테이징 텍스처 해제
    if (m_pStagingTex != nullptr)
    {
        m_pStagingTex->Release();
        m_pStagingTex = nullptr;
    }

    m_nStagingWidth  = 0;
    m_nStagingHeight = 0;
    m_nFrameCount = 0;
    m_bOpen = false;
}

bool CVideoEncoder::TryOpenEncoder(const char* pszCodecName, const SEncodeConfig& stConfig)
{
    const AVCodec* pCodec = avcodec_find_encoder_by_name(pszCodecName);
    if (pCodec == nullptr)
    {
        return false;
    }

    AVCodecContext* pCtx = avcodec_alloc_context3(pCodec);
    if (pCtx == nullptr)
    {
        return false;
    }

    pCtx->width = stConfig.Width;
    pCtx->height = stConfig.Height;
    pCtx->time_base = AVRational{ 1, stConfig.FrameRate };
    pCtx->framerate = AVRational{ stConfig.FrameRate, 1 };
    pCtx->bit_rate = static_cast<int64_t>(stConfig.BitRate);
    pCtx->pix_fmt = AV_PIX_FMT_NV12;
    pCtx->gop_size = stConfig.FrameRate;  // 1초마다 키프레임
    pCtx->max_b_frames = 0;

    if (m_stImpl->pFormatCtx->oformat->flags & AVFMT_GLOBALHEADER)
    {
        pCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    // h264_nvenc 전용 품질 옵션
    if (strcmp(pszCodecName, "h264_nvenc") == 0)
    {
        av_opt_set(pCtx->priv_data, "preset", "p4", 0);
        av_opt_set(pCtx->priv_data, "tune",   "hq", 0);
    }

    if (avcodec_open2(pCtx, pCodec, nullptr) < 0)
    {
        avcodec_free_context(&pCtx);
        return false;
    }

    m_stImpl->pCodecCtx = pCtx;

    // SwsContext 생성 (BGRA → NV12)
    m_stImpl->pSwsCtx = sws_getContext(
        stConfig.Width, stConfig.Height, AV_PIX_FMT_BGRA,
        stConfig.Width, stConfig.Height, AV_PIX_FMT_NV12,
        SWS_BILINEAR, nullptr, nullptr, nullptr);

    return m_stImpl->pSwsCtx != nullptr;
}

bool CVideoEncoder::CreateStagingTexture(int32_t nWidth, int32_t nHeight)
{
    if (m_pStagingTex != nullptr)
    {
        m_pStagingTex->Release();
        m_pStagingTex = nullptr;
    }

    D3D11_TEXTURE2D_DESC stDesc{};
    {
        stDesc.Width = static_cast<UINT>(nWidth);
        stDesc.Height = static_cast<UINT>(nHeight);
        stDesc.MipLevels = 1;
        stDesc.ArraySize = 1;
        stDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        stDesc.SampleDesc.Count = 1;
        stDesc.SampleDesc.Quality = 0;
        stDesc.Usage = D3D11_USAGE_STAGING;
        stDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    }

    const HRESULT hr = m_pDevice->CreateTexture2D(&stDesc, nullptr, &m_pStagingTex);
    if (FAILED(hr))
    {
        return false;
    }

    m_nStagingWidth  = nWidth;
    m_nStagingHeight = nHeight;
    return true;
}

bool CVideoEncoder::SendAvFrame(AVFrame* pFrame)
{
    int32_t nRet = avcodec_send_frame(m_stImpl->pCodecCtx, pFrame);
    if (nRet < 0 &&
        nRet != AVERROR_EOF)
    {
        return false;
    }

    while (true)
    {
        nRet = avcodec_receive_packet(m_stImpl->pCodecCtx, m_stImpl->pPacket);
        if (nRet == AVERROR(EAGAIN) ||
            nRet == AVERROR_EOF)
        {
            break;
        }
        if (nRet < 0)
        {
            return false;
        }

        av_packet_rescale_ts(
            m_stImpl->pPacket,
            m_stImpl->pCodecCtx->time_base,
            m_stImpl->pStream->time_base);
        m_stImpl->pPacket->stream_index = m_stImpl->pStream->index;

        av_interleaved_write_frame(m_stImpl->pFormatCtx, m_stImpl->pPacket);
        av_packet_unref(m_stImpl->pPacket);
    }

    if (pFrame != nullptr)
    {
        ++m_nFrameCount;
    }

    return true;
}
