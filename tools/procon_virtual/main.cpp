// procon_virtual: faz o Pro Controller aparecer como um controle de Xbox 360.
//
// Requer um driver de controle virtual: o ProconWinBus deste projeto ou o
// ViGEmBus oficial (assinado, funciona com Secure Boot). Veja driver/README.md.
//
// Uso:
//   procon_virtual [--layout posicao|nome] [--gyro off|mouse|stick] [--sens N]
//
//   --layout posicao  B da Nintendo = A do Xbox (padrão)
//   --layout nome     A da Nintendo = A do Xbox
//   --gyro mouse      giroscópio move o mouse (jogos de tiro com mouse)
//   --gyro stick      giroscópio soma no analógico direito
//   --sens N          sensibilidade do giroscópio (mouse: pixels por grau;
//                     analógico: °/s para o analógico chegar no fim)
//
// Durante o uso: Captura recalibra o drift (deixe o controle parado),
// Ctrl+C sai.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <hidapi.h>

#include <ViGEm/Client.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "procon/controller.h"
#include "procon/gyro.h"
#include "procon/mapping.h"
#include "procon/protocol.h"

using namespace procon;
using Clock = std::chrono::steady_clock;

namespace {

std::atomic<bool> g_quit{false};
std::atomic<uint8_t> g_large_motor{0}, g_small_motor{0};
std::atomic<int> g_led{-1};

void OnSignal(int) { g_quit = true; }

VOID CALLBACK OnX360Notification(PVIGEM_CLIENT, PVIGEM_TARGET, UCHAR large_motor, UCHAR small_motor,
                                 UCHAR led, LPVOID) {
  g_large_motor = large_motor;
  g_small_motor = small_motor;
  g_led = led;
}

struct Options {
  bool nintendo_layout = false;
  GyroMode gyro = GyroMode::Off;
  float sens = -1;
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

XUSB_REPORT ToReport(const XboxState& s) {
  XUSB_REPORT r{};
  r.wButtons = s.buttons;
  r.bLeftTrigger = s.left_trigger;
  r.bRightTrigger = s.right_trigger;
  r.sThumbLX = s.lx;
  r.sThumbLY = s.ly;
  r.sThumbRX = s.rx;
  r.sThumbRY = s.ry;
  return r;
}

}  // namespace

int main(int argc, char** argv) {
  SetConsoleOutputCP(CP_UTF8);
  Options opt;
  if (!ParseArgs(argc, argv, opt)) {
    std::fprintf(stderr,
                 "Uso: procon_virtual [--layout posicao|nome] [--gyro off|mouse|stick] [--sens N]\n");
    return 1;
  }
  std::signal(SIGINT, OnSignal);

  // 1. Driver do controle virtual.
  PVIGEM_CLIENT client = vigem_alloc();
  if (!client) {
    std::fprintf(stderr, "Sem memória para o cliente do driver.\n");
    return 1;
  }
  VIGEM_ERROR err = vigem_connect(client);
  if (!VIGEM_SUCCESS(err)) {
    std::fprintf(stderr,
                 "Não encontrei nenhum driver de controle virtual (erro 0x%08X).\n"
                 "Instale o ViGEmBus oficial ou o ProconWinBus (veja driver/README.md).\n",
                 static_cast<unsigned>(err));
    vigem_free(client);
    return 1;
  }

  // 2. Pro Controller.
  if (hid_init() != 0) {
    std::fprintf(stderr, "Falha ao iniciar o hidapi.\n");
    return 1;
  }
  int exit_code = 0;
  {
    Controller pad;
    std::string perr;
    if (!pad.Open(perr) || !pad.Initialize(perr)) {
      std::fprintf(stderr, "%s\n", perr.c_str());
      vigem_disconnect(client);
      vigem_free(client);
      hid_exit();
      return 1;
    }

    // 3. Controle de Xbox 360 virtual.
    PVIGEM_TARGET x360 = vigem_target_x360_alloc();
    err = vigem_target_add(client, x360);
    if (!VIGEM_SUCCESS(err)) {
      std::fprintf(stderr, "O driver não criou o controle virtual (erro 0x%08X).\n",
                   static_cast<unsigned>(err));
      vigem_target_free(x360);
      vigem_disconnect(client);
      vigem_free(client);
      hid_exit();
      return 1;
    }
    vigem_target_x360_register_notification(client, x360, &OnX360Notification, nullptr);

    std::printf("Driver: %s\n", vigem_connected_bus_kind() == VIGEM_BUS_KIND_PROCONWIN
                                     ? "ProconWinBus (deste projeto)"
                                     : "ViGEmBus oficial");
    std::printf("Pro Controller (%s) agora é um controle de Xbox 360.\n",
                pad.IsUsb() ? "USB" : "Bluetooth");
    std::printf("Layout: %s | giroscópio: %s\n",
                opt.nintendo_layout ? "pelo nome" : "pela posição",
                opt.gyro == GyroMode::Off     ? "desligado"
                : opt.gyro == GyroMode::Mouse ? "mouse"
                                              : "analógico direito");
    std::printf("Captura = recalibrar giroscópio | Ctrl+C = sair\n");

    const ButtonMap map = opt.nintendo_layout ? NintendoLabelMap() : DefaultPositionalMap();
    GyroSettings gs;
    gs.mode = opt.gyro;
    if (opt.sens > 0) {
      if (gs.mode == GyroMode::Mouse) gs.mouse_pixels_per_degree = opt.sens;
      else gs.stick_full_speed_dps = opt.sens;
    }
    GyroCalibrator gyro_cal;
    GyroOutput gyro_out(gs);

    constexpr float kDeadzone = 0.08f;
    InputState st;
    uint8_t buf[64];
    uint32_t prev_buttons = 0;
    auto last_report = Clock::now();
    auto last_rumble = Clock::now();
    bool rumbling = false;
    int current_led = -1;

    while (!g_quit) {
      int n = pad.ReadReport(buf, sizeof(buf), 100);
      if (n < 0) {
        std::fprintf(stderr, "O controle desconectou.\n");
        exit_code = 2;
        break;
      }
      auto now = Clock::now();

      // Rumble vindo do jogo: motor forte = frequência baixa, fraco = alta.
      uint8_t big_m = g_large_motor, little_m = g_small_motor;
      if ((big_m || little_m) && now - last_rumble > std::chrono::milliseconds(15)) {
        auto side = EncodeRumble(320.0f, little_m / 255.0f, 160.0f, big_m / 255.0f);
        pad.SendRumble(side, side);
        last_rumble = now;
        rumbling = true;
      } else if (!big_m && !little_m && rumbling) {
        pad.SendRumble(kRumbleNeutral, kRumbleNeutral);
        rumbling = false;
      }

      // LED do jogador que o Windows atribuiu ao controle virtual.
      int led = g_led;
      if (led >= 0 && led != current_led) {
        pad.SetPlayerLights(static_cast<uint8_t>(1u << (led % 4)));
        current_led = led;
      }

      if (n == 0 || buf[0] != kInputFull) continue;
      if (!ParseFullReport(buf, static_cast<size_t>(n), pad.Cal(), kDeadzone, st)) continue;

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

      XboxState xs = MapToXbox(st, map);
      if (gs.mode == GyroMode::RightStick) {
        xs.rx = AxisToXbox(st.rx + gyro_out.StickX());
        xs.ry = AxisToXbox(st.ry + gyro_out.StickY());
      } else if (gs.mode == GyroMode::Mouse) {
        int dx, dy;
        gyro_out.TakeMouseDelta(dx, dy);
        MoveMouse(dx, dy);
      }
      XUSB_REPORT report = ToReport(xs);
      vigem_target_x360_update(client, x360, report);
    }

    vigem_target_x360_unregister_notification(x360);
    vigem_target_remove(client, x360);
    vigem_target_free(x360);
    std::printf("Encerrando...\n");
  }
  vigem_disconnect(client);
  vigem_free(client);
  hid_exit();
  return exit_code;
}
