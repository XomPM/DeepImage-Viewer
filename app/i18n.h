#pragma once
#include <string>
#include <vector>
#include <utility>

namespace dv {
enum class Lang { Ko, En };
void setLang(Lang l);
Lang lang();
const wchar_t* T(const char* key);
const wchar_t* TL(const char* key); // label without the trailing "(shortcut)"
std::wstring Tf(const char* key, const std::vector<std::pair<std::wstring, std::wstring>>& vars);
void splitTip(const std::wstring& s, std::wstring& label, std::wstring& kbd);
}
