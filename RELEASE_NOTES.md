## DeepImage Viewer 1.0.4

### 고친 점
- **휴대폰 사진이 "불러오는 중"에서 멈추던 문제:** 갤럭시 · 아이폰 등은 사진을 가로로 저장하고 "90° 회전" 정보(EXIF 방향)를 붙여 둡니다. 이런 사진을 바로 세울 때 Windows 회전 기능(WIC FlipRotator)을 JPEG 디코더에 직접 연결해 쓰고 있었는데, 이 방식은 회전된 한 줄을 만들 때마다 원본을 다시 해독해야 해서 큰 사진(약 2.5MB 이상, 1200만 화소)일수록 사실상 끝나지 않았습니다. 이제 한 번만 해독한 뒤 메모리에서 바로 회전합니다.
- 같은 원인으로 하단 썸네일이 늦게 뜨거나 비어 보이던 현상, 회전 저장(Ctrl+R 후 저장)이 느리던 현상도 함께 고쳤습니다.

### 설치 파일
- **DeepImage-Viewer-1.0.4-Setup-x64.exe**: 일반 Windows PC (Intel / AMD)
- **DeepImage-Viewer-1.0.4-Setup-arm64.exe**: ARM 기반 Windows PC (Snapdragon 등)
- **Portable zip**: 설치 없이 실행 (탐색기 썸네일 미포함)
- **DeepImage-Thumbnail-Help.zip**: 탐색기 썸네일 문제 해결 안내서 + 진단 도구

이전 버전 위에 그대로 설치하면 됩니다.
