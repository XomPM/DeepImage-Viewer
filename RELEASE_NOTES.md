## DeepImage Viewer 1.0.9

### 새 기능
- **AI 배경 제거 (B):** 편집 모드의 "배경 제거" 버튼 또는 B 키. 오픈 라이선스(MIT) 중 가장 정확한 축에 드는 **BiRefNet** 모델로 사람·물건·로고의 배경을 지웁니다. 머리카락이나 도형 안쪽 구멍처럼 까다로운 부분도 처리합니다.
  - **PC 안에서만 처리**: 사진이 외부로 전송되지 않습니다.
  - **그래픽카드 가속**: DirectX 12 그래픽카드(Intel · AMD · NVIDIA · Qualcomm)가 있으면 DirectML로 빠르게 처리하고, 없으면 CPU로 자동 전환합니다.
  - **처음 한 번만 모델 내려받기**(약 214MB, SHA-256 검사). 그다음부터는 인터넷 없이 동작합니다.
  - 결과는 체크무늬 배경으로 투명한 부분이 보이고, **되돌리기**로 원래대로 돌아갑니다. 저장할 때는 투명도를 유지하도록 PNG가 기본입니다.

### 참고
- 처리 중 메모리를 많이 씁니다(CPU 처리 시 최대 약 5GB). 메모리가 8GB인 PC에서는 다른 프로그램을 닫고 쓰면 좋습니다.
- 설치 파일이 ONNX Runtime · DirectML 구성요소로 약 15MB 커졌습니다.
- 1.0.8까지의 기능(PNG 로딩 개선, 3D 단면 · 거리 측정 · 3D 비교 등)이 모두 포함되어 있습니다.

### 설치 파일
- **DeepImage-Viewer-1.0.9-Setup-x64.exe**: 일반 Windows PC (Intel / AMD)
- **DeepImage-Viewer-1.0.9-Setup-arm64.exe**: ARM 기반 Windows PC (Snapdragon 등)
- **Portable zip**: 설치 없이 실행 (탐색기 썸네일 미포함)
- **DeepImage-Thumbnail-Help.zip**: 탐색기 썸네일 문제 해결 안내서 + 진단 도구
