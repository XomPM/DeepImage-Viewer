// DeepImage Viewer — Korean / English strings
// Copyright (c) DMIND <dmind114@gmail.com>. MIT License.
#include "i18n.h"
#include <unordered_map>

namespace dv {
namespace {
struct Entry { const wchar_t* ko; const wchar_t* en; };
const std::unordered_map<std::string, Entry>& table() {
    static const std::unordered_map<std::string, Entry> t = {
        {"open", {L"열기", L"Open"}},
        {"open.file", {L"파일 열기", L"Open file"}},
        {"open.hint", {L"이미지나 3D 파일을 여기로 끌어다 놓으세요", L"Drop an image or a 3D file here"}},
        {"open.formats.images", {L"이미지", L"Images"}},
        {"open.formats.models", {L"3D 모델", L"3D models"}},
        {"tagline", {L"이미지와 3D를 한 화면에서", L"Images and 3D, in one place"}},
        {"filter.all", {L"지원되는 모든 파일", L"All supported files"}},
        {"filter.images", {L"이미지", L"Images"}},
        {"filter.models", {L"3D 모델", L"3D models"}},
        {"filter.any", {L"모든 파일", L"All files"}},
        {"prev", {L"이전 (←)", L"Previous (←)"}},
        {"next", {L"다음 (→)", L"Next (→)"}},
        {"zoom.in", {L"확대 (+)", L"Zoom in (+)"}},
        {"zoom.out", {L"축소 (−)", L"Zoom out (−)"}},
        {"zoom.fit", {L"전체 보기 (0)", L"Fit to window (0)"}},
        {"zoom.fill", {L"화면 꽉 차게", L"Fill window"}},
        {"zoom.width", {L"폭 맞춤", L"Fit width"}},
        {"zoom.height", {L"높이 맞춤", L"Fit height"}},
        {"compare", {L"두 이미지 비교 (V)", L"Compare two images (V)"}},
        {"compare.hint", {L"썸네일 왼쪽 클릭 → 왼쪽 · 오른쪽 클릭 → 오른쪽", L"Thumbnail left-click → left · right-click → right"}},
        {"compare.imageonly", {L"비교는 이미지 파일만 할 수 있습니다", L"Only images can be compared"}},
        {"compare.need2", {L"비교할 이미지가 2장 이상 필요합니다", L"Compare needs at least two images"}},
        {"print3d", {L"3D프린팅", L"3D Printing"}},
        {"print3d.tip", {L"3DMIND 제작 요청서 열기", L"Open the 3DMIND print request form"}},
        {"zoom.actual", {L"실제 크기 (1)", L"Actual size (1)"}},
        {"rotate.left", {L"왼쪽으로 회전 (Ctrl+Shift+R)", L"Rotate left (Ctrl+Shift+R)"}},
        {"rotate.right", {L"오른쪽으로 회전 (Ctrl+R)", L"Rotate right (Ctrl+R)"}},
        {"flip.h", {L"좌우 반전", L"Flip horizontal"}},
        {"flip.v", {L"상하 반전", L"Flip vertical"}},
        {"annotate", {L"표시하기 (E)", L"Markup (E)"}},
        {"annot.title", {L"표시", L"Markup"}},
        {"edit", {L"편집", L"Edit"}},
        {"info", {L"정보 (Tab)", L"Info (Tab)"}},
        {"info.title", {L"정보", L"Info"}},
        {"fullscreen", {L"전체 화면 (F11)", L"Full screen (F11)"}},
        {"settings", {L"설정", L"Settings"}},
        {"about", {L"정보", L"About"}},
        {"theme", {L"테마", L"Theme"}},
        {"theme.system", {L"시스템", L"System"}},
        {"theme.light", {L"라이트", L"Light"}},
        {"theme.dark", {L"다크", L"Dark"}},
        {"language", {L"언어", L"Language"}},
        {"lang.system", {L"시스템", L"System"}},
        {"tool.hand", {L"선택·이동 (H)", L"Select & move (H)"}},
        {"tool.pen", {L"펜 (P)", L"Pen (P)"}},
        {"tool.hl", {L"형광펜 (F)", L"Highlighter (F)"}},
        {"tool.mosaic", {L"모자이크 (Z)", L"Mosaic (Z)"}},
        {"tool.mask", {L"마스크 (K)", L"Mask (K)"}},
        {"tool.block", {L"모자이크 크기", L"Block size"}},
        {"tool.delete", {L"선택 삭제 (Delete)", L"Delete selection (Delete)"}},
        {"tool.line", {L"직선 (L)", L"Line (L)"}},
        {"tool.arrow", {L"화살표 (A)", L"Arrow (A)"}},
        {"tool.rect", {L"사각형 (U)", L"Rectangle (U)"}},
        {"tool.ellipse", {L"원 (O)", L"Ellipse (O)"}},
        {"tool.eraser", {L"지우개 (X)", L"Eraser (X)"}},
        {"tool.color", {L"색상", L"Colour"}},
        {"tool.width", {L"굵기", L"Size"}},
        {"tool.fill", {L"채우기", L"Fill"}},
        {"undo", {L"실행 취소 (Ctrl+Z)", L"Undo (Ctrl+Z)"}},
        {"redo", {L"다시 실행 (Ctrl+Y)", L"Redo (Ctrl+Y)"}},
        {"clear", {L"모두 지우기", L"Clear all"}},
        {"save", {L"다른 이름으로 저장 (Ctrl+S)", L"Save as (Ctrl+S)"}},
        {"copy", {L"클립보드에 복사 (Ctrl+C)", L"Copy to clipboard (Ctrl+C)"}},
        {"done", {L"완료", L"Done"}},
        {"apply", {L"적용", L"Apply"}},
        {"cancel", {L"취소", L"Cancel"}},
        {"reset", {L"초기화", L"Reset"}},
        {"ok", {L"확인", L"OK"}},
        {"close", {L"닫기", L"Close"}},
        {"saved", {L"저장했습니다", L"Saved"}},
        {"copied", {L"클립보드에 복사했습니다", L"Copied to clipboard"}},
        {"cleared", {L"모든 표시를 지웠습니다", L"Markup cleared"}},
        {"path.copied", {L"경로를 복사했습니다", L"Path copied"}},
        {"save.failed", {L"저장하지 못했습니다", L"Could not save"}},
        {"load.failed", {L"파일을 열 수 없습니다", L"Could not open this file"}},
        {"load.codec", {L"이 형식을 읽을 코덱이 Windows에 없습니다", L"Windows has no codec for this format"}},
        {"loading", {L"불러오는 중", L"Loading"}},
        {"loading.cad", {L"CAD 형상을 메쉬로 변환하는 중", L"Tessellating CAD geometry"}},
        {"annot.unsaved", {L"저장하지 않은 변경이 있습니다", L"You have unsaved changes"}},
        {"confirm.clear", {L"이 이미지의 모든 표시를 지울까요?", L"Clear all markup on this image?"}},
        {"v3d.reset", {L"보기 초기화 (0)", L"Reset view (0)"}},
        {"v3d.front", {L"정면 (1)", L"Front (1)"}},
        {"v3d.top", {L"윗면 (2)", L"Top (2)"}},
        {"v3d.right", {L"우측면 (3)", L"Right (3)"}},
        {"v3d.iso", {L"등각", L"Isometric"}},
        {"v3d.wire", {L"와이어프레임 (W)", L"Wireframe (W)"}},
        {"v3d.edges", {L"모서리 표시", L"Show edges"}},
        {"v3d.clay", {L"클레이 재질", L"Clay material"}},
        {"v3d.grid", {L"바닥 그리드 (G)", L"Ground grid (G)"}},
        {"v3d.bbox", {L"경계 상자 (B)", L"Bounding box (B)"}},
        {"v3d.rotate", {L"자동 회전 (Space)", L"Auto-rotate (Space)"}},
        {"v3d.upaxis", {L"위쪽 축 전환 (Y/Z)", L"Toggle up axis (Y/Z)"}},
        {"v3d.shot", {L"스크린샷 저장", L"Save screenshot"}},
        {"v3d.hint", {L"왼쪽 드래그 회전 · 오른쪽 드래그 이동 · 휠 확대/축소", L"Left-drag orbit · Right-drag pan · Wheel zoom"}},
        {"info.file", {L"파일", L"File"}},
        {"info.name", {L"이름", L"Name"}},
        {"info.folder", {L"폴더", L"Folder"}},
        {"info.size", {L"파일 크기", L"File size"}},
        {"info.modified", {L"수정한 날짜", L"Modified"}},
        {"info.type", {L"형식", L"Format"}},
        {"info.image", {L"이미지", L"Image"}},
        {"info.dimensions", {L"해상도", L"Dimensions"}},
        {"info.megapixels", {L"화소", L"Megapixels"}},
        {"info.dpi", {L"DPI", L"DPI"}},
        {"info.bitdepth", {L"색 깊이", L"Bit depth"}},
        {"info.frames", {L"프레임", L"Frames"}},
        {"info.zoom", {L"배율", L"Zoom"}},
        {"info.histogram", {L"히스토그램", L"Histogram"}},
        {"info.camera", {L"촬영 정보", L"Camera"}},
        {"info.make", {L"제조사", L"Make"}},
        {"info.cammodel", {L"카메라", L"Camera"}},
        {"info.lens", {L"렌즈", L"Lens"}},
        {"info.focal", {L"초점 거리", L"Focal length"}},
        {"info.aperture", {L"조리개", L"Aperture"}},
        {"info.shutter", {L"셔터 속도", L"Shutter"}},
        {"info.iso", {L"ISO", L"ISO"}},
        {"info.exposure", {L"노출 보정", L"Exposure bias"}},
        {"info.flash", {L"플래시", L"Flash"}},
        {"info.flash.on", {L"사용", L"Fired"}},
        {"info.flash.off", {L"사용 안 함", L"Off"}},
        {"info.wb", {L"화이트 밸런스", L"White balance"}},
        {"info.wb.auto", {L"자동", L"Auto"}},
        {"info.wb.manual", {L"수동", L"Manual"}},
        {"info.taken", {L"촬영 날짜", L"Date taken"}},
        {"info.gps", {L"위치", L"Location"}},
        {"info.software", {L"소프트웨어", L"Software"}},
        {"info.noexif", {L"촬영 정보가 없습니다", L"No camera information"}},
        {"info.model", {L"모델", L"Model"}},
        {"info.meshes", {L"메쉬", L"Meshes"}},
        {"info.vertices", {L"정점", L"Vertices"}},
        {"info.triangles", {L"삼각형", L"Triangles"}},
        {"info.materials", {L"재질", L"Materials"}},
        {"info.extent", {L"크기 (X × Y × Z)", L"Extent (X × Y × Z)"}},
        {"info.showFolder", {L"폴더에서 보기", L"Show in folder"}},
        {"info.copyPath", {L"경로 복사", L"Copy path"}},
        {"edit.crop", {L"자르기 (C)", L"Crop (C)"}},
        {"edit.resize", {L"크기 조절 (R)", L"Resize (R)"}},
        {"edit.adjust", {L"색 보정 (M)", L"Adjust (M)"}},
        {"edit.undo", {L"편집 취소", L"Undo edit"}},
        {"edit.free", {L"자유", L"Free"}},
        {"edit.original", {L"원본 비율", L"Original"}},
        {"edit.width", {L"너비", L"Width"}},
        {"edit.height", {L"높이", L"Height"}},
        {"edit.keep", {L"비율 유지", L"Keep ratio"}},
        {"edit.percent", {L"비율", L"Scale"}},
        {"adj.exposure", {L"노출", L"Exposure"}},
        {"adj.contrast", {L"대비", L"Contrast"}},
        {"adj.highlights", {L"밝은 영역", L"Highlights"}},
        {"adj.shadows", {L"어두운 영역", L"Shadows"}},
        {"adj.saturation", {L"채도", L"Saturation"}},
        {"adj.vibrance", {L"생동감", L"Vibrance"}},
        {"adj.temperature", {L"색온도", L"Temperature"}},
        {"adj.tint", {L"색조", L"Tint"}},
        {"save.quality", {L"JPEG 품질", L"JPEG quality"}},
        {"save.keepmeta", {L"촬영 정보(EXIF) 유지", L"Keep camera info (EXIF)"}},
        {"set.appearance", {L"화면", L"Appearance"}},
        {"set.accent", {L"강조 색상", L"Accent colour"}},
        {"set.accent.system", {L"운영체제 색상", L"System accent"}},
        {"set.accent.brand", {L"DeepImage 기본", L"DeepImage default"}},
        {"set.viewer", {L"뷰어", L"Viewer"}},
        {"set.wheel", {L"마우스 휠", L"Mouse wheel"}},
        {"set.wheel.zoom", {L"확대/축소", L"Zoom"}},
        {"set.wheel.navigate", {L"이전/다음", L"Previous / next"}},
        {"set.imagebg", {L"이미지 배경", L"Image background"}},
        {"set.imagebg.plain", {L"단색", L"Plain"}},
        {"set.imagebg.checker", {L"체크무늬", L"Checkerboard"}},
        {"set.filmstrip", {L"하단 미리보기 목록 표시", L"Show filmstrip"}},
        {"set.grid", {L"3D 바닥 그리드 기본 표시", L"Show 3D ground grid by default"}},
        {"set.defaults", {L"기본 앱으로 설정…", L"Set as default app…"}},
        {"about.madeby", {L"제작", L"Made by"}},
        {"about.contact", {L"문의", L"Contact"}},
        {"about.version", {L"버전", L"Version"}},
        {"about.credits", {L"오픈소스", L"Open source"}},
        {"about.credits.text", {L"nomacs의 뷰어 구조와 DMIND 3D Thumbnail을 바탕으로 C++와 Direct2D·Direct3D로 새로 작성했습니다. OpenCascade · Assimp · stb_image · Lucide · Pretendard를 사용합니다.",
                                L"Re-written in C++ with Direct2D and Direct3D, based on the nomacs viewer architecture and DMIND 3D Thumbnail. Uses OpenCascade · Assimp · stb_image · Lucide · Pretendard."}},
        {"count", {L"{i} / {n}", L"{i} of {n}"}},
        {"minimize", {L"최소화", L"Minimize"}},
        {"maximize", {L"최대화", L"Maximize"}},
        {"restore", {L"이전 크기로", L"Restore"}},
        {"window.close", {L"닫기", L"Close"}},
    };
    return t;
}
Lang g_lang = Lang::En;
} // namespace

void setLang(Lang l) { g_lang = l; }
Lang lang() { return g_lang; }
const wchar_t* T(const char* key) {
    auto& t = table();
    auto it = t.find(key);
    if (it == t.end()) return L"?";
    return g_lang == Lang::Ko ? it->second.ko : it->second.en;
}
const wchar_t* TL(const char* key) {
    static std::unordered_map<std::string, std::wstring> cache[2];
    auto& c = cache[g_lang == Lang::Ko ? 0 : 1];
    auto it = c.find(key);
    if (it != c.end()) return it->second.c_str();
    std::wstring label, kbd;
    splitTip(T(key), label, kbd);
    return (c[key] = label).c_str();
}
std::wstring Tf(const char* key, const std::vector<std::pair<std::wstring, std::wstring>>& vars) {
    std::wstring s = T(key);
    for (auto& [k, v] : vars) {
        auto p = s.find(L"{" + k + L"}");
        if (p != std::wstring::npos) s.replace(p, k.size() + 2, v);
    }
    return s;
}
void splitTip(const std::wstring& s, std::wstring& label, std::wstring& kbd) {
    auto open = s.rfind(L" (");
    if (open != std::wstring::npos && !s.empty() && s.back() == L')') {
        label = s.substr(0, open);
        kbd = s.substr(open + 2, s.size() - open - 3);
    } else {
        label = s;
        kbd.clear();
    }
}

} // namespace dv
