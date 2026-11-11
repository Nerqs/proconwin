// procon_virtual: versão de console do ProconWin (o app da bandeja é o
// procon_tray). Faz o Pro Controller aparecer como um controle de Xbox 360.
//
// Requer o driver ProconWinXusb (modo usuário: funciona com o Secure Boot
// ligado e sem modo de teste). Veja driver/README.md.
//
// Uso:
//   procon_virtual [--layout posicao|nome] [--gyro off|mouse|stick] [--sens N]
//                  [--steam auto|sempre|nunca] [--luz-home on|off]
//
//   --layout posicao  B da Nintendo = A do Xbox (padrão)
//   --layout nome     A da Nintendo = A do Xbox
//   --gyro mouse      giroscópio move o mouse (jogos de tiro com mouse)
//   --gyro stick      giroscópio soma no analógico direito
//   --sens N          sensibilidade do giroscópio (mouse: pixels por grau;
//                     analógico: °/s para o analógico chegar no fim)
//   --steam auto      pausa enquanto um jogo aberto pela Steam estiver rodando
//                     (padrão), para o Steam Input usar o controle sozinho
//   --steam sempre    "Estou usando a Steam": fica sempre pausado
//   --steam nunca     nunca pausa
//   --luz-home off    apaga a luz do botão HOME (padrão: on)
//
// Durante o uso: Captura recalibra o drift (deixe o controle parado),
// segurar HOME por 3 s desliga o controle (Bluetooth), a tecla S liga e
// desliga o "Estou usando a Steam", a tecla L acende/apaga a luz do
// HOME, Ctrl+C sai.
//
// Se o controle desconectar ou for desligado, o programa espera ele voltar.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <conio.h>

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "engine.h"
#include "procon/settings.h"

using namespace procon;

namespace {

std::atomic<bool> g_quit{false};

void OnSignal(int) { g_quit = true; }

bool ParseArgs(int argc, char** argv, Settings& o) {
  float sens = -1;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&](std::string& out) {
      if (i + 1 >= argc) return false;
      out = argv[++i];
      return true;
    };
    std::string v;
    if (a == "--layout" && next(v)) {
      if (v == "nome") o.nintendo_layout = true;
      else if (v == "posicao") o.nintendo_layout = false;
      else return false;
    } else if (a == "--gyro" && next(v)) {
      if (v == "off") o.gyro = GyroMode::Off;
      else if (v == "mouse") o.gyro = GyroMode::Mouse;
      else if (v == "stick") o.gyro = GyroMode::RightStick;
      else return false;
    } else if (a == "--steam" && next(v)) {
      if (v == "auto") o.steam = SteamMode::Auto;
      else if (v == "sempre") o.steam = SteamMode::Always;
      else if (v == "nunca") o.steam = SteamMode::Never;
      else return false;
    } else if (a == "--luz-home" && next(v)) {
      if (v == "on") o.home_light = true;
      else if (v == "off") o.home_light = false;
      else return false;
    } else if (a == "--sens" && next(v)) {
      sens = std::strtof(v.c_str(), nullptr);
      if (sens <= 0) return false;
    } else {
      return false;
    }
  }
  if (sens > 0) {
    if (o.gyro == GyroMode::Mouse) o.mouse_pixels_per_degree = sens;
    else o.stick_full_speed_dps = sens;
  }
  return true;
}

const char* SteamModeName(SteamMode m) {
  switch (m) {
    case SteamMode::Auto: return "automático (pausa com jogo da Steam)";
    case SteamMode::Always: return "Estou usando a Steam (pausado)";
    case SteamMode::Never: return "nunca pausa";
  }
  return "";
}

}  // namespace

int main(int argc, char** argv) {
  SetConsoleOutputCP(CP_UTF8);
  Settings cfg;
  if (!ParseArgs(argc, argv, cfg)) {
    std::fprintf(stderr,
                 "Uso: procon_virtual [--layout posicao|nome] [--gyro off|mouse|stick] [--sens N]\n"
                 "                    [--steam auto|sempre|nunca] [--luz-home on|off]\n");
    return 1;
  }
  std::signal(SIGINT, OnSignal);

  std::printf("Layout: %s | giroscópio: %s\n",
              cfg.nintendo_layout ? "pelo nome" : "pela posição",
              cfg.gyro == GyroMode::Off     ? "desligado"
              : cfg.gyro == GyroMode::Mouse ? "mouse"
                                            : "analógico direito");
  std::printf("Steam: %s\n", SteamModeName(cfg.steam));
  std::printf("Captura = recalibrar giroscópio | segurar HOME 3 s = desligar o controle\n"
              "S = Estou usando a Steam (liga/desliga) | L = luz do HOME | Ctrl+C = sair\n");

  Engine engine;
  engine.Start(cfg, [](EngineEvent, const std::string& text) {
    std::printf("%s\n", text.c_str());
  });

  const SteamMode base = cfg.steam == SteamMode::Always ? SteamMode::Auto : cfg.steam;
  while (!g_quit) {
    while (_kbhit()) {
      int c = _getch();
      if (c == 's' || c == 'S') {
        cfg.steam = cfg.steam == SteamMode::Always ? base : SteamMode::Always;
        engine.SetSettings(cfg);
        std::printf("Steam: %s\n", SteamModeName(cfg.steam));
      } else if (c == 'l' || c == 'L') {
        cfg.home_light = !cfg.home_light;
        engine.SetSettings(cfg);
        std::printf("Luz do HOME: %s\n", cfg.home_light ? "acesa" : "apagada");
      }
    }
    Sleep(50);
  }

  std::printf("Encerrando...\n");
  engine.Stop();
  return 0;
}
