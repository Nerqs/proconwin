// procon_virtual: faz o Pro Controller aparecer como um controle de Xbox 360.
//
// Requer o driver ProconWinXusb (modo usuário: funciona com o Secure Boot
// ligado e sem modo de teste). Veja driver/README.md.
//
// Uso:
//   procon_virtual [--layout posicao|nome] [--gyro off|mouse|stick] [--sens N]
//                  [--steam auto|sempre|nunca]
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
//
// Durante o uso: Captura recalibra o drift (deixe o controle parado),
// segurar HOME por 3 s desliga o controle (Bluetooth), a tecla S liga e
// desliga o "Estou usando a Steam", Ctrl+C sai.
//
// Se o controle desconectar ou for desligado, o programa espera ele voltar.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <conio.h>
#include <hidapi.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

#include "procon/controller.h"
#include "procon/gyro.h"
#include "procon/mapping.h"
#include "procon/protocol.h"
#include "procon/steam.h"
#include "processes.h"
#include "xusb_feeder.h"

using namespace procon;
using Clock = std::chrono::steady_clock;

namespace {

std::atomic<bool> g_quit{false};

void OnSignal(int) { g_quit = true; }

struct Options {
  bool nintendo_layout = false;
  GyroMode gyro = GyroMode::Off;
  float sens = -1;
  SteamMode steam = SteamMode::Auto;
};

bool ParseArgs(int argc, char** argv, Options& o) {
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
    } else if (a == "--sens" && next(v)) {
      o.sens = std::strtof(v.c_str(), nullptr);
      if (o.sens <= 0) return false;
    } else {
      return false;
    }
  }
  return true;
}

void MoveMouse(int dx, int dy) {
  if (dx == 0 && dy == 0) return;
  INPUT in{};
  in.type = INPUT_MOUSE;
  in.mi.dx = dx;
  in.mi.dy = dy;
  in.mi.dwFlags = MOUSEEVENTF_MOVE;
  SendInput(1, &in, sizeof(in));
}

ProconwinReport ToReport(const XboxState& s) {
  ProconwinReport r{};
  r.buttons = s.buttons;
  r.left_trigger = s.left_trigger;
  r.right_trigger = s.right_trigger;
  r.lx = s.lx;
  r.ly = s.ly;
  r.rx = s.rx;
  r.ry = s.ry;
  return r;
}

}  // namespace

const char* SteamModeName(SteamMode m) {
  switch (m) {
    case SteamMode::Auto: return "automático (pausa com jogo da Steam)";
    case SteamMode::Always: return "Estou usando a Steam (pausado)";
    case SteamMode::Never: return "nunca pausa";
  }
  return "";
}

