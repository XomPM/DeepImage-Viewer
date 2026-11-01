## DeepImage Viewer 1.0.2

### 새 기능
- **G-code 미리보기:** PrusaSlicer · Orca Slicer · Bambu Studio · Creality Print · SuperSlicer · Cura(썸네일 플러그인)가 G-code에 넣은 미리보기 이미지를 보여 줍니다. `.gcode`, `.gco`, Prusa 바이너리 `.bgcode`(PNG · JPG · QOI)를 지원하며, 하단 썸네일 목록과 탐색기 썸네일에도 표시됩니다. 미리보기가 없는 G-code는 안내 문구를 보여 줍니다.
- **정보 창 링크:** 제작(DMIND)을 누르면 3DMIND 페이지, 새 GitHub 줄을 누르면 저장소가 열립니다.

- **탐색기 썸네일 문제 해결 도구:** 설치 폴더 `Help` 안의 `ThumbnailFix.cmd`가 확장자별 썸네일 담당 프로그램을 진단하고(다른 앱 충돌, 깨진 32비트 처리기 표시) 썸네일 캐시를 비워 줍니다. 안내서 `THUMBNAIL-HELP.md` 포함. 설정 창의 **썸네일 문제 해결** 버튼으로 열 수 있고, 릴리스에서 `DeepImage-Thumbnail-Help.zip`으로 따로 받을 수도 있습니다.

### 참고
- SVG는 보기 · 썸네일 · 탐색기 썸네일을 이미 지원합니다.
- 1.0.1의 3MF 내장 썸네일 · 썸네일 캐시 · 3D 썸네일 색상 · 새 아이콘이 모두 포함되어 있습니다.

### 설치 파일
- **DeepImage-Viewer-1.0.2-Setup-x64.exe**: 일반 Windows PC (Intel / AMD)
- **DeepImage-Viewer-1.0.2-Setup-arm64.exe**: ARM 기반 Windows PC (Snapdragon 등)
- **Portable zip**: 설치 없이 실행 (탐색기 썸네일 미포함)
- **DeepImage-Thumbnail-Help.zip**: 탐색기 썸네일 문제 해결 안내서 + 진단 도구

이전 버전 위에 그대로 설치하면 됩니다. 탐색기 썸네일이 바로 안 바뀌면 디스크 정리 → "미리 보기 사진"을 비우세요.
