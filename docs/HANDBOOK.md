# DeepImage Viewer — 개발 핸드북 (Claude 작업용)

> 새 채팅에서 이 프로젝트를 이어받을 때 **가장 먼저 읽는 문서**. 사용자와 확정한 기능·디자인·빌드·배포 절차를 정리한다.
> 마지막 갱신: 2026-11-01 · 현재 버전 **1.0.2**

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
- 맞춤 모드는 설정에 저장(`FitMode`)되어 다음 이미지에도 유지. "화면 꽉 차게" = 잘리지 않게 화면 안에 최대로(작은 이미지도 확대). "전체 보기" = 같지만 작은 이미지는 원래 크기.
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
| + − / 휠 | 확대·축소 | R | 크기 조절 |
| 0 / 1 | 전체 보기 / 1:1 | M | 색 보정 |
| E | 표시 모드 | F | 형광펜 |
| C | 자르기 | V | 비교 모드 |
| Ctrl+R / Ctrl+Shift+R | 회전 | F11 | 전체 화면 |
| Esc | 진행 중 작업(대화창·자르기·보정·표시·전체화면·비교) 취소, 없으면 **창 최소화** | T | 필름스트립 |

### G-code (1.0.2)
- `.gcode`/`.gco`/`.bgcode`는 **이미지 종류**로 취급: 슬라이서가 넣은 미리보기 중 **가장 큰 것**을 보여 준다(`app/image.cpp` `decodeGcode`).
- 텍스트: `; thumbnail[_PNG|_JPG|_QOI] begin WxH len` … base64 … `; thumbnail… end` (앞 6MB, 없으면 끝 2MB 검색), Snapmaker `thumbnail: data:image/png;base64,`. 바이너리: Prusa `GCDE` 블록 type 5(PNG 0 / JPG 1 / QOI 2). QOI는 자체 디코더.
- 미리보기 없으면 `load.gcode` 안내. 필름스트립 확장자 배지 표시, 탐색기 핸들러 CLSID `{E8E359B7-8AB6-4713-8956-BB7BAB759DDE}`(.gcode/.gco), `{E44C69BD-81E3-4D73-B040-7F5FF2ED4E1A}`(.bgcode). 툴패스 3D 렌더링은 아직 없음.
- SVG는 D2D SVG로 보기·썸네일·탐색기 썸네일 모두 지원(Wine에서는 안 보임).

### 썸네일 문제 해결 도구 (1.0.2)
- `docs/thumbnail-help/` : `THUMBNAIL-HELP.md`(한/영 안내), `ThumbnailFix.cmd` → `ThumbnailFix.ps1`(UTF-8 BOM, CRLF). 확장자별 `AssocQueryString(SHELLEXTENSION)` 결과와 담당 클래스 이름, 64비트 등록 여부(32비트 전용 = 깨짐), UserChoice ProgID 표시 → `ThumbnailReport.txt` 저장 → [C] 탐색기 종료·`thumbcache_*.db` 삭제·재시작.
- 설치 폴더 `Help\`와 포터블 zip에 포함, 릴리스 자산 `DeepImage-Thumbnail-Help.zip` 별도. 설정 창 하단 **썸네일 문제 해결** 버튼 → 설치 폴더 Help 열기(없으면 GitHub 문서). 제거기는 Help 파일·폴더도 지운다.

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

## 7. 지나온 함정 (다시 밟지 말 것)

- `layout()`이 매 프레임 `setViewport`를 부름 → **실제로 바뀐 경우에만** 다시 맞춤 처리(예전엔 맞춤 상태에서 +/−·이동이 매 프레임 되돌려짐).
- 확대 버튼 연타는 애니메이션 목표값(`target()`) 기준으로 누적.
- SVG 아이콘 root에 `width/height=24` 고정 → 모든 아이콘이 24px로 그려지고 오른쪽 아래로 밀림. 요청 크기를 넣는다.
- UI 휠: 최상단 영역(썸네일)에 휠 처리기가 없다고 끝내지 말고 아래 영역(필름스트립)까지 찾는다.
- 문자열 치환으로 코드 수정 시 같은 패턴이 두 클래스에 있으면 둘 다 바뀐다(thumb_provider의 Provider/Factory) — 횟수 지정.
- 공유 ProgID(Bambu)가 STL을 3MF 핸들러로 보냄 → 스트림 이름의 확장자로 판단.
- Creality Print의 32비트 전용 STL 핸들러가 ProgID에 걸려 64비트 탐색기에서 실패 → 우리 핸들러를 그 ProgID에도 등록.
- Windows OPC API는 슬라이서 3MF를 거부할 수 있음 → miniz로 직접 읽기.
- 설치 파일을 같은 이름으로 덮어쓰면 사용자가 예전 빌드를 설치했을 수 있다 → 보고 시 "이번 파일로 다시 설치" 명시.

## 8. 버전 기록

- **1.0.0** (2026-10-30): 첫 공개. 이미지+3D 뷰어, 마크업, 편집, EXIF/히스토그램, 탐색기 썸네일, 설치기(x64/ARM64), 비교 모드, 맞춤 5종, 단축키 개편, 3D프린팅 버튼.
- **1.0.1** (2026-11-01): 3MF 내장 썸네일 우선, 썸네일 디스크 캐시, 3D 썸네일 색상 설정, 탐색기 STL 수정, 아이콘 크기 버그 수정, 새 앱 아이콘(투명 배경 육면체 + DMIND 로고).
- **1.0.2** (2026-11-01): G-code/bgcode 미리보기(보기·썸네일·탐색기), 정보 창 DMIND·GitHub 링크, 탐색기 썸네일 문제 해결 안내서·진단 도구.
