/**
 * @file         CVideoEncoder.h
 * @author       eskeptor (yc.jeon)
 * @date         2026-07-23
 * @version      1.0.0
 * @brief        FFmpeg 기반 H.264/MP4 인코더 클래스 정의
 * @details      - NVENC, AMF, QSV, h264_mf 등 다양한 인코더 지원
 *               - D3D11 BGRA → 스테이징 버퍼 → NV12 → 인코더 경로
 *               - third_party\ffmpeg\include\  (헤더)
 *               - third_party\ffmpeg\lib\      (.lib 파일)
 *               - third_party\ffmpeg\bin\      (.dll 파일, 실행 시 필요)
 */

#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <memory>
#include <cstdint>

#include "Common/Types.h"

/**
 * @class    CVideoEncoder
 * @brief    FFmpeg 기반 H.264/MP4 인코더 클래스
 * @details  - NVENC → AMF → QSV → h264_mf 순서로 인코더를 자동 선택한다.
 *           - D3D11 BGRA 텍스체를 스테이징 버퍼를 통해 NV12 로 변환 후 인코딩한다.
 */
class CVideoEncoder
{
    struct SImpl;

public:
    CVideoEncoder(ID3D11Device* pDevice, ID3D11DeviceContext* pDeviceContext);
    ~CVideoEncoder();

    /**
     * @brief       인코더를 열고 MP4 출력 파일을 준비한다.
     * @details     HW 인코더 (NVENC/AMF/QSV) 초기화를 시도하고, 실패 시 h264_mf 로 폴백한다.
     * @param[in]   stConfig        인코더 및 출력 파일 설정
     * @return      초기화 성공 여부
     */
    bool Open(const SEncodeConfig& stConfig);

    /**
     * @brief       D3D11 텍스처를 인코더에 제출한다.
     * @details     텍스처를 스테이징 버퍼로 복사한 뒤 NV12 변환 → FFmpeg AVFrame 제출 순으로 처리한다.
     * @param[in]   pTexture        인코딩할 D3D11 텍스처 포인터
     * @return      프레임 제출 성공 여부
     */
    bool SubmitFrame(ID3D11Texture2D* pTexture);

    /**
     * @brief       인코더를 닫고 MP4 트레일러를 기록한 뒤 모든 리소스를 해제한다.
     */
    void Close();

    /**
     * @brief       인코더가 열려 있는지(MP4 파일 기록 중) 여부를 반환한다.
     * @return      열려 있으면 true
     */
    bool IsOpen() const
    {
        return m_bOpen;
    }

    /**
     * @brief       현재 활성화된 인코더 유형을 반환한다.
     * @return      eEncoderType 열거값
     */
    eEncoderType GetActiveEncoder() const
    {
        return m_eActiveEncoder;
    }

    /**
     * @brief       현재까지 인코딩된 수 프레임 수를 반환한다.
     * @return      프레임 수
     */
    int64_t GetFrameCount() const
    {
        return m_nFrameCount;
    }

private:
    /**
     * @brief       지정된 코덱 이름으로 FFmpeg 인코더 초기화를 시도한다.
     * @param[in]   pszCodecName    FFmpeg 코덱 이름 (예: "h264_nvenc")
     * @param[in]   stConfig        인코더 설정
     * @return      초기화 성공 여부
     */
    bool TryOpenEncoder(const char* pszCodecName, const SEncodeConfig& stConfig);

    /**
     * @brief       CPU 매핑용 스테이징 텍스처를 생성한다.
     * @param[in]   nWidth          텍스처 너비 (픽셀)
     * @param[in]   nHeight         텍스처 높이 (픽셀)
     * @return      생성 성공 여부
     */
    bool CreateStagingTexture(int32_t nWidth, int32_t nHeight);

    /**
     * @brief       변환된 AVFrame 을 FFmpeg 인코더에 전송한다.
     * @param[in]   pFrame          전송할 AVFrame 포인터
     * @return      전송 성공 여부
     */
    bool SendAvFrame(struct AVFrame* pFrame);

private:
    std::unique_ptr<SImpl> m_stImpl;

    ID3D11Device* m_pDevice;
    ID3D11DeviceContext* m_pDeviceContext;
    ID3D11Texture2D* m_pStagingTex;

    int32_t m_nStagingWidth;
    int32_t m_nStagingHeight;
    int64_t m_nFrameCount;
    bool m_bOpen;
    eEncoderType m_eActiveEncoder;
};
