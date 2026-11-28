# DeepImage Viewer — 개발 핸드북 (Claude 작업용)

> 새 채팅에서 이 프로젝트를 이어받을 때 **가장 먼저 읽는 문서**. 사용자와 확정한 기능·디자인·빌드·배포 절차를 정리한다.
> 마지막 갱신: 2026-11-26 · 현재 버전 **1.0.13**

---

## 0. 사용자와 작업 방식

- 제작자 **DMIND** (dmind114@gmail.com). GitHub 계정 **XomPM**. 한국어로 대화한다.
- 사용자가 "기억만 해"라고 하면 목록만 쌓고, "수정해줘"라고 할 때 한꺼번에 고친다. 정리할 때는 번호 목록으로 다시 보여 준다.
- "직접 실험하지 말고 예측해줘"처럼 방식을 지정하면 그대로 따른다.
- 사용자 PC 경로를 기준으로 결과물을 넣는다(아래 1절). 결과를 보고할 때는 **실제 PC에서 확인했는지, Wine에서만 확인했는지** 구분해서 말한다.
- GitHub 릴리스 생성·태그 푸시는 이 작업 환경에서 **정책상 불가**("Creating, editing, or deleting releases is not permitted for this session type"). 우회하지 말고 `release.cmd`(사용자 PC에서 gh CLI 실행)를 만들어 사용자가 더블클릭하게 한다. `main` 브랜치 push는 된다.

## 1. 위치

