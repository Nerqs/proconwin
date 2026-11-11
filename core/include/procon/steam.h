// Convivência com a Steam.
//
// Quando um jogo é aberto pela Steam, o Steam Input lê o Pro Controller
// direto. Se o nosso controle virtual continuar ligado, o jogo recebe tudo
// duas vezes. Por isso o procon_virtual pausa enquanto houver um jogo da
// Steam rodando, e também quando o usuário liga "Estou usando a Steam".
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace procon {

struct ProcessEntry {
  uint32_t pid = 0;
  uint32_t parent_pid = 0;
  std::string name;  // nome do executável, ex.: "steam.exe"
};

// Algum processo foi aberto pela Steam e não é parte da própria Steam?
// Conta filhos, netos etc. do steam.exe (jogos que passam por um launcher
// também contam), ignorando os programas internos da Steam e o que eles
// abrem. Se achar, escreve o nome do jogo em game_name.
bool FindSteamGame(const std::vector<ProcessEntry>& processes, std::string* game_name = nullptr);

enum class SteamMode {
  Auto,    // pausa só com jogo aberto pela Steam
  Always,  // "Estou usando a Steam": sempre pausado
  Never,   // nunca pausa
};

// Decide se o controle virtual deve ficar pausado.
inline bool ShouldPause(SteamMode mode, bool steam_game_running) {
  switch (mode) {
    case SteamMode::Always: return true;
    case SteamMode::Never: return false;
    case SteamMode::Auto: return steam_game_running;
  }
  return false;
}

}  // namespace procon
