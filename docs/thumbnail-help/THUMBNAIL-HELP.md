# 탐색기 썸네일이 안 보일 때 — DeepImage Viewer

DeepImage Viewer는 설치할 때 **"탐색기 썸네일"** 을 켜면 파일 탐색기에서 3D 파일(STL, 3MF, STEP, OBJ …), G-code, SVG 등의 썸네일을 보여 줍니다.
썸네일이 아이콘으로만 보이면 아래 순서대로 확인하세요. 대부분 **1 ~ 4번**에서 해결됩니다.

> 빠른 방법: 이 폴더의 **`ThumbnailFix.cmd`** 를 더블클릭하면 2번(진단)과 4번(캐시 비우기)을 자동으로 해 줍니다.

---

## 1. 최신 버전으로 다시 설치

- [Releases](https://github.com/XomPM/DeepImage-Viewer/releases/latest)에서 PC에 맞는 설치 파일(일반 PC `x64`, Snapdragon 등 `arm64`)을 받아 설치합니다.
- 설치 화면에서 **"탐색기 썸네일"** 옵션이 켜져 있는지 확인하세요.
- **다른 3D 프로그램(Creality Print, Bambu Studio, Orca Slicer, 3D Builder 등)을 새로 설치하거나 업데이트한 뒤**에는 그 프로그램이 썸네일 설정을 다시 가져갈 수 있습니다. 이때도 DeepImage Viewer를 한 번 더 설치하면 됩니다(설정은 그대로 유지됩니다).
- 포터블(zip) 버전은 탐색기 썸네일을 지원하지 않습니다.

## 2. 어떤 프로그램이 썸네일을 맡고 있는지 확인 (진단)

`ThumbnailFix.cmd` → 진단 결과에서 확장자마다 담당 프로그램이 표시됩니다.

| 표시 | 의미 | 해결 |
|---|---|---|
| `DeepImage …` | 정상 | 4번(캐시 비우기) |
| 다른 이름 (예: Creality, Bambu) | 다른 프로그램이 썸네일을 맡고 있음 | 1번(DeepImage 재설치) |
| `깨짐(32비트 전용)` | 다른 프로그램이 64비트 탐색기에서 쓸 수 없는 처리기를 등록함 (예: Creality Print의 STL) | 1번 — DeepImage 1.0.1 이상이 그 위에 연결합니다 |
| `없음` | 썸네일 처리기가 연결되지 않음 | 1번 |

설치 프로그램도 확장자별 결과를 `HKCU\Software\DMIND\DeepImageViewer\ThumbnailCheck` 에 기록합니다. `.stl` 값이 `{655A6EB5-7D19-4029-96B7-393E71F543A1}` 이면 정상입니다.

## 3. 탐색기 보기 설정

- 파일 탐색기 → **보기 → 옵션(폴더 옵션) → 보기** 탭에서 **"항상 아이콘만 표시, 미리 보기 표시 안 함"** 이 **꺼져** 있어야 합니다.
- **시스템 → 고급 시스템 설정 → 성능 설정** 에서 **"아이콘 대신 미리 보기 표시"** 가 **켜져** 있어야 합니다.
- 보기 형식이 **"자세히" / "목록"** 이면 썸네일이 작게 보이거나 안 보입니다. **보통 아이콘 이상**으로 바꿔 보세요.

## 4. 썸네일 캐시 비우기 (예전 결과가 남아 있을 때)

Windows는 한 번 실패한 썸네일을 저장해 두고 다시 만들지 않습니다. 설치·재설치 후에는 캐시를 비워야 합니다.

- 방법 A: `ThumbnailFix.cmd` → **[C] 캐시 비우기** (탐색기가 잠깐 닫혔다 다시 열립니다)
- 방법 B: 시작 → **디스크 정리** → C: 드라이브 → **"미리 보기 사진"** 만 체크 → 확인
- DeepImage Viewer 안의 썸네일(하단 목록)은 **설정 → 썸네일 → 썸네일 캐시 → 비우기** 로 따로 비웁니다.

## 5. 그 밖의 경우

- **OneDrive "온라인 전용" 파일**: 내려받지 않은 파일은 Windows가 썸네일을 만들지 않습니다. "이 장치에 항상 유지"로 바꾸세요.
- **네트워크 드라이브 / NAS**: Windows 설정에 따라 썸네일을 만들지 않을 수 있습니다.
- **아주 큰 파일**: 512MB가 넘는 파일, 또는 20초 안에 그리지 못하는 아주 복잡한 STEP/IGES는 썸네일을 건너뜁니다.
- **G-code**: 슬라이서가 미리보기 이미지를 넣은 파일만 썸네일이 나옵니다(슬라이서 설정의 "썸네일/미리보기" 옵션).
- **3MF**: 슬라이서·제작자가 넣은 썸네일이 있으면 그것을, 없으면 DeepImage Viewer가 직접 그린 그림을 보여 줍니다.
- **썸네일 색상**: DeepImage Viewer **설정 → 썸네일 → 3D 모델 색상** 을 바꾼 뒤에는 4번(캐시 비우기)을 해야 탐색기에 반영됩니다.

## 그래도 안 되면

`ThumbnailFix.cmd` 진단 결과 화면(또는 같은 폴더에 저장되는 `ThumbnailReport.txt`)과 함께 **dmind114@gmail.com** 으로 보내 주세요.

---

# Thumbnails not showing in File Explorer

1. **Reinstall the latest version** with *Explorer thumbnails* enabled. Reinstall again after installing/updating other 3D apps (Creality Print, Bambu Studio, …) — they may take the thumbnail slot back.
2. **Diagnose**: run `ThumbnailFix.cmd`. Each extension shows the handler Explorer uses. `DeepImage …` = OK; another app or *broken (32-bit only)* = reinstall DeepImage Viewer 1.0.1+.
3. **Explorer settings**: Folder Options → View → turn **off** "Always show icons, never thumbnails"; Performance Options → turn **on** "Show thumbnails instead of icons".
4. **Clear the thumbnail cache**: `ThumbnailFix.cmd` → **[C]**, or Disk Cleanup → *Thumbnails*.
5. Online-only OneDrive files, network shares, files over 512 MB and G-code without an embedded preview get no thumbnail.

Still stuck? Send the report (`ThumbnailReport.txt`) to dmind114@gmail.com.