| 무엇 | 위치 |
|---|---|
| 소스 저장소 | GitHub `XomPM/DeepImage-Viewer` (`main`) — 클라우드 작업 사본 `/home/claude/repo`, 빌드 작업 폴더 `/home/claude/native` |
| 사용자 PC 루트 | `D:\DeepImage Viewer` (연결된 폴더) |
| 릴리스 파일 | `D:\DeepImage Viewer\Release-<버전>\` (설치 파일 2 + 포터블 zip 2 + `RELEASE_NOTES.md` + `release.cmd`) |
| 바로 실행 사본 | `D:\DeepImage Viewer\Native\Portable-x64\DeepImage.exe` (+ `dvmodel.dll`, `fonts\`) |
| 의존성 빌드 캐시 | `D:\DeepImage Viewer\Native\build-cache\deps-install.tar.xz.part00~02` (+ `.sha256`) — OCCT/Assimp 정적 라이브러리(x86_64, aarch64) |
| 로고/아이콘 원본 | `D:\DeepImage Viewer\Logo\` (`DeepImage_icon.svg/.png/.ico`, `DMIND_logo_2D/3D.png/.jpg`) |
| 옛 Electron 버전 | `D:\DeepImage Viewer\source`, `Program` (더 이상 사용 안 함) |

## 2. 아키텍처

C++20, Windows 전용 API. Linux에서 **llvm-mingw**로 x64·ARM64 교차 빌드.

- **DeepImage.exe** (~1.2 MB): Win32 + Direct2D(DeviceContext5, SVG 아이콘, 효과) + DirectWrite(Pretendard) + WIC + D3D11. 즉시 모드 UI(영역은 직전 프레임 것으로 클릭 처리), 커스텀 타이틀바, Per-Monitor V2 DPI, 필요할 때만 다시 그림.
  - `--thumbnail <in> <out.png> <size> [--ext .stl]` : 탐색기 썸네일 워커 모드 (`app/worker.cpp`).
  - `--settings` : 시작하자마자 설정 창을 연다 (Wine 화면 확인용).
- **dvmodel.dll** (~23 MB, 지연 로드): OpenCASCADE 7.9.3(STEP/IGES/BREP, XCAF 색상) + Assimp 6.0.5 + 자체 3MF 리더(miniz) + WARP 썸네일 렌더러. C ABI(`model/dvmodel.h`). **앱만 바뀌면 다시 빌드할 필요 없음** — 기존 릴리스 zip에서 꺼내 써도 된다.
- **DeepImageThumbs.dll**: 탐색기 IThumbnailProvider. 직접 그리지 않고 `DeepImage.exe --thumbnail`을 Job 객체로 제한해 실행.
- **DeepImageSetup.exe**: D2D UI 설치기. 끝에 zip 페이로드 `[stub][zip]"DVPAYLD1"[u64 offset]`. 사용자 단위 설치 `%LOCALAPPDATA%\Programs\DeepImage Viewer`, 제거 시 임시 사본에서 `/uninstall2`. 페이로드에 네이티브 아키텍처가 없으면 x64 설치(ARM64 PC에서 에뮬레이션).

### 소스 지도

| 파일 | 내용 |
|---|---|
| `app/app.h` | App 상태 전체(비교 모드, 마크업, 편집, 썸네일 등) |
| `app/draw.cpp` | 모든 화면 그리기(타이틀바, 툴바, 레일, 비교 화면, 필름스트립, 정보/보정 패널, 설정/정보 모달, 3D프린팅 버튼) |
| `app/window.cpp` | 창 프로시저, 마우스/휠/키보드(단축키 표는 3절) |
| `app/actions.cpp` | 파일 열기·이동, 프리로드, 비교 모드, 맞춤 모드, 편집, 저장, `open3dPrint` |
| `app/imageview.*` | 보기 변환(맞춤 5종) + Markup(펜·형광펜·도형·모자이크·마스크·선택이동) |
| `app/bgremove.*` | AI 배경 제거: onnxruntime.dll/DirectML.dll 동적 로드, BiRefNet lite 1024² 추론, 모델 다운로드(WinHTTP + SHA-256) |
| `app/bgedit.*` | 배경 제거 마스크 파이프라인(Windows API 없음 — 리눅스 g++로 단독 테스트 가능) |
| `app/bgpanel.cpp` | 배경 제거 작업 스레드, 다듬기 패널, 브러시·물체·영역 도구 |
| `app/update.*` | 업데이트 확인(GitHub API releases/latest), Setup 다운로드·SHA-256 검사, Setup `/update` 실행, 알림 배너 |
| `app/capture.cpp` | 3D 화면 캡처(옵션 팝오버, 모델 옆 저장, `--markup`으로 새 창) |
| `app/notes.cpp` | 3D 노트(앵커 픽, 콜아웃, EDIT 자식 창 편집기, .dvnotes 저장) |
| `app/json.h` | 미니 JSON 파서/quote/base64 (업데이트·노트 공용) |
| `app/thumbs.cpp` | 썸네일 서비스(2스레드) + **디스크 캐시** |
| `app/gfx.cpp` | D2D/DWrite/SVG 아이콘 (아이콘 root svg width/height = 요청 크기 — 아니면 24px로 그려지는 버그) |
| `app/ui.cpp` | 위젯(toolButton, segmented, slider, swatch, toggle) — 휠은 맨 위 "휠 처리 영역"을 찾아 전달 |
| `app/settings.*` | `HKCU\Software\DMIND\DeepImageViewer` |
| `app/i18n.cpp` | 한/영 문자열. 툴팁 키에 `(단축키)` 붙이고, 버튼 라벨은 `TL()`로 단축키 제거 |
| `app/icons.inc` | Lucide 아이콘 + 자체 글리프(printer, columns-2, fit-width/height/fill, mosaic, mask) |
| `model/thumb_render.cpp` | WARP 썸네일 + **3MF 내장 썸네일 읽기**(miniz) + 사용자 색상 적용 |
| `shell/*` | 탐색기 핸들러, `stream_extension()`으로 실제 확장자 판단 |
| `setup/setup.cpp` | 설치/제거, 레지스트리, ProgID 썸네일 바인딩, ThumbnailCheck 기록 |
| `res/` | `app.ico`, `mark256.png`(실제 512px, 앱 안 로고 RCDATA 10), `app_icon.svg`(아이콘 원본) |
| `package.py` | `dist/DeepImage-Viewer-<ver>-Setup-{x64,arm64}.exe`, `-Portable-{x64,arm64}.zip` |
| `cmake/` | 툴체인 파일(x86_64, aarch64; `LLVM_MINGW` 환경변수 또는 `-DTC=`) |
| `tools/deps/` | OCCT/Assimp 빌드 스크립트(참고용, 경로는 /home/claude/deps 기준) + 패치 2개 |

## 3. 확정된 기능 · 디자인 (바꾸지 말 것)

### 공통
- OS 테마(라이트/다크)·강조색 연동, 한국어/English, Pretendard 글꼴, Lucide 선 아이콘. 아이콘 색은 항상 일반 아이콘 색(`P.text2`, hover `P.text`) — 특정 버튼만 강조색 쓰지 않는다.
- 하단 필름스트립: 현재 이미지를 항상 가운데(시간 기반 부드러운 이동), 썸네일 위에서 휠 → 좌우 스크롤.

### 이미지 보기
- 툴바: 축소 · 배율 · 확대 | **전체 보기 · 화면 꽉 차게 · 폭 맞춤 · 높이 맞춤 · 1:1** | 회전·반전 | **비교** · 표시 · 편집 | 저장 · 정보 · 전체화면.
- 맞춤 모드는 설정에 저장(`FitMode`)되어 다음 이미지에도 유지.
  - "전체 보기"(0) = 툴바 아래 영역에 잘리지 않게, **100% 넘게 키우지 않음**.
  - "화면 꽉 차게"(1) = **몰입 보기**(1.0.3, 사용자 선택): 툴바·필름스트립 숨기고 창 전체(제목줄 아래)에 잘림 없이 최대(작은 이미지 확대). 마우스가 위 84px / 필름스트립 근처로 가면 나타나고 3초 뒤 페이드아웃. 표시·편집·비교·전체화면 중엔 몰입 아님(`App::immersive()`). 예전 "잘리는(cover)" 방식은 사용자가 거부함.
- 좌우 여백 없음(padL/R=0, 1.0.3). 좌우 이동 화살표는 가장자리 120px 안으로 오면 표시, 3초 뒤 페이드아웃(`Reveal navRv/barRv/stripRv`, 타이머 3, `pokeChrome`).
- 왼쪽 아래 "해상도·크기·배율" 표시는 **이미지 보기에서 없음**(3D 보기에서만 표시).
- **비교 모드(V)**: 현재 + 다음 이미지를 좌우로, 줌·이동 동기화, ←/→ 2장씩 이동. 각 화면 아래 파일명 + 색 가로바. 왼쪽 = 연한 파랑 `#5AB4FF`, 오른쪽 = 빨강 `#FF453A`; 필름스트립 테두리도 같은 색 + L/R 표시. 썸네일 왼쪽 클릭 → 왼쪽, 오른쪽 클릭 → 오른쪽.

### 표시(마크업)
- 레일: 선택·이동(H) | 펜(P) · 형광펜(F) | 직선(L) · 화살표(A) · 사각형(U) · 원(O) | 모자이크(Z) · 마스크(K) | 지우개(X) | 실행취소 · 다시실행.
- 선택·이동: 도형을 끌면 이동, 빈 곳을 끌면 화면 이동. 선택하면 점선 테두리, Delete로 삭제, 툴바에서 그 도형 색·굵기 변경.
- 모자이크는 원본 픽셀 평균 블록(크기 4~64), 마스크는 단색 사각형. 둘 다 다른 표시보다 아래에 그린다.

### 단축키
| 키 | 기능 | 키 | 기능 |
|---|---|---|---|
| ← → / Space | 이전·다음 | Tab | 정보 패널 |
| PgUp / PgDn | 이전·다음 **폴더**(압축 파일 포함, 빈 폴더 건너뜀) | Home / End | 처음·마지막 |
| F5 | 슬라이드쇼 (스페이스 일시정지, Esc 끝) | I | 색상 추출 (스포이드) |
| P | 메뉴·썸네일 고정 | Ctrl+C (비교) | 두 이미지 높이 맞춰 한 장으로 복사 |
| X (3D) | 단면 보기 | D (3D) | 거리 측정 (두 번 클릭) |
| V (3D) | 3D 비교 | Esc (측정 중) | 찍은 점 취소 → 측정 끝 |
| + − / 휠 | 확대·축소 | R | 크기 조절 |
| 0 / 1 | 전체 보기 / 1:1 | M | 색 보정 |
| E | 표시 모드 | F | 형광펜 |
| C | 자르기 | V | 비교 모드 |
| Ctrl+R / Ctrl+Shift+R | 회전 | F11 | 전체 화면 |
| Esc | 진행 중 작업(대화창·자르기·보정·표시·전체화면·비교) 취소, 없으면 **창 최소화** | T | 필름스트립 |

### 압축 파일 (1.0.3)
- `.zip/.cbz/.tar/.cbt`를 폴더처럼 열기(`app/archive.*`, miniz + 자체 tar 파서). 항목은 `%TEMP%\DeepImage Viewer\arc\<해시>\<번호>\<이름>` 가상 경로를 받고 **처음 쓸 때 그 파일만 추출**(`arc::materialize` — decodeImageFile·renderThumbFile·readExif·3D 로드 앞에서 호출). 종료 시 이번 세션 폴더 삭제, 시작 시 하루 지난 잔여물 삭제.
- 이름: UTF-8 플래그/유효 UTF-8 아니면 OEM 코드페이지(한국 Windows = CP949). 경로 탈출(`../`) 방지 위해 파일명만 사용. `__MACOSX`, `._*` 제외. 썸네일 캐시 키는 압축 파일의 크기·시간 사용(추출 없이 캐시 적중).
- 제목줄 `압축파일 › 이름`, "폴더 열기"는 압축 파일 선택, 저장 대화상자는 압축 파일이 있는 폴더. 설치기: `DeepImageViewer.Archive` ProgID(연결 프로그램만), MSIX FTA `archives`.
- **7z/RAR 미지원**: libarchive(+xz, zlib) 크로스 빌드를 시도했으나 외부 소스 빌드가 자동 권한 검사에서 막힘 → 사용자 승인 필요.

### G-code (1.0.2)
- `.gcode`/`.gco`/`.bgcode`는 **이미지 종류**로 취급: 슬라이서가 넣은 미리보기 중 **가장 큰 것**을 보여 준다(`app/image.cpp` `decodeGcode`).
- 텍스트: `; thumbnail[_PNG|_JPG|_QOI] begin WxH len` … base64 … `; thumbnail… end` (앞 6MB, 없으면 끝 2MB 검색), Snapmaker `thumbnail: data:image/png;base64,`. 바이너리: Prusa `GCDE` 블록 type 5(PNG 0 / JPG 1 / QOI 2). QOI는 자체 디코더.
- 미리보기 없으면 `load.gcode` 안내. 필름스트립 확장자 배지 표시, 탐색기 핸들러 CLSID `{E8E359B7-8AB6-4713-8956-BB7BAB759DDE}`(.gcode/.gco), `{E44C69BD-81E3-4D73-B040-7F5FF2ED4E1A}`(.bgcode). 툴패스 3D 렌더링은 아직 없음.
- SVG는 D2D SVG로 보기·썸네일·탐색기 썸네일 모두 지원(Wine에서는 안 보임).

### 썸네일 문제 해결 도구 (1.0.2)
- `docs/thumbnail-help/` : `THUMBNAIL-HELP.md`(한/영 안내), `ThumbnailFix.cmd` → `ThumbnailFix.ps1`(UTF-8 BOM, CRLF). 확장자별 `AssocQueryString(SHELLEXTENSION)` 결과와 담당 클래스 이름, 64비트 등록 여부(32비트 전용 = 깨짐), UserChoice ProgID 표시 → `ThumbnailReport.txt` 저장 → [C] 탐색기 종료·`thumbcache_*.db` 삭제·재시작.
- 설치 폴더 `Help\`와 포터블 zip에 포함, 릴리스 자산 `DeepImage-Thumbnail-Help.zip` 별도. 설정 창 하단 **썸네일 문제 해결** 버튼 → 설치 폴더 Help 열기(없으면 GitHub 문서). 제거기는 Help 파일·폴더도 지운다.

### 배경 제거 (1.0.9)
- 편집 툴바 "배경 제거"(wand-sparkles) 또는 이미지 모드 B 키(3D 모드에서 B는 여전히 바운딩 박스). **1.0.10부터** AI 마스크가 바로 확정되지 않고 `EditPanel::Bg` 다듬기 패널이 열림 → 적용 시 `pushEdit` + `doc.replace`(투명이면 컨테이너 PNG, `bgCutout` 체크무늬). 편집 툴바 "완료" = 적용, Esc = 취소(`doc.upload`로 원본 복원).
- 다듬기 구조(`BgEdit`): 원본 PBGRA 사본 + AI 마스크(shared_ptr, 불변) → **stage**(작업 스레드, `bgComputeStage`: 강도 감마 2^(s/50) → 4연결 물체 라벨 → 선택 물체만(+림 팽창) → 가장자리 min/max(vHGW) → 부드럽게(박스 2회) → 색 번짐 제거 F=(I−(1−a)B)/a, B=배경 가중 저해상도 블러 → 흐림 배경) → **compose**(UI 스레드, 브러시 keep/erase 반영, 전체 또는 브러시 사각형만) → `ImageDoc::updatePixels`로 GPU 타일만 갱신(원본 프레임은 그대로). stage는 1개만 돌고 변경이 오면 `bgStageDirty`로 끝난 뒤 다시.
- 도구 1~5: 이동 / 남기기·지우기 브러시(화면 px 지름 `bgBrush`, [ ]) / 물체 클릭(라벨 토글) / 영역(사각형 안만 AI 다시 → 밖은 배경). M = 제거 영역 빨간 표시, Ctrl+Z = 브러시 패치·AI 마스크·물체 목록 되돌리기(30단계). 패널 중에는 다른 단축키·저장·복사 막음.
- 성능(컨테이너 2코어, 12MP): stage 기본 ~0.2초, 전부 켜면 ~1초, compose ~80ms.
- 모델: BiRefNet general lite(bb_swin_v1_tiny, MIT, 224,005,088 B, SHA-256 `5600024376f572a557870a5eb0afb1e5961636bef4e1e22132025467d0f03333`). 처음 사용 시 확인 후 `%LOCALAPPDATA%\DMIND\DeepImage Viewer\Models\birefnet-lite.onnx`로 내려받음. URL 순서: ① 우리 `releases/download/models-v1/birefnet-lite.onnx`(아직 없음 — 올리면 rembg 의존 제거) ② `danielgatis/rembg` v0.0.0 asset.
- 런타임: ONNX Runtime(NuGet Microsoft.ML.OnnxRuntime.DirectML) + DirectML.dll을 exe 옆에 배포(설치·포터블·MSIX 모두). 헤더는 `third_party/onnxruntime/include`. `dml_provider_factory.h`는 SAL 때문에 mingw에서 안 됨 → `OrtDmlApi` 최소 구조체 직접 선언.
- GPU 우선(D3D12 FL 11 확인) → 실패/크래시 시 CPU. `gpu-trial` 표식 파일로 지난번 GPU 크래시를 감지해 건너뜀, `DEEPIMAGE_BG_CPU=1`이면 CPU 강제. CPU 세션은 메모리 아레나·패턴 끔.
- CPU 1024² 추론 최대 메모리 약 5.3GB(최적화 수준과 무관), Wine 2코어 19.9초. 설치 제거 시 Models 폴더 삭제.
- 컨테이너에서 NuGet/HF/PyPI는 막힘 → 패키지는 사용자가 `D:\DeepImage Viewer\deps`에 넣고 스테이징.

### 업데이트 (1.0.11)
- GitHub 설치판만(`packagedAumid()`가 비어 있을 때). 시작 6초 뒤 타이머 5 → 하루 한 번(`UpdateLastCheck` = 1970년부터의 일수, `UpdateCheck` 끄기 가능) `api.github.com/repos/XomPM/DeepImage-Viewer/releases/latest` 조회(자체 미니 JSON 파서).
- 태그가 `kVersion`보다 크면(숫자 비교) 오른쪽 아래(필름스트립 위) 배너: 변경 내용(릴리스 페이지) · 지금 업데이트 · X. 정보 창 버전 줄에 "업데이트 확인" 버튼과 상태, 아래에 자동 확인 스위치.
- 지금 업데이트: `guardUnsaved` → 네이티브 아키텍처(`IsWow64Process2`)의 `*-Setup-<arch>.exe` 자산(없으면 x64)을 `%TEMP%\DeepImage-update`에 받음 → **GitHub 자산 `digest`(sha256)** 와 크기 비교 → `Setup /update /relaunch "<보던 파일>"`(압축 항목이면 압축 파일) 실행 후 앱 WM_CLOSE. 다음 시작 때 임시 폴더 정리.
- Setup `/update`: 환영 화면 없이 진행 화면 → 기존 설치 선택 유지(바탕화면 .lnk 존재, `Applications\DeepImage.exe\SupportedTypes` 키, `ThumbnailsRegistered`) → 끝나면 설치된 exe를 relaunch 인자로 실행하고 닫힘. 실패 시 오류 화면.
- 포터블(exe 옆에 Uninstall.exe 없음)은 "다운로드 페이지"만. **버전마다 GitHub 릴리스(release.cmd)가 있어야 동작**. 1.0.10 이하 Setup은 `/update`를 모르므로 일반 설치 화면이 뜸(1.0.11 이상끼리만 매끄러움).
- Wine 테스트: `http_proxy=$HTTPS_PROXY`를 주면 Wine WinHTTP가 컨테이너 프록시로 GitHub에 접속됨(Http::open은 AUTOMATIC → DEFAULT 프록시 순서로 재시도). `--about` 인자로 정보 창 바로 열기.

### 3D 캡처 (1.0.12)
- C 키 / 툴바 카메라 → 팝오버(`capMenu`, 위치는 toolbar()가 기록한 `capBtnRect`): 크기 `CapScale`(화면=G.scale, 2배, 4K 긴 변 3840, 최대 8192), 배경 `CapBg`(P.canvas/투명/흰색), `CapOverlay`(측정·노트), `CapOpen`. Ctrl+C 복사, Ctrl+S 다른 이름(기존 saveScreenshot 대체).
- `renderCapture`: `mv.screenshot(W,H)` → `renderToWic`에서 배경 + 모델 + 오버레이를 `Translation(-pane)·Scale(k)·타일변환`으로 그림(측정은 `measCapture` 플래그로 완료된 선만). 비교 모드는 양쪽을 `joinSideBySide`.
- 저장: 모델 폴더(압축 항목은 압축 파일 폴더)에 `<stem>_capture_YYYYMMDD-HHMMSS.png`(중복 시 -2..), 실패 시 사진\DeepImage Viewer. 열기: 자기 exe를 `--markup "<png>"`로 실행 → `startMarkup`이면 이미지 해독 후 `toggleAnnot(1)`.

### 3D 노트 (1.0.12)
- N = 노트 모드(측정과 배타), Shift+N = 보이기/숨기기(`NotesShow`). 클릭(드래그 아님) → `mv.pick`(꼭짓점/엣지 스냅) → `Note3D{model 좌표, snap, 화면 오프셋 dx/dy, text, color, img(≤480px), png}`. 콜아웃은 앵커 투영점+오프셋(화면 공간) → 회전해도 항상 정면·화살표 유지, 뷰포트 안으로 clamp.
- 가려짐: `ModelView::visible(model)`(눈→점 선분이 다른 삼각형에 막히는지, 단면 고려, 400k 삼각형 이하만) 160ms 스로틀 + 타이머 7로 정착 후 재계산 → 흐리게(알파 직접 곱함 — **Wine은 PushLayer 불투명도를 무시**). 가려진 것 먼저 그림.
- 편집기: 왼쪽 위 카드 + 실제 Win32 EDIT 자식 창(IME·여러 줄·클립보드; 부모에 WS_CLIPCHILDREN 필요), WM_CTLCOLOREDIT로 색, EN_CHANGE → 텍스트 반영 + 타이머 6(700ms) 저장, Esc/Ctrl+Enter/Tab 닫기, 이미지 없는 클립보드 그림 Ctrl+V → 노트 사진. 상자 끌기 = 이동(UI Region drag), 클릭 = 편집.
- 저장: `<모델>.dvnotes`(JSON, 사진 base64 PNG) 모델 옆, 압축 항목·쓰기 불가 폴더는 `%LOCALAPPDATA%\DMIND\DeepImage Viewer\Notes\<FNV해시>.dvnotes`. 노트가 0개면 파일 삭제. 모델 로드 시 `loadNotes()`, show()에서 `closeNoteEditor()`로 저장.

### 이미지 노트 (1.0.13)
- 3D 노트 코드(`notes.cpp`)를 이미지 보기에도 씀: 앵커 = 그림 픽셀 좌표(`model.x/y`, `snap = 3`), 화면 위치는 `view.toScreen`, 가려짐 없음. N = 노트 모드(비교·표시·스포이드·편집 해제), Shift+N 보이기. 클릭은 Pan 드래그가 4px 미만일 때. 이미지 해독 직후 `loadNotes()`.
- 저장·복사: `composite(withMarkup)`에서 `inverse(view.matrix()) * M * 타일변환`으로 화면 배치 그대로 그려 넣음(WYSIWYG 크기). **원본 경로에 덮어쓸 때는 `notesInOutput=false`** — .dvnotes가 남아 있어 두 번 보이는 것을 막음. 편집 패널이 열려 있으면 노트를 그리지 않음.

### 정보 창
- 버전 / 제작 **DMIND**(링크 → `https://www.godgok.com/3dmind/`) / 문의 메일 / **GitHub**(링크 → `https://github.com/XomPM/DeepImage-Viewer`). 링크는 강조색 + hover 밑줄 + 손 커서.

### 3D 보기
- D3D11, 궤도/이동/줌, 그리드·그림자·모서리·와이어·바운딩박스, 축 기즈모, 업축 Z↑/Y↑ 칩.
- 하단: 왼쪽 정보 표시, 가운데 조작 안내, 기즈모 왼쪽에 **"3D프린팅" 버튼**(printer 아이콘, 아이콘-글자 간격 약 9px) → `https://www.godgok.com/3dmind/`.

### 썸네일
- 우선순위: **① 디스크 캐시 → ② (일반 사진) Windows 썸네일 캐시 → ③ 3MF 내장 썸네일 → ④ 직접 렌더링**.
- 3MF 내장 썸네일 순서: Bambu `cover-thumbnail-middle/small` 관계 → `Metadata/plate_1.png/.jpg` → 표준 OPC `/metadata/thumbnail` 관계 → `Metadata/thumbnail.png`, `Thumbnails/thumbnail.png`, `Metadata/top_1.png`, `plate_1_small.png` → 다른 `plate_N.png`. 투명/단색뿐인 그림은 무시.
- 디스크 캐시: `%LOCALAPPDATA%\DMIND\DeepImage Viewer\ThumbCache\*.dvt` (`DVT1` 헤더 + zlib BGRA, 160px). 키 = 경로 + 크기 + 수정시각 + 렌더 버전 + (3D면) 색 설정. 300MB 넘으면 240MB까지 LRU 삭제. 렌더 규칙을 바꾸면 `thumbs.cpp`의 버전 상수 `v`를 올린다.
- 설정 → 썸네일: **3D 모델 색상**(기본 회색/흰/검/빨/주/노/초/파/보 + 직접 선택) — 기본은 색 없는 모델만, **"색이 있는 모델에도 적용"** 켜면 전부. 탐색기 썸네일(워커)도 같은 설정을 읽는다. **썸네일 캐시** 크기 표시 + 비우기.
- 탐색기 연결: 확장자 키 + SystemFileAssociations + UserChoice ProgID + **썸네일 슬롯이 이미 있는 다른 앱 ProgID**(Creality `stl_cxsw`, Bambu `" Bambu.Studio.1"` 등)에 우리 CLSID 등록, 원래 값은 `InstallBackup`에 백업/복원. 설치 후 `ThumbnailCheck`에 확장자별 실제 핸들러 기록 (`.stl` = `{655A6EB5-7D19-4029-96B7-393E71F543A1}`이면 정상).

### 브랜드 · 아이콘
- DMIND 색: 청록 **#359A96**, 진회색 **#53515A**.
- 앱 아이콘(`res/app_icon.svg`): **투명 배경**의 등각 육면체 + 진회색 `#2E2D33` 외곽선(둥근 모서리). 윗면 민트→청록 그라데이션+광택, 왼쪽 앞면 흰색에 DMIND 뇌 로고(원래 2색), 오른쪽 면 진청록 + 3D 격자선, 옅은 바닥 그림자. 1024 렌더 → ICO 16~256(≤32px 언샤프), `mark256.png`는 512px.
- SVG 렌더: Playwright Chromium(`page.setContent(svg)` → `screenshot({omitBackground:true})`).

## 4. 빌드 환경 처음부터 만들기 (클라우드 컨테이너는 매번 초기화됨)

```sh
# 1) 툴체인: llvm-mingw 20260908 (ucrt, Linux x86_64) — GitHub release asset 다운로드 허용됨
#    https://github.com/mstorsjo/llvm-mingw/releases/tag/20260908 → llvm-mingw-20260908-ucrt-ubuntu-*-x86_64.tar.xz
mkdir -p /home/claude/tc && tar -xf llvm-mingw-*.tar.xz -C /home/claude/tc && mv /home/claude/tc/llvm-mingw-* /home/claude/tc/llvm

# 2) 의존성: 사용자 PC 캐시를 가져와 푼다 (OCCT 다시 빌드하면 몇 시간)
#    device_stage_files 로 D:\DeepImage Viewer\Native\build-cache\deps-install.tar.xz.part00~02 를 올린 뒤
cat /mnt/user-data/uploads/*/Native/build-cache/deps-install.tar.xz.part* > /tmp/deps.tar.xz
sha256sum /tmp/deps.tar.xz   # deps-install.sha256 과 비교
mkdir -p /home/claude/deps && tar -xJf /tmp/deps.tar.xz -C /home/claude/deps   # → /home/claude/deps/install/{x86_64,aarch64}
#    캐시가 없으면: tools/deps/build-occt.sh, build-assimp.sh (OCCT V7_9_3 + occt-mingw-aarch64.patch, Assimp + assimp-poly2tri.patch)

