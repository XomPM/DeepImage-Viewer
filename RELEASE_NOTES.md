## DeepImage Viewer 1.0.8

### 고친 점
- **일부 PNG가 "불러오는 중"에서 오래 멈추던 문제:** 스캐너나 일부 프로그램은 PNG의 그림 데이터 전체를 한 덩어리(IDAT 청크 하나)로 저장합니다. Windows 기본 PNG 해독기는 이런 파일을 띠 단위로 읽을 때마다 그 덩어리를 처음부터 다시 풀어서, A4 300dpi 스캔(약 4.5MB)처럼 큰 파일은 한참 걸리거나 썸네일이 비어 보였습니다. 이런 파일은 이제 한 번에 해독하는 별도 해독기(stb_image)로 바로 엽니다. 일반 PNG는 기존 그대로 Windows 해독기를 씁니다.

1.0.7의 3D 단면 보기 · 거리 측정 · 3D 비교가 모두 포함되어 있습니다.

### 설치 파일
- **DeepImage-Viewer-1.0.8-Setup-x64.exe**: 일반 Windows PC (Intel / AMD)
- **DeepImage-Viewer-1.0.8-Setup-arm64.exe**: ARM 기반 Windows PC (Snapdragon 등)
- **Portable zip**: 설치 없이 실행 (탐색기 썸네일 미포함)
- **DeepImage-Thumbnail-Help.zip**: 탐색기 썸네일 문제 해결 안내서 + 진단 도구
