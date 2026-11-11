#include "processes.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <tlhelp32.h>

#include <string>

namespace {

std::string ToUtf8(const wchar_t* w) {
  int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
  if (n <= 1) return {};
  std::string s(static_cast<size_t>(n - 1), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
  return s;
}

}  // namespace

std::vector<procon::ProcessEntry> ListProcesses() {
  std::vector<procon::ProcessEntry> out;
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE) return out;
  PROCESSENTRY32W pe{};
  pe.dwSize = sizeof(pe);
  if (Process32FirstW(snap, &pe)) {
    do {
      out.push_back({pe.th32ProcessID, pe.th32ParentProcessID, ToUtf8(pe.szExeFile)});
    } while (Process32NextW(snap, &pe));
  }
  CloseHandle(snap);
  return out;
}