# 3) 소스 + 빌드
git clone https://github.com/XomPM/DeepImage-Viewer /home/claude/native   # (add_repo 먼저)
cd /home/claude/native
mkdir -p fonts   # Pretendard-Regular/Medium/SemiBold.otf (github.com/orioncactus/pretendard 릴리스 zip) — 또는 기존 포터블 zip의 fonts\
export LLVM_MINGW=/home/claude/tc/llvm
for A in x64:x86_64 arm64:aarch64; do a=${A%%:*}; t=${A##*:}
  cmake -S . -B build/$a -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=cmake/$t.cmake -DDEPS=/home/claude/deps/install/$t
  ninja -C build/$a; done
python3 package.py fonts        # → dist/
```
- `.rc`가 참조하는 `res/app.ico`, `mark256.png`를 바꿨을 때 ninja가 감지 못할 수 있다 → `touch app/resource.rc setup/setup.rc` 후 빌드, 실행 파일에 새 PNG 바이트가 들어갔는지 확인.
- `NTDDI_VERSION`/`_WIN32_WINNT`는 CMake에서 정의(경고 1개는 무시). MinGW 헤더에 없는 것: `WICBitmapInterpolationModeHighQualityCubic` → `(WICBitmapInterpolationMode)4`, D2D Vibrance 효과 없음.
- 네트워크: npm/pip/apt 레지스트리는 막힘. GitHub clone, release asset 다운로드는 됨.

## 5. 테스트

- **Wine** (Kron4ek wine 11.19 amd64-wow64, `WINEPREFIX=/home/claude/wine/prefix`, `WINEDEBUG=-all`). D3D/WARP는 X 디스플레이가 필요 → `xvfb-run -a` 또는 `Xvfb :NN &` + `DISPLAY=:NN`. 화면 캡처는 `import -window root`.
  - Wine에는 D2D SVG가 없어 **아이콘이 안 보인다**(정상). `AssocQueryString` 쉘 확장도 `-`로 기록됨.
  - xvfb-run이 wineserver 때문에 안 끝날 수 있음 → 결과 파일 생성 확인 후 `pkill -f wineserver`.
- G-code 샘플은 Python으로 만들 수 있다(PIL로 PNG/JPG, QOI는 RGBA 청크만으로 인코딩, bgcode는 `GCDE` + 블록 + CRC32).
- 썸네일 워커 단독 테스트: `wine DeepImage.exe --thumbnail model.stl out.png 256`. 설정은 `wine reg add HKCU\Software\DMIND\DeepImageViewer /v ThumbColor ...`.
- 설치 테스트: `wine DeepImage-Viewer-<ver>-Setup-x64.exe /quiet` → `reg query ...\Uninstall\DeepImageViewer`.
- 실제 PC 확인은 컴퓨터 사용(권한 승인 필요)으로만 가능. 사용자에게 확인 범위를 정확히 말한다.

## 6. 버전 올리기 · 배포 체크리스트

1. 버전 문자열: `app/common.h kVersion`, `CMakeLists.txt project VERSION`, `package.py VERSION`, `app/resource.rc`·`setup/setup.rc`(FILEVERSION a,b,c,0 + 문자열 2곳), `licenses/LICENSES.txt`·`NOTICE-head.txt` 첫 줄, `README.md` 파일 이름.
2. x64·arm64 빌드 → `package.py` → 버전 문자열(UTF-16) 포함 확인.
3. `RELEASE_NOTES.md` 작성(한국어, 사용자 관점).
4. 저장소 갱신 후 `git push origin main` (커밋 메시지 끝에 Co-Authored-By/Claude-Session 줄).
5. `D:\DeepImage Viewer\Release-<ver>\`에 파일 4개 + `DeepImage-Thumbnail-Help.zip` + 노트 + `release.cmd`(gh CLI: 없으면 winget 설치 → `gh auth login --web` → 릴리스가 있으면 `upload --clobber`, 없으면 `release create --target main --latest`) 커밋. CRLF + UTF-8 + `chcp 65001`, 괄호 블록 안에서 `%PATH%` 확장 금지.
6. 사용자에게 `release.cmd` 더블클릭 안내 → 이후 `https://api.github.com/repos/XomPM/deepimage-viewer/releases`로 자산 확인.

## 6-1. Microsoft Store (MSIX)

- 목적: 서명 없는 설치 파일의 SmartScreen·브라우저 경고 회피 — Store가 무료로 서명. 개인 개발자 등록 무료(신분증+셀카).
- 도구: `microsoft/msix-packaging`의 **makemsix** (Linux 빌드). CMake 최소 3.29 → 3.28로 낮추고, ICU는 GitHub 릴리스 `icu4c-74_2-src.tgz`를 정적 빌드(`--prefix=/home/claude/icu-inst`) 후 `-DICU_ROOT`. 옵션 `-DMSIX_PACK=on -DUSE_VALIDATION_PARSER=on -DLINUX=on` (매니페스트 스키마 검증됨).
- 오픈소스 makemsix는 **일반 .msixbundle을 못 만든다**(평면 번들만) → 아키텍처별 `.msix` 2개를 Partner Center에 함께 올린다.
- `store/make_msix.py`: `shell_contract.h`에서 CLSID를 읽어 FileTypeAssociation마다 `desktop2:ThumbnailHandler`(중괄호 없는 GUID) + `com:SurrogateServer`에 `DeepImageThumbs.dll` 클래스 등록, 이미지 연결 프로그램, glTF는 연결만(IInitializeWithFile 필요). 에셋은 `res/app_icon_1024.png`에서 생성(scale/targetsize/unplated). 버전은 `kVersion` + `.0`.
- 패키지 ID: Partner Center → 제품 ID의 Name/Publisher/PublisherDisplayName을 `store/identity.json`에 넣어야 업로드 가능(없으면 자리표시자). 
- 앱 동작: `packagedAumid()`가 비어 있지 않으면 Store 실행 → AppUserModelID 덮어쓰지 않음, 기본 앱 설정은 `registeredAUMID=` 사용. Store 앱은 다른 앱 ProgID를 못 바꾸므로 썸네일 충돌 회피는 GitHub 설치판만.
- 문서: `store/STORE-GUIDE.md`(등록 순서), `store/STORE-LISTING.md`(설명·검색어·runFullTrust 사유), `docs/PRIVACY.md`(개인정보 처리방침 URL).

### Partner Center 실제 등록 (2026-11-01)
- 제품은 반드시 **"MSIX 또는 PWA 앱"**(EXE/MSI 앱은 서명된 설치 파일 URL이 필요해 불가). Store ID `9NGVFW0BKS97`, Name `0B0E4BB4.DeepImageViewer`, Publisher `CN=491EF4DE-ED2A-4D71-9E7B-7DF6D46E9373`, PublisherDisplayName `디마인드`, PFN `0B0E4BB4.DeepImageViewer_qkmcfd0fett58` → `store/identity.json`.
- 에셋은 **정확한 파일 이름**(Assets\StoreLogo.png 등)만 넣는다. scale-/targetsize- 변형만 있으면 "이미지를 찾을 수 없음" 오류(resources.pri 없음).
- runFullTrust → 개인정보처리방침 URL 필수(속성), 제출 옵션에 사용 이유(최대 500자). 가격: KRW 0, 범주 사진+동영상 / 보조 멀티미디어 디자인, IARC 모두 "아니요" → 3+.
- 스토어 목록(ko-KR, en-US) 설명·기능 9개·짧은 설명·개발자(DMIND) 입력됨. 스크린샷(≥1장)은 사용자가 업로드. 버전 업데이트 시: kVersion 올리기 → `store/make_msix.py` → 새 제출에서 패키지만 교체.
- 브라우저 작업: 사용자의 Claude 내장 브라우저에 로그인돼 있음. 약관 동의·삭제·파일 업로드·"인증을 위해 제출"은 사용자에게 맡긴다.

## 7. 지나온 함정 (다시 밟지 말 것)

- `layout()`이 매 프레임 `setViewport`를 부름 → **실제로 바뀐 경우에만** 다시 맞춤 처리(예전엔 맞춤 상태에서 +/−·이동이 매 프레임 되돌려짐).
- 확대 버튼 연타는 애니메이션 목표값(`target()`) 기준으로 누적.
- SVG 아이콘 root에 `width/height=24` 고정 → 모든 아이콘이 24px로 그려지고 오른쪽 아래로 밀림. 요청 크기를 넣는다.
- UI 휠: 최상단 영역(썸네일)에 휠 처리기가 없다고 끝내지 말고 아래 영역(필름스트립)까지 찾는다.
- 문자열 치환으로 코드 수정 시 같은 패턴이 두 클래스에 있으면 둘 다 바뀐다(thumb_provider의 Provider/Factory) — 횟수 지정.
- 공유 ProgID(Bambu)가 STL을 3MF 핸들러로 보냄 → 스트림 이름의 확장자로 판단.
- Creality Print의 32비트 전용 STL 핸들러가 ProgID에 걸려 64비트 탐색기에서 실패 → 우리 핸들러를 그 ProgID에도 등록.
- Windows OPC API는 슬라이서 3MF를 거부할 수 있음 → miniz로 직접 읽기.
- Wine 테스트: `LANG=C.UTF-8`이 없으면 한글 파일 이름 생성이 실패(코드 문제 아님). Xvfb + 자체 `inp.exe`(keybd_event/SetCursorPos)로 입력, 화면은 한 단계 늦게 갱신됨.
- **EXIF 회전에 `IWICBitmapFlipRotator`를 디코더에 직접 연결 금지** (1.0.4): 픽셀 단위로 요청 → JPEG는 줄 단위 해독이라 n² 동작, 휴대폰 사진(가로 저장 + 방향 6)이 "불러오는 중"에서 영원히 멈춤. 해독(`toPbgra`) 후 `applyOrientation`(메모리 회전)으로 처리. Wine의 FlipRotator는 이 문제가 없어 **Wine에서는 재현 안 됨**.
- ORT CPU 추론 테스트 중 OOM kill(컨테이너 8GB) — 다른 무거운 프로세스와 동시에 돌리지 말 것.
- 설치 파일을 같은 이름으로 덮어쓰면 사용자가 예전 빌드를 설치했을 수 있다 → 보고 시 "이번 파일로 다시 설치" 명시.

## 8. 버전 기록

- **1.0.0** (2026-10-30): 첫 공개. 이미지+3D 뷰어, 마크업, 편집, EXIF/히스토그램, 탐색기 썸네일, 설치기(x64/ARM64), 비교 모드, 맞춤 5종, 단축키 개편, 3D프린팅 버튼.
- **1.0.1** (2026-11-01): 3MF 내장 썸네일 우선, 썸네일 디스크 캐시, 3D 썸네일 색상 설정, 탐색기 STL 수정, 아이콘 크기 버그 수정, 새 앱 아이콘(투명 배경 육면체 + DMIND 로고).
- **1.0.2** (2026-11-01): G-code/bgcode 미리보기(보기·썸네일·탐색기), 정보 창 DMIND·GitHub 링크, 탐색기 썸네일 문제 해결 안내서·진단 도구.
- **1.0.3** (2026-11-07): 압축 파일(zip/cbz/tar/cbt) 보기, PgUp/PgDn 폴더 이동, 몰입 보기(화면 꽉 차게), 좌우 여백 제거 + 이동 버튼 자동 숨김, 전체 보기는 100% 이하. GitHub v1.0.3 릴리스 완료(사용자 release.cmd). Store: 1.0.2 게시 확인 후 2026-11-07 Submission 2로 1.0.3 MSIX 제출(인증 중). MSIX 업로드는 임시 브랜치 `store-upload` → raw URL fetch → DataTransfer 주입(중복 행이 '분석 중'에 멈추면 Cancel 후 새로고침 → 저장). 이 세션에서는 브랜치 삭제가 403으로 막힘.
- **1.0.4** (2026-11-13): 휴대폰 사진(EXIF 회전) 로딩 멈춤 수정 — 메모리 회전(`applyOrientation`), 썸네일·회전 저장도 같이 빨라짐.
- **1.0.5** (2026-11-21): 슬라이드쇼(F5, 간격 1/2/3/5/10초 `SlideDelay`, 타이머 4, 비교 모드는 두 장씩: [L R]→[R+1 R+2], 끝에서는 [마지막-1 마지막] 후 처음으로), 색상 추출(I, 원본 픽셀 샘플·HEX/RGB 카드·복사), 메뉴·썸네일 고정(P, `PinChrome` — 고정 시 immersive 해제), 비교 모드 PgUp/PgDn(새 폴더의 1·2번째), 비교 모드 복사(`joinSideBySide`: 큰 쪽 높이에 맞춰 나란히, 폭 20000 상한).
- **1.0.6** (2026-11-21): 1.0.5 + 비교 모드 슬라이드쇼 두 장씩 수정. 1.0.5는 Store 인증을 이미 통과해 교체 불가 → Store 버전은 항상 올려야 함(MSIX revision은 0 고정). GitHub는 v1.0.5를 건너뛰고 v1.0.6.
- **1.0.7** (2026-11-21): 3D 단면(X, 셰이더 clip + 안쪽 면을 단면색으로 칠해 캡 표현, `ModelView::setSection`), 거리 측정(D, CPU 레이캐스트 `ModelView::pick` + 꼭짓점 10px/모서리 7px 스냅, 측정값은 파일 좌표계 기준, 단위는 units==1 또는 stl/3mf/step/iges/amf면 mm, 측정마다 8색 팔레트 `kMeasColors`·번호, 카드에 전체 목록과 줄별 복사·삭제), 3D 비교(V, `mvR` + WM_APP_MODEL_R, `compareModels`일 때 nextImage가 3D 파일을 고름, 카메라·표시 설정은 `allModels`로 양쪽 동기화).
- **1.0.8** (2026-11-23): 첫 IDAT 청크가 1MB를 넘는 PNG(스캐너가 그림 전체를 한 청크로 저장)는 WIC 대신 stb_image로 해독(`pngHasGiantIdat`). WIC가 띠마다 그 청크를 다시 푸는 것으로 보여 A4 300dpi 스캔이 "불러오는 중"에 오래 멈췄음. Wine에서는 재현 안 됨(실제 Windows에서 확인 필요).
- **1.0.9** (2026-11-26): AI 배경 제거(B) — BiRefNet lite + ONNX Runtime(DirectML → CPU 대체), 첫 사용 시 모델 다운로드. 설치 파일 약 15MB 증가. 실제 Windows의 GPU 경로·다운로드는 사용자 확인 필요.
- **1.0.10** (2026-11-26): 배경 제거 다듬기 패널 — 남기기/지우기 브러시, 물체 선택, 영역 다시 인식, 강도·가장자리·부드럽게·색 번짐 제거, 배경 투명/단색/흐림, 제거 영역 표시, 되돌리기. Wine에서 전 기능 확인, 실제 PC 확인 필요. Store: 1.0.7 게시 확인 후 2026-11-26 Submission 7로 1.0.10 제출(인증 중, 1.0.8·1.0.9는 Store 건너뜀). 패키지 페이지 URL `.../products/9NGVFW0BKS97/submissions/<id>/packages`, 목록은 `.../submissions/<id>/listings?languageid=61&languagecode=ko-kr`(en-us는 4), 새 기능 칸은 `textarea[aria-label=releaseNotes]`(최대 1500자).
- **1.0.11** (2026-11-27): 업데이트 알림 + 버튼 한 번으로 설치(GitHub digest 검사, Setup `/update` 후 보던 파일 다시 열기), 정보 창 업데이트 확인·자동 확인 스위치. Wine에서 확인 → 다운로드·검사·Setup 실행·재실행까지 동작.
- **1.0.12** (2026-11-28): 3D 화면 캡처(모델 옆 저장 → 새 창 표시 모드, 크기·배경·오버레이 옵션, 복사/다른 이름), 3D 노트(면·엣지·꼭짓점 화살표 + 글·사진, 회전해도 정면 유지, 가려지면 흐리게, .dvnotes 저장). Wine에서 확인.
- **1.0.13** (2026-11-28): 이미지 보기 노트(화살표+글·사진, 저장·복사에 포함). GitHub 최신 릴리스는 아직 v1.0.10(1.0.11~1.0.13 release.cmd 미실행).
