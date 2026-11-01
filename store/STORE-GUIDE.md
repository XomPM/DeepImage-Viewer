# Microsoft Store 등록 순서 (DeepImage Viewer)

Store 버전은 Microsoft가 서명해 주므로 **다운로드·설치 경고가 없습니다.** 처음 한 번만 아래 순서를 따르면 되고, 다음 버전부터는 패키지만 새로 올리면 됩니다.

## 1. 개발자 등록 (무료, 처음 한 번)
1. https://storedeveloper.microsoft.com 접속 → **개인(Individual)** 으로 시작
2. 개인 Microsoft 계정으로 로그인 → 신분증 + 셀카로 본인 인증
3. 인증이 끝나면 Partner Center(https://partner.microsoft.com/dashboard)에 들어갈 수 있습니다.

## 2. 앱 이름 예약
1. Partner Center → **앱 및 게임 → 새 제품 → MSIX 또는 PWA 앱**
2. 이름 **DeepImage Viewer** 예약

## 3. 패키지 ID 확인 → Claude에게 전달
1. 앱 → **제품 관리 → 제품 ID(Product identity)**
2. 아래 세 값을 복사해 Claude에게 알려 주세요 (또는 `store/identity.json`에 입력):
   - `Package/Identity/Name` (예: 12345DMIND.DeepImageViewer)
   - `Package/Identity/Publisher` (예: CN=XXXXXXXX-XXXX-…)
   - `Package/Properties/PublisherDisplayName` (예: DMIND)
3. Claude가 이 값으로 **x64 / ARM64 .msix 패키지 2개**를 다시 만들어 `Release-<버전>\Store` 폴더에 넣습니다.

## 4. 제출 만들기 (Partner Center → 앱 → 제출 시작)
| 항목 | 입력 |
|---|---|
| 가격 및 사용 가능 여부 | 무료, 시장: 모든 시장(또는 원하는 국가) |
| 속성 | 범주: 사진 및 비디오, 개인정보 처리방침 URL·웹사이트·연락처 (STORE-LISTING.md) |
| 연령 등급 | 설문에서 폭력·채팅·구매 등 모두 "아니요" → 전체 이용가 |
| 패키지 | `DeepImageViewer_<버전>_x64.msix`, `DeepImageViewer_<버전>_arm64.msix` 두 개를 끌어다 놓기 |
| 스토어 목록 | 한국어 / 영어: STORE-LISTING.md의 설명·기능·검색어 붙여 넣기, 스크린샷 1~4장 |
| 제출 옵션 | 제한된 기능(runFullTrust) 사용 이유: STORE-LISTING.md 문구 붙여 넣기 |

**제출** 을 누르면 보통 1~3일 안에 인증 결과가 메일로 옵니다.

## 5. 업데이트할 때
버전이 올라가면 Claude가 새 .msix 2개를 만들어 줍니다. Partner Center에서 **새 제출(업데이트)** → 패키지만 교체 → 제출.

## 참고: Store 버전과 GitHub 설치 파일의 차이
- Store 버전: 경고 없이 설치, 자동 업데이트, 시작 메뉴·연결 프로그램·탐색기 썸네일 지원.
- 다른 3D 앱(Creality Print, Bambu Studio 등)이 이미 차지한 확장자의 탐색기 썸네일을 **되찾는 기능은 Store 버전에 없습니다**(Store 앱은 다른 앱 설정을 바꿀 수 없음). 그런 경우 GitHub 설치 파일 버전을 쓰면 됩니다.
