#include "storage.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <shlobj.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

using namespace procon;
namespace fs = std::filesystem;

std::wstring Widen(const std::string& s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring w(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
  return w;
}

std::string Narrow(const std::wstring& w) {
  if (w.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0,
                              nullptr, nullptr);
  std::string s(static_cast<size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr,
                      nullptr);
  return s;
}

std::wstring ConfigDir() {
  PWSTR path = nullptr;
  std::wstring dir;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &path))) {
    dir = std::wstring(path) + L"\\ProconWin";
  }
  CoTaskMemFree(path);
  return dir;
}

namespace {

fs::path ProfilesDir() { return fs::path(ConfigDir()) / L"perfis"; }

fs::path ProfilePath(const std::string& name) {
  return ProfilesDir() / (Widen(name) + L".ini");
}

bool ReadFile(const fs::path& p, std::string& out) {
  std::ifstream f(p, std::ios::binary);
  if (!f) return false;
  std::stringstream ss;
  ss << f.rdbuf();
  out = ss.str();
  return true;
}

bool WriteFile(const fs::path& p, const std::string& text) {
  std::error_code ec;
  fs::create_directories(p.parent_path(), ec);
  // Escreve num arquivo temporário e troca, para não deixar um arquivo pela
  // metade se o PC desligar no meio.
  fs::path tmp = p;
  tmp += L".tmp";
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << text;
    if (!f) return false;
  }
  return MoveFileExW(tmp.c_str(), p.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}

}  // namespace

Settings LoadSettings() {
  std::string text;
  if (!ReadFile(fs::path(ConfigDir()) / L"config.ini", text)) return Settings{};
  return ParseSettings(text);
}

void SaveSettings(const Settings& s) {
  WriteFile(fs::path(ConfigDir()) / L"config.ini", SerializeSettings(s));
}

std::vector<std::string> ListProfiles() {
  std::vector<std::string> names;
  std::error_code ec;
  for (const auto& e : fs::directory_iterator(ProfilesDir(), ec)) {
    if (!e.is_regular_file(ec) || e.path().extension() != L".ini") continue;
    names.push_back(Narrow(e.path().stem().wstring()));
  }
  std::sort(names.begin(), names.end());
  return names;
}

bool LoadProfile(const std::string& name, Settings& out) {
  std::string text;
  if (SanitizeProfileName(name).empty() || !ReadFile(ProfilePath(name), text)) return false;
  out = ParseSettings(text);
  return true;
}

bool SaveProfile(const std::string& name, const Settings& s) {
  std::string clean = SanitizeProfileName(name);
  if (clean.empty()) return false;
  // O perfil guarda só botões e giroscópio (ApplyProfile ignora o resto).
  Settings p;
  ApplyProfile(s, p);
  return WriteFile(ProfilePath(clean), SerializeSettings(p));
}

bool DeleteProfile(const std::string& name) {
  std::error_code ec;
  return !SanitizeProfileName(name).empty() && fs::remove(ProfilePath(name), ec);
}
