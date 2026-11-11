// Protótipo: conecta no Pro Controller, mostra todas as entradas calibradas,
// o mapeamento para Xbox 360 e o giroscópio já sem drift, e testa o rumble HD
// e os LEDs.
//
// Controles durante o teste:
//   A segurado  -> rumble grave (160 Hz)
//   B segurado  -> rumble agudo (320 Hz)
//   X segurado  -> rumble forte nas duas frequências
//   Direcional  -> muda o LED do jogador (1 a 4)
//   Captura     -> recalibra o drift do giroscópio (deixe o controle parado)
//   + e - juntos (ou Ctrl+C) -> sair
#include <hidapi.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <string>

#include "procon/controller.h"
#include "procon/gyro.h"
#include "procon/mapping.h"
#include "procon/protocol.h"

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

using namespace procon;
using Clock = std::chrono::steady_clock;

namespace {

std::atomic<bool> g_quit{false};
void OnSignal(int) { g_quit = true; }

void EnableAnsiConsole() {
#ifdef _WIN32
  SetConsoleOutputCP(CP_UTF8);
  HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
  DWORD mode = 0;
  if (GetConsoleMode(h, &mode)) {
    SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
  }
#endif
}

const char* BatteryText(int level) {
  switch (level) {
    case 8: return "cheia";
    case 6: return "média";
    case 4: return "baixa";
    case 2: return "crítica";
    default: return "vazia";
  }
}

void PrintStatic(const Controller& c) {
  const auto& i = c.Info();
  const auto& cal = c.Cal();
  std::printf("Conectado por %s\n", c.IsUsb() ? "USB" : "Bluetooth");
  std::printf("Firmware %d.%02d | tipo %d | MAC %s\n", i.fw_major, i.fw_minor, i.type,
              i.mac.c_str());
  std::printf("Cor do corpo #%02X%02X%02X | botões #%02X%02X%02X\n", i.body_color[0],
              i.body_color[1], i.body_color[2], i.button_color[0], i.button_color[1],
              i.button_color[2]);
  std::printf("Calibração: analógico esq. %s, dir. %s, IMU %s\n",
              cal.left_from_user ? "do usuário" : "de fábrica",
              cal.right_from_user ? "do usuário" : "de fábrica",
              cal.imu_from_user ? "do usuário" : "de fábrica");
  std::printf("  esq. centro (%d, %d)  dir. centro (%d, %d)\n", cal.left.x_center,
              cal.left.y_center, cal.right.x_center, cal.right.y_center);
  std::printf("\nA/B/X = rumble | direcional = LED | Captura = recalibrar giroscópio | + e - juntos = sair\n\n");
}

}  // namespace

