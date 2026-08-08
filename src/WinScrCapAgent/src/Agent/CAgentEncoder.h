/**
 * @file         CAgentEncoder.h
 * @author       eskeptor (yc.jeon)
 * @date         2026-08-08
 * @version      1.0.0
 */

#pragma once

#include <cstdint>
#include <vector>
#include <functional>

// H.264 패킷 콜백: 외부에서 pData/nSize를 TCP로 전송
using FnPacketCallback = std::function<void(const uint8_t* pData, int32_t nSize)>;

class CAgentEncoder
{
public:
    CAgentEncoder();
    ~CAgentEncoder();

    /**
     * @detail      FFmpeg 라이브러리를 사용하여 H.264 인코더를 초기화한다.
     * @param[in]   nWidth 프레임 너비
     * @param[in]   nHeight 프레임 높이
     * @param[in]   nFps 프레임 속도
     * @param[in]   nCrf CRF 값
     * @return      true 초기화 성공, false 초기화 실패
     */
    bool Open(int32_t nWidth, int32_t nHeight, int32_t nFps, int32_t nCrf);
    void Close();

    bool IsOpen() const
    {
        return m_bOpen;
    }
    int32_t GetWidth() const
    {
        return m_nWidth;
    }
    int32_t GetHeight() const
    {
        return m_nHeight;
    }

    /**
     * @brief       BGRA CPU 데이터를 인코딩하고 H.264 패킷을 콜백으로 전달한다.
     * @param[in]   pBGRA BGRA CPU 데이터 포인터
     * @param[in]   nStride BGRA CPU 데이터의 한 줄 바이트 수 (보통 Width * 4)
     * @param[in]   nTimestampUs 프레임 타임스탬프 (마이크로초 단위)
     * @param[in]   fnCallback H.264 패킷 콜백 함수
     * @return      true 인코딩 성공, false 인코딩 실패
     */
    bool EncodeFrame(
        const uint8_t* pBGRA,
        int32_t nStride,
        int64_t nTimestampUs,
        const FnPacketCallback& fnCallback);

private:
    bool TryOpenCodec(const char* pszName, int32_t nWidth, int32_t nHeight,
        int32_t nFps, int32_t nCrf);

    struct SImpl;
    SImpl* m_pImpl;

    int32_t m_nWidth;
    int32_t m_nHeight;
    int32_t m_nFps;
    int64_t m_nPts;
    bool m_bOpen;
};
