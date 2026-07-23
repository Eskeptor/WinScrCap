# Plan: WinScrCap - C++ Windows Screen Recorder

## TL;DR
DirectX 11 기반 ImGui UI + DXGI Desktop Duplication(모니터) + Windows Graphics Capture API(윈도우) + FFmpeg HW 인코딩(NVENC/AMF/QSV → libx264 폴백) 구조의 화면 녹화 프로그램. GPU 제로카피 파이프라인으로 시스템 리소스 최소화.

---

## 기술 스택 확정
- **언어**: C++20, MSVC
- **UI**: ImGui + DirectX 11 backend
- **화면 캡처**: DXGI Desktop Duplication (모니터) + WGC API (특정 윈도우)
- **인코딩**: FFmpeg LGPL 빌드 (h264_nvenc / h264_amf / h264_qsv → h264_mf 폴백)
- **출력**: MP4 (H.264)
- **최소 Windows**: Windows 10 (1803+)

---

## 아키텍처

### GPU 제로카피 파이프라인
```
DXGI Duplication → ID3D11Texture2D (VRAM) → FFmpeg D3D11VA Context → NVENC (GPU 인코딩)
                                                          ↓ 폴백
                                             Staging Buffer → CPU → h264_mf (Windows MF)
```
- CPU 복사 없이 GPU 메모리에서 직접 인코딩
- 폴백: GPU Texture → Staging Buffer → CPU → `h264_mf` (Windows Media Foundation 내장 H.264 인코더)

### 스레딩 모델
- **메인 스레드**: DX11 렌더링 + ImGui UI
- **캡처 스레드**: DXGI/WGC 프레임 획득 (높은 우선순위)
- **인코딩 스레드**: FFmpeg 인코딩 + MP4 먹싱

---

## 프로젝트 구조
```
WinScrCap/
├── WinScrCap.sln
└── WinScrCap/
    ├── WinScrCap.vcxproj
    ├── src/
    │   ├── main.cpp
    │   ├── Application/
    │   │   ├── CApplication.h/.cpp    ← DX11 장치 + 메인 루프
    │   ├── Capture/
    │   │   ├── ICapture.h             ← 순수 가상 인터페이스
    │   │   ├── CScreenCapture.h/.cpp  ← DXGI Desktop Duplication
    │   │   ├── CWindowCapture.h/.cpp  ← WGC API
    │   │   ├── CCaptureManager.h/.cpp ← 캡처 조율 + 프레임 큐
    │   ├── Encoder/
    │   │   ├── CVideoEncoder.h/.cpp   ← FFmpeg HW/SW 인코더
    │   ├── UI/
    │   │   ├── CMainWindow.h/.cpp     ← ImGui 메인 창
    │   └── Common/
    │       └── Types.h               ← SFrameData, eXxx 정의
    ├── third_party/
    │   ├── imgui/                    ← ImGui 소스 직접 포함
    │   └── ffmpeg/                   ← 사전 빌드 바이너리 (include/lib/bin)
    └── res/
        └── (아이콘 등)
```

---

## 핵심 클래스 설계 (C++ 코딩 지침 준수)

### Common/Types.h
```
struct SFrameData      ← ID3D11Texture2D 포인터, 타임스탬프, 너비/높이
enum class eCaptureType  ← Monitor, Window
enum class eEncoderType  ← NVENC, AMF, QSV, Software
enum class eRecordState  ← Idle, Recording, Paused, Stopping
```

### ICapture (인터페이스)
```
pure virtual: Initialize(), StartCapture(), StopCapture(), AcquireFrame(), ReleaseFrame()
```

### CScreenCapture : ICapture
- `IDXGIOutputDuplication` 래핑
- 멀티 모니터 지원 (`IDXGIFactory → IDXGIAdapter → IDXGIOutput` 열거)
- `AcquireNextFrame()` → `SFrameData` 반환

### CWindowCapture : ICapture
- Windows.Graphics.Capture (WinRT COM)
- `GraphicsCaptureItem` 생성 (HWND 기반)
- `Direct3D11CaptureFramePool` 사용

### CCaptureManager
- `ICapture*` 인터페이스를 통해 소스 전환 (모니터 ↔ 창)
- 캡처 스레드 관리 (`std::thread`)
- 프레임 큐 (`std::queue` + `std::mutex`) 또는 lock-free 링 버퍼
- FPS 제어 (`std::chrono` 기반 타이밍)

### CVideoEncoder
- `AVCodecContext` 래핑 (FFmpeg)
- `InitHardwareEncoder()`: nvenc → amf → qsv → h264_mf 순서로 시도
- D3D11VA 컨텍스트 설정 (`av_hwdevice_ctx_create`)
- `EncodeFrame(SFrameData&)`: `AV_PIX_FMT_D3D11` → 인코딩
- MP4 먹서 (`AVFormatContext`, `avformat_write_header` 등)

### CApplication
- DX11 `Device` / `DeviceContext` / `SwapChain` 소유
- ImGui 초기화 및 렌더 루프
- `CCaptureManager`, `CVideoEncoder`, `CMainWindow` 소유
- Win32 메시지 루프

