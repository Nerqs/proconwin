#include "procon/steam.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <unordered_set>

namespace procon {

namespace {

std::string Lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

// Programas que a própria Steam abre. Nem eles nem os filhos deles são jogos.
bool IsSteamInternal(const std::string& lower_name) {
  static const std::unordered_set<std::string> kInternal = {
      "steam.exe",           "steamwebhelper.exe",     "steamservice.exe",
      "gameoverlayui.exe",   "gameoverlayui64.exe",    "steamerrorreporter.exe",
      "steamerrorreporter64.exe", "steam_monitor.exe", "streaming_client.exe",
      "steamsysinfo.exe",    "steamxboxutil.exe",      "steamxboxutil64.exe",
      "x64launcher.exe",     "x86launcher.exe",        "crashhandler.exe",
      "crashhandler64.exe",  "conhost.exe",            "werfault.exe",
  };
  return kInternal.count(lower_name) != 0;
}

}  // namespace

bool FindSteamGame(const std::vector<ProcessEntry>& processes, std::string* game_name) {
  std::unordered_map<uint32_t, const ProcessEntry*> by_pid;
  for (const auto& p : processes) by_pid[p.pid] = &p;

  for (const auto& p : processes) {
    if (IsSteamInternal(Lower(p.name))) continue;
    // Sobe pelos pais até achar o steam.exe. Se passar por um programa
    // interno da Steam no caminho, não é jogo.
    const ProcessEntry* cur = &p;
    for (int depth = 0; depth < 16; ++depth) {
      auto it = by_pid.find(cur->parent_pid);
      // PID 0 ou pai que já fechou: o Windows reaproveita PIDs, então paramos.
      if (cur->parent_pid == 0 || cur->parent_pid == cur->pid || it == by_pid.end()) break;
      const ProcessEntry* parent = it->second;
      std::string parent_name = Lower(parent->name);
      if (parent_name == "steam.exe") {
        if (game_name) *game_name = p.name;
        return true;
      }
      if (IsSteamInternal(parent_name)) break;
      cur = parent;
    }
  }
  return false;
}

}  // namespace procon