int main(int argc, char** argv) {
  SetConsoleOutputCP(CP_UTF8);
  Options opt;
  if (!ParseArgs(argc, argv, opt)) {
    std::fprintf(stderr,
                 "Uso: procon_virtual [--layout posicao|nome] [--gyro off|mouse|stick] [--sens N]\n"
                 "                    [--steam auto|sempre|nunca]\n");
    return 1;
  }
  std::signal(SIGINT, OnSignal);

  if (hid_init() != 0) {
    std::fprintf(stderr, "Falha ao iniciar o hidapi.\n");
    return 1;
  }

  std::printf("Layout: %s | giroscópio: %s\n",
              opt.nintendo_layout ? "pelo nome" : "pela posição",
              opt.gyro == GyroMode::Off     ? "desligado"
              : opt.gyro == GyroMode::Mouse ? "mouse"
                                            : "analógico direito");
  std::printf("Steam: %s\n", SteamModeName(opt.steam));
  std::printf("Captura = recalibrar giroscópio | segurar HOME 3 s = desligar o controle\n"
              "S = Estou usando a Steam (liga/desliga) | Ctrl+C = sair\n");

  const ButtonMap map = opt.nintendo_layout ? NintendoLabelMap() : DefaultPositionalMap();
  GyroSettings gs;
  gs.mode = opt.gyro;
  if (opt.sens > 0) {
    if (gs.mode == GyroMode::Mouse) gs.mouse_pixels_per_degree = opt.sens;
    else gs.stick_full_speed_dps = opt.sens;
  }
  GyroCalibrator gyro_cal;
  GyroOutput gyro_out(gs);

  std::unique_ptr<Controller> pad;
  XusbFeeder virt;
  SteamMode steam_mode = opt.steam;
  bool steam_game = false;
  std::string steam_game_name;
  bool paused = false;
  bool waiting_msg_shown = false;
  auto next_steam_check = Clock::now();
  auto next_open_try = Clock::now();

  constexpr float kDeadzone = 0.08f;
  InputState st;
  uint8_t buf[64];
  uint32_t prev_buttons = 0;
  auto last_report = Clock::now();
  auto last_rumble = Clock::now();
  bool rumbling = false;
  int current_player = -1;
  bool warned_submit = false;
  bool home_held = false;
  auto home_since = Clock::now();

  // Fecha o controle virtual e solta o Pro Controller.
  auto release = [&]() {
    virt.Close();
    if (pad) {
      if (rumbling) pad->SendRumble(kRumbleNeutral, kRumbleNeutral);
      pad.reset();
    }
    rumbling = false;
    current_player = -1;
    prev_buttons = 0;
    home_held = false;
    gyro_out.Reset();
  };

  int exit_code = 0;
  while (!g_quit) {
    auto now = Clock::now();

    // Tecla S: "Estou usando a Steam".
    while (_kbhit()) {
      int c = _getch();
      if (c == 's' || c == 'S') {
        // Volta para o modo escolhido na linha de comando (ou automático).
        SteamMode base = opt.steam == SteamMode::Always ? SteamMode::Auto : opt.steam;
        steam_mode = steam_mode == SteamMode::Always ? base : SteamMode::Always;
        next_steam_check = now;
        std::printf("Steam: %s\n", SteamModeName(steam_mode));
      }
    }

    // A cada 2 s, procura jogo aberto pela Steam.
    if (steam_mode == SteamMode::Auto && now >= next_steam_check) {
      std::string name;
      bool found = FindSteamGame(ListProcesses(), &name);
      steam_game = found;
      steam_game_name = name;
      next_steam_check = now + std::chrono::seconds(2);
    }

    bool want_pause = ShouldPause(steam_mode, steam_game);
    if (want_pause != paused) {
      paused = want_pause;
      if (paused) {
        release();
        if (steam_mode == SteamMode::Always) {
          std::printf("Pausado: a Steam fica com o controle. Aperte S para voltar.\n");
        } else {
          std::printf("Pausado: jogo da Steam aberto (%s). O Steam Input fica com o controle.\n",
                      steam_game_name.c_str());
        }
      } else {
        std::printf("Voltando: o controle de Xbox 360 virtual está ativo de novo.\n");
        next_open_try = now;
        waiting_msg_shown = false;
      }
    }
    if (paused) {
      Sleep(200);
      continue;
    }

    // Conecta o Pro Controller (de novo, se ele caiu).
    if (!pad) {
      if (now < next_open_try) {
        Sleep(100);
        continue;
      }
      auto p = std::make_unique<Controller>();
      std::string err;
      if (!p->Open(err) || !p->Initialize(err)) {
        if (!waiting_msg_shown) {
          std::printf("Esperando o Pro Controller (%s)...\n", err.c_str());
          waiting_msg_shown = true;
        }
        next_open_try = now + std::chrono::seconds(1);
        continue;
      }
      pad = std::move(p);
      waiting_msg_shown = false;
      last_report = Clock::now();
      std::printf("Pro Controller conectado (%s).\n", pad->IsUsb() ? "USB" : "Bluetooth");
    }

    // Controle de Xbox 360 virtual (aparece no Windows a partir daqui).
    if (!virt.IsOpen()) {
      std::string err;
      if (!virt.Open(err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        exit_code = 1;
        break;
      }
      std::printf("O Pro Controller agora é um controle de Xbox 360.\n");
    }

    int n = pad->ReadReport(buf, sizeof(buf), 100);
    if (n < 0) {
      std::printf("O controle desconectou. Esperando ele voltar...\n");
      release();
      waiting_msg_shown = true;
      next_open_try = Clock::now() + std::chrono::seconds(1);
      continue;
    }
    now = Clock::now();

    // Rumble vindo do jogo: motor forte = frequência baixa, fraco = alta.
    uint8_t big_m = virt.LargeMotor(), little_m = virt.SmallMotor();
    if ((big_m || little_m) && now - last_rumble > std::chrono::milliseconds(15)) {
      auto side = EncodeRumble(320.0f, little_m / 255.0f, 160.0f, big_m / 255.0f);
      pad->SendRumble(side, side);
      last_rumble = now;
      rumbling = true;
    } else if (!big_m && !little_m && rumbling) {
      pad->SendRumble(kRumbleNeutral, kRumbleNeutral);
      rumbling = false;
    }

    // LED do jogador que o XInput atribuiu ao controle virtual.
    int player = virt.Player();
    if (player >= 0 && player != current_player) {
      pad->SetPlayerLights(static_cast<uint8_t>(1u << (player % 4)));
      current_player = player;
    }

    if (n == 0 || buf[0] != kInputFull) continue;
    if (!ParseFullReport(buf, static_cast<size_t>(n), pad->Cal(), kDeadzone, st)) continue;

    float dt = std::chrono::duration<float>(now - last_report).count();
    last_report = now;
    if (dt <= 0 || dt > 0.1f) dt = 1.0f / 66.0f;
    for (const auto& s : st.imu) {
      gyro_cal.AddSample(s, dt / 3);
      gyro_out.AddSample(gyro_cal.Correct(s), dt / 3);
    }

    uint32_t pressed = st.buttons & ~prev_buttons;
    prev_buttons = st.buttons;
    if (pressed & BtnCapture) {
      gyro_cal.StartManual();
      std::printf("Recalibrando o giroscópio, deixe o controle parado...\n");
    }

    // Segurar HOME desliga o controle.
    if (!(st.buttons & BtnHome)) {
      home_held = false;
    } else if (!home_held) {
      home_held = true;
      home_since = now;
    } else if (now - home_since >= std::chrono::seconds(3)) {
      if (pad->IsUsb()) {
        std::printf("No cabo o controle não desliga (o USB mantém ele ligado). "
                    "Desconecte o cabo para desligar.\n");
        home_held = false;
        home_since = now;
      } else {
        if (rumbling) pad->SendRumble(kRumbleNeutral, kRumbleNeutral);
        rumbling = false;
        pad->PowerOff();
        release();
        std::printf("Controle desligado. Aperte um botão nele para ligar de novo.\n");
        waiting_msg_shown = true;
        next_open_try = Clock::now() + std::chrono::seconds(2);
        continue;
      }
    }

    XboxState xs = MapToXbox(st, map);
    if (gs.mode == GyroMode::RightStick) {
      xs.rx = AxisToXbox(st.rx + gyro_out.StickX());
      xs.ry = AxisToXbox(st.ry + gyro_out.StickY());
    } else if (gs.mode == GyroMode::Mouse) {
      int dx, dy;
      gyro_out.TakeMouseDelta(dx, dy);
      MoveMouse(dx, dy);
    }
    if (!virt.Submit(ToReport(xs)) && !warned_submit) {
      std::fprintf(stderr, "O driver recusou o estado do controle (erro %lu).\n", GetLastError());
      warned_submit = true;
    }
  }

  std::printf("Encerrando...\n");
  release();
  hid_exit();
  return exit_code;
}