### CMainWindow
- ImGui 레이아웃:
  - 소스 선택 패널 (모니터 목록 / 창 목록)
  - 인코더 설정 (FPS, 품질, 파일 경로)
  - 미리보기 패널 (`ImGui::Image` + `ID3D11ShaderResourceView`)
  - 녹화 제어 버튼 + 경과 시간

---

## Phase별 구현 계획

### Phase 1: 프로젝트 설정
1. VS 솔루션(`.sln`) + 프로젝트(`.vcxproj`) 생성
2. C++20 표준 설정, 경고 레벨 W4, SDL 체크
3. ImGui 소스 파일 `third_party/imgui/` 에 포함, 빌드 설정
4. FFmpeg 사전 빌드 바이너리(BtbN) 다운로드 → `third_party/ffmpeg/`
   - **반드시 `lgpl` 태그 빌드 사용** (GPL 코덱 미포함, 상용/클로즈드소스 배포 가능)
   - `include/`, `lib/`, `bin/` (dll) 구조
5. `.vcxproj`에 include/lib 경로, 링커 설정 추가
6. 기본 `WinMain` + 빈 창 + DX11 초기화 확인

### Phase 2: DX11 + ImGui 기반 구축
1. `CApplication`: DX11 Device, DeviceContext, SwapChain, RTV 초기화
2. Win32 창 생성, WndProc 등록
3. ImGui 초기화 (`imgui_impl_win32`, `imgui_impl_dx11`)
4. 메인 루프: 메시지 처리 → ImGui 프레임 → 렌더 → Present
5. `CMainWindow` 스켈레톤 UI (빈 레이아웃만)

### Phase 3: 화면 캡처 구현
1. `Types.h`에 `SFrameData`, 관련 enum 정의
2. `ICapture` 인터페이스 정의
3. `CScreenCapture`: `IDXGIOutputDuplication`
   - 모니터 열거 (`IDXGIFactory → IDXGIAdapter → IDXGIOutput`)
   - `AcquireNextFrame()` → `ID3D11Texture2D` 반환
   - 프레임 해제 `ReleaseFrame()`
4. `CWindowCapture`: Windows.Graphics.Capture
   - HWND → `CreateForWindow()`
   - `D3D11CaptureFramePool` 설정
5. `CCaptureManager`: 스레드 + 프레임 큐 + FPS 조율

### Phase 4: ImGui 미리보기 연동
1. 캡처된 `ID3D11Texture2D`로 SRV 생성
2. `CMainWindow`에 `ImGui::Image()`로 실시간 프리뷰 표시
3. 소스 전환 시 SRV 재생성

### Phase 5: FFmpeg 인코더 구현
1. `CVideoEncoder`: `AVFormatContext` + `AVCodecContext` 초기화
2. `InitHardwareEncoder()`: nvenc → amf → qsv → h264_mf 순서
3. D3D11VA 하드웨어 컨텍스트 (`av_hwdevice_ctx_create` + D3D11VA)
4. `EncodeFrame()`: D3D11 텍스처 → `AVFrame` (`AV_PIX_FMT_D3D11`) → 인코딩
5. MP4 컨테이너 (`avformat_write_header`, `av_write_trailer`)
6. 인코딩 스레드 분리

### Phase 6: UI 완성 + 상태 머신
1. `eRecordState` 상태 머신 (Idle / Recording / Paused / Stopping)
2. 녹화 시작/중지/일시정지 버튼 연동
3. 출력 파일 경로 입력 (`GetSaveFileName` 파일 대화상자)
4. 인코더 선택 UI (감지된 HW 인코더 자동 표시)
5. 경과 시간, 파일 크기 실시간 표시
6. 설정값 (FPS, 품질 - CRF / 비트레이트)

### Phase 7: 안정화 및 마무리
1. 모든 COM/FFmpeg 리소스 안전 해제 확인 (RAII)
2. 오류 발생 시 ImGui 팝업 표시
3. 컴파일러 경고 W4 전부 해결
4. 멀티모니터 전환, DPI 변화 처리
5. DX11 디버그 레이어로 리소스 누수 점검

### Phase 추가: 분할 녹화 (Split Recording)
녹화 중 일정 시간 간격으로 자동으로 파일을 나누는 기능.

**동작 방식:**
- `SEncodeConfig::bSplitEnabled` + `nSplitMinutes` 설정
- 인코딩 프레임 수 기준 (`FPS × 분 × 60`)으로 분할 → 일시정지 중 자동 제외
- 분할 시 현재 파일 종료(`av_write_trailer`) → 새 타임스탬프 파일 자동 생성

**파일명 규칙:**
```
입력: ABC.mp4          설정: 1시간
출력: ABC_20260722_2000.mp4  (녹화 시작 시각)
      ABC_20260722_2100.mp4  (1시간 후 분할)
      ABC_20260722_2200.mp4  (2시간 후 분할)  ...
```

