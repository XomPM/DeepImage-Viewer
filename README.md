# DeepImage Viewer

빠르고 가벼운 Windows용 **이미지 + 3D 뷰어** · A fast, lightweight **image + 3D viewer** for Windows

Made by **DMIND** · dmind114@gmail.com

## 다운로드 · Download

[Releases](../../releases/latest) 페이지에서 PC에 맞는 파일을 받으세요.

| 파일 | 대상 |
|---|---|
| `DeepImage-Viewer-1.0.2-Setup-x64.exe` | 일반 Windows PC (Intel / AMD) |
| `DeepImage-Viewer-1.0.2-Setup-arm64.exe` | ARM 기반 Windows PC (Snapdragon 등) |
| `DeepImage-Viewer-1.0.2-Portable-*.zip` | 설치 없이 실행 (탐색기 썸네일 제외) |

설치는 현재 사용자 계정에만 진행되어 관리자 권한이 필요 없습니다. Windows 10 / 11.

## 기능 · Features

- **이미지:** JPG, PNG, GIF(애니메이션), WebP, HEIC, AVIF, TIFF, BMP, SVG, PSD, TGA, RAW 등
- **3D:** STL, 3MF, OBJ, PLY, glTF/GLB, FBX, STEP/STP, IGES/IGS, BREP, DAE, 3DS, AMF
- **G-code:** 슬라이서가 넣은 미리보기 이미지 표시 (.gcode / .gco / .bgcode — PNG·JPG·QOI)
- **보기:** 전체 보기 · 화면 꽉 차게 · 폭 맞춤 · 높이 맞춤 · 1:1 (선택 기억), 두 이미지 비교 모드
- **표시:** 펜, 형광펜, 직선, 화살표, 사각형, 원, 모자이크, 마스크, 선택·이동, 색상·굵기
- **편집:** 자르기, 크기 조절, 밝기·대비·색 보정, 회전·반전, 다른 형식으로 저장
- **정보:** EXIF·촬영 정보, 히스토그램
- **탐색기 썸네일:** 3D 파일과 일부 이미지 형식
- OS 테마(라이트/다크)와 강조색 연동, 한국어 / English
- 3D 보기에서 **3D프린팅** 버튼 → 3DMIND 제작 요청서

## 탐색기 썸네일이 안 보일 때 · Thumbnail troubleshooting

[docs/thumbnail-help/THUMBNAIL-HELP.md](docs/thumbnail-help/THUMBNAIL-HELP.md) 를 참고하세요. 설치 폴더의 `Help\ThumbnailFix.cmd` (또는 릴리스의 `DeepImage-Thumbnail-Help.zip`)로 진단과 썸네일 캐시 비우기를 한 번에 할 수 있습니다. 앱의 **설정 → 썸네일 문제 해결** 버튼으로도 열 수 있습니다.

## 단축키 · Shortcuts

| 키 | 기능 |
|---|---|
| ← → / Space | 이전 / 다음 이미지 |
| + − / 휠 | 확대 / 축소 |
| 0 / 1 | 전체 보기 / 실제 크기 |
| Tab | 정보 패널 |
| E | 표시(마크업) 모드 |
| F | 형광펜 |
| R | 크기 조절 |
| M | 색 보정 |
| C | 자르기 |
| V | 두 이미지 비교 |
| Ctrl+R / Ctrl+Shift+R | 회전 |
| F11 | 전체 화면 |
| Esc | 작업 취소, 없으면 창 최소화 |

## 빌드 · Building

Windows 전용 API(Win32, Direct2D, Direct3D 11, WIC, DirectWrite)로 작성된 C++20 프로젝트입니다.
Linux에서 [llvm-mingw](https://github.com/mstorsjo/llvm-mingw)로 x64 / ARM64 를 교차 빌드합니다.

필요한 정적 라이브러리: OpenCASCADE 7.9 (STEP/IGES), Assimp 6.0.

```sh
export LLVM_MINGW=/path/to/llvm-mingw
cmake -B build/x64   -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/x86_64.cmake  -DCMAKE_BUILD_TYPE=Release -DDEPS=/path/to/deps/x86_64
cmake -B build/arm64 -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64.cmake -DCMAKE_BUILD_TYPE=Release -DDEPS=/path/to/deps/aarch64
ninja -C build/x64 && ninja -C build/arm64
python3 package.py        # dist/ 에 설치 파일과 포터블 zip 생성
```

## 라이선스 · License

MIT © DMIND. 포함된 서드파티 구성요소(OpenCASCADE LGPL-2.1 + 예외, Assimp BSD, miniz MIT, Pretendard OFL, Lucide ISC)의 고지는 [licenses/](licenses/) 를 참고하세요.