int main() {
  EnableAnsiConsole();
  std::signal(SIGINT, OnSignal);

  if (hid_init() != 0) {
    std::fprintf(stderr, "Falha ao iniciar o hidapi.\n");
    return 1;
  }

  int exit_code = 0;
  {
    Controller pad;
    std::string err;
    if (!pad.Open(err) || !pad.Initialize(err)) {
      std::fprintf(stderr, "%s\n", err.c_str());
      hid_exit();
      return 1;
    }
    PrintStatic(pad);

    constexpr float kDeadzone = 0.08f;
    InputState st;
    uint8_t buf[64];
    uint8_t last_timer = 0;
    bool have_timer = false;
    long reports = 0, late = 0, total = 0;
    float avg_delta = 0;  // quantos "tiques" do timer passam entre relatórios
    double hz = 0;
    auto rate_start = Clock::now();
    auto last_draw = Clock::now();
    auto last_rumble = Clock::now();
    bool rumbling = false;
    uint32_t prev_buttons = 0;

    const ButtonMap map = DefaultPositionalMap();
    GyroCalibrator gyro_cal;
    GyroSettings gs;
    gs.mode = GyroMode::Mouse;  // só para mostrar na tela; não move o mouse
    GyroOutput gyro_out(gs);
    long mouse_total_x = 0, mouse_total_y = 0;
    auto last_report = Clock::now();
    bool first_report = true;

    while (!g_quit) {
      int n = pad.ReadReport(buf, sizeof(buf), 100);
      if (n < 0) {
        std::fprintf(stderr, "\nO controle desconectou.\n");
        exit_code = 2;
        break;
      }
      if (n == 0 || buf[0] != kInputFull) continue;
      if (!ParseFullReport(buf, static_cast<size_t>(n), pad.Cal(), kDeadzone, st)) continue;

      // Taxa de relatórios e atrasos. O timer do controle não sobe de 1 em 1
      // por relatório: ele avança vários tiques entre um e outro. Contamos como
      // atrasado o relatório que chega com bem mais tiques que a média.
      if (have_timer) {
        int delta = static_cast<uint8_t>(st.timer - last_timer);
        ++total;
        if (total > 30 && delta > avg_delta * 1.75f) ++late;
        avg_delta = total == 1 ? delta : avg_delta * 0.95f + delta * 0.05f;
      }
      last_timer = st.timer;
      have_timer = true;
      ++reports;
      auto now = Clock::now();
      double secs = std::chrono::duration<double>(now - rate_start).count();
      if (secs >= 1.0) {
        hz = reports / secs;
        reports = 0;
        rate_start = now;
      }

      // Giroscópio: cada relatório traz 3 amostras.
      float report_dt = std::chrono::duration<float>(now - last_report).count();
      last_report = now;
      if (first_report || report_dt > 0.1f) report_dt = 1.0f / 66.0f;
      first_report = false;
      std::array<float, 3> gyro_now{0, 0, 0};
      for (const auto& smp : st.imu) {
        gyro_cal.AddSample(smp, report_dt / 3);
        gyro_now = gyro_cal.Correct(smp);
        gyro_out.AddSample(gyro_now, report_dt / 3);
      }
      int mdx = 0, mdy = 0;
      gyro_out.TakeMouseDelta(mdx, mdy);
      mouse_total_x += mdx;
      mouse_total_y += mdy;

      // Sair com + e - juntos.
      if ((st.buttons & BtnPlus) && (st.buttons & BtnMinus)) break;

      // LED do jogador pelo direcional (só na borda de subida).
      uint32_t pressed = st.buttons & ~prev_buttons;
      if (pressed & BtnUp) pad.SetPlayerLights(0x01);
      if (pressed & BtnRight) pad.SetPlayerLights(0x02);
      if (pressed & BtnDown) pad.SetPlayerLights(0x04);
      if (pressed & BtnLeft) pad.SetPlayerLights(0x08);
      if (pressed & BtnCapture) {
        gyro_cal.StartManual();
        mouse_total_x = mouse_total_y = 0;
      }
      prev_buttons = st.buttons;

      // Rumble: precisa ser reenviado o tempo todo, senão o controle para.
      bool want = st.buttons & (BtnA | BtnB | BtnX);
      if (want && now - last_rumble > std::chrono::milliseconds(15)) {
        std::array<uint8_t, 4> side = kRumbleNeutral;
        if (st.buttons & BtnX) side = EncodeRumble(320.0f, 1.0f, 160.0f, 1.0f);
        else if (st.buttons & BtnA) side = EncodeRumble(320.0f, 0.0f, 160.0f, 0.6f);
        else if (st.buttons & BtnB) side = EncodeRumble(320.0f, 0.6f, 160.0f, 0.0f);
        pad.SendRumble(side, side);
        last_rumble = now;
        rumbling = true;
      } else if (!want && rumbling) {
        pad.SendRumble(kRumbleNeutral, kRumbleNeutral);
        rumbling = false;
      }

      // Desenha ~20 vezes por segundo.
      if (now - last_draw < std::chrono::milliseconds(50)) continue;
      last_draw = now;

      float ax = 0, ay = 0, az = 0;
      for (const auto& smp : st.imu) {
        ax += smp.accel_g[0] / 3; ay += smp.accel_g[1] / 3; az += smp.accel_g[2] / 3;
      }
      const XboxState xs = MapToXbox(st, map);
      const auto& bias = gyro_cal.Bias();
      const char* cal_status = gyro_cal.ManualInProgress() ? "calibrando, não mexa..."
                               : gyro_cal.HasBias()         ? "ok"
                                                            : "esperando o controle ficar parado";

      std::printf("\x1b[2K\rBotões: %s\n", ButtonsToString(st.buttons).c_str());
      std::printf("\x1b[2K\rAnalógico esq. % .2f % .2f (cru %4d %4d) | dir. % .2f % .2f (cru %4d %4d)\n",
                  st.lx, st.ly, st.raw_lx, st.raw_ly, st.rx, st.ry, st.raw_rx, st.raw_ry);
      std::printf("\x1b[2K\rXbox: %s | LT %3d RT %3d\n", XboxButtonsToString(xs.buttons).c_str(),
                  xs.left_trigger, xs.right_trigger);
      std::printf("\x1b[2K\rGiroscópio °/s  % 8.1f % 8.1f % 8.1f (sem drift)\n", gyro_now[0],
                  gyro_now[1], gyro_now[2]);
      std::printf("\x1b[2K\rDrift: % .2f % .2f % .2f °/s (%s)\n", bias[0], bias[1], bias[2],
                  cal_status);
      std::printf("\x1b[2K\rMouse acumulado: x %6ld  y %6ld (girar p/ direita = x sobe, inclinar p/ cima = y desce)\n",
                  mouse_total_x, mouse_total_y);
      std::printf("\x1b[2K\rAcelerômetro G  % 8.2f % 8.2f % 8.2f\n", ax, ay, az);
      std::printf("\x1b[2K\rBateria %s%s | %.0f relatórios/s | %.1f tiques/relatório | %ld atrasados\n",
                  BatteryText(st.battery_level), st.charging ? " (carregando)" : "", hz,
                  avg_delta, late);
      std::printf("\x1b[8A");  // volta 8 linhas para redesenhar no mesmo lugar
      std::fflush(stdout);
    }
    std::printf("\x1b[8B\nEncerrando...\n");
    // O destrutor do Controller para o rumble e devolve o estado do USB.
  }
  hid_exit();
  return exit_code;
}
