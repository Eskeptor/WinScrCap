/**
 * @file         ICapture.h
 * @author       eskeptor (yc.jeon)
 * @date         2026-07-23
 * @version      1.0.0
 * @brief        캡처 소스 인터페이스 정의
 */

#pragma once

#include "Common/Types.h"

struct SFrameData; // Types.h에서 정의

/**
 * @class    ICapture
 * @brief    캡처 소스 추상 인터페이스
 * @details  모니터 캡처(CScreenCapture) 및 창 캡처(CWindowCapture)가 공통으로
 *           구현해야 하는 순수 가상 인터페이스이다.
 */
class ICapture
{
public:
    virtual ~ICapture() = default;

    /**
     * @brief       캡처 소스를 초기화한다.
     * @return      초기화 성공 여부
     */
    virtual bool Initialize()  = 0;

    /**
     * @brief       캡처를 시작한다.
     * @return      캡처 시작 성공 여부
     */
    virtual bool StartCapture() = 0;

    /**
     * @brief       캡처를 중지한다.
     */
    virtual void StopCapture() = 0;

    /**
     * @brief       다음 프레임을 획득한다.
     * @param[out]  pOutFrame       획득된 프레임 정보를 저장할 SFrameData 포인터
     * @return      프레임 획득 성공 여부 (새 프레임이 없으면 false)
     */
    virtual bool AcquireFrame(SFrameData* pOutFrame) = 0;

    /**
     * @brief       현재 보유 중인 프레임을 해제한다.
     * @details     AcquireFrame() 성공 후 반드시 호출해야 내부 버퍼가 정상 반환된다.
     */
    virtual void ReleaseFrame() = 0;

    /**
     * @brief       현재 캡처 중인지 여부를 반환한다.
     * @return      캡처 진행 중이면 true
     */
    virtual bool IsCapturing() const = 0;
};