**구현 위치:**
- `SEncodeConfig` — `bSplitEnabled`, `nSplitMinutes` 추가
- `CCaptureManager::PerformSplit()` — 파일 교체 로직
- `CCaptureManager::GenerateSplitPath()` — 타임스탬프 파일명 생성 (`YYYYMMDD_HHMM`)
- `CMainWindow` — Split 체크박스 + 분 입력 UI, 분할 중 현재 파일명 표시

---

## 의존성 설정 상세

### ImGui
- https://github.com/ocornut/imgui 에서 다운로드
- 포함 파일: `imgui.h/cpp`, `imgui_draw.cpp`, `imgui_tables.cpp`, `imgui_widgets.cpp`
- 백엔드: `imgui_impl_win32.h/cpp`, `imgui_impl_dx11.h/cpp`

### FFmpeg (사전 빌드 바이너리)
- https://github.com/BtbN/FFmpeg-Builds/releases
- **`ffmpeg-master-latest-win64-lgpl-shared.zip` 빌드 선택** (GPL 코덱 미포함)
  - `lgpl` 빌드: libx264/libx265 등 GPL 코덱 미포함 → 클로즈드소스 배포 허용
  - `gpl` 빌드: libx264 포함 → **사용 금지**
- 필요 lib: `avcodec.lib`, `avformat.lib`, `avutil.lib`, `swscale.lib`, `swresample.lib`
- 필요 DLL: `avcodec-*.dll`, `avformat-*.dll`, `avutil-*.dll` 등

### DirectX 11 / WGC API
- Windows SDK 포함 (별도 설치 불필요)
- WGC API 사용 시 `windowsapp.lib` 링크 필요
- WinRT 헤더 (`#include <winrt/...>`) + `/await` 컴파일 플래그 설정

### h264_mf (소프트웨어 폴백)
- FFmpeg의 `h264_mf` 인코더 = Windows Media Foundation H.264 인코더 래핑
- Windows 내장 (`mfplat.dll`, `mfreadwrite.dll`) → 별도 설치/배포 불필요
- Microsoft가 MPEG LA H.264 특허료 납부 → 사용자 부담 없음
- 라이선스: Windows SDK 약관 (GPL 없음)

---

## 검증 체크리스트
1. [x] 솔루션 빌드 성공 (W4 경고 0개)
2. [x] ImGui 창 정상 표시 (DX11 렌더링 확인)
3. [x] 모니터 목록 정상 열거
4. [x] 미리보기 패널에서 실시간 화면 표시 (≥ 15fps)
5. [x] 모니터 캡처 → MP4 파일 정상 생성 + 재생 가능
6. [x] 특정 윈도우 캡처 → MP4 파일 정상 생성
7. [x] HW 인코더 감지 및 SW 폴백 동작 확인
8. [x] CPU 사용률 ≤ 15% (HW 인코딩 시 @1080p 30fps)
9. [x] 녹화 중 일시정지/재개 동작
10. [x] 프로그램 종료 시 리소스 누수 없음 (DX11 디버그 레이어 확인)

검증 체크리스트 항목 완료시 [] 사이에 체크([x])하여 진행할 것.

---

## 라이선스 요약

| 구성 요소 | 라이선스 | 비고 |
|---|---|---|
| FFmpeg (LGPL 빌드) | LGPL 2.1+ | DLL 동적 링킹 시 클로즈드소스 허용 |
| h264_nvenc | LGPL FFmpeg + NVIDIA 독점 SDK | GPL 없음 |
| h264_amf | LGPL FFmpeg + MIT (AMD AMF) | GPL 없음 |
| h264_qsv | LGPL FFmpeg + Intel 독점 | GPL 없음 |
| h264_mf | LGPL FFmpeg + Windows SDK 약관 | GPL 없음, MS가 특허료 납부 |
| ImGui | MIT | 제약 없음 |
| DirectX 11 / WGC | Windows SDK 약관 | 제약 없음 |
| ~~libx264~~ | ~~GPL v2~~ | **사용 금지** |

> **주의**: BtbN 빌드 다운로드 시 반드시 `lgpl` 태그 파일을 선택할 것. `gpl` 빌드 사용 시 libx264가 포함되어 배포물 전체가 GPL 오염됨.

---

## 범위 외 (Out of Scope)
- 오디오 캡처/녹음 (추후 추가 가능)
- 화면 자르기(크롭) 영역 선택 UI
- 실시간 스트리밍 (RTMP)
- H.265/HEVC 인코딩 (추후 확장 가능)

---

## 결정 사항 요약
| 항목 | 결정 |
|---|---|
| ImGui Backend | DirectX 11 |
| 화면 캡처 (모니터) | DXGI Desktop Duplication |
| 화면 캡처 (창) | Windows Graphics Capture API (WGC) |
| 인코더 | FFmpeg LGPL HW: NVENC → AMF → QSV → h264_mf(Windows MF) 폴백 |
| 출력 포맷 | MP4 (H.264) |
| 최소 OS | Windows 10 1803+ |
