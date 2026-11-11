#include "engine.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <chrono>
#include <memory>

#include "processes.h"
#include "procon/controller.h"
#include "procon/gyro.h"
#include "procon/mapping.h"
#include "procon/protocol.h"
#include "procon/steam.h"
#include "xusb_feeder.h"

namespace procon {

namespace {

using Clock = std::chrono::steady_clock;

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

Engine::~Engine() { Stop(); }

void Engine::Start(const Settings& s, Listener listener) {
  Stop();
  {
    std::lock_guard<std::mutex> l(mu_);
    settings_ = s;
    status_ = EngineStatus{};
  }
  listener_ = std::move(listener);
  stop_ = false;
  ++settings_version_;
  thread_ = std::thread([this] { Run(); });
}

void Engine::Stop() {
  stop_ = true;
  if (thread_.joinable()) thread_.join();
}

void Engine::SetSettings(const Settings& s) {
  {
    std::lock_guard<std::mutex> l(mu_);
    settings_ = s;
  }
  ++settings_version_;
}

Settings Engine::GetSettings() const {
  std::lock_guard<std::mutex> l(mu_);
  return settings_;
}

EngineStatus Engine::GetStatus() const {
  std::lock_guard<std::mutex> l(mu_);
  return status_;
}

void Engine::Emit(EngineEvent e, const std::string& text) {
  if (listener_) listener_(e, text);
}

void Engine::Run() {
  Settings cfg;
  unsigned seen_version = ~0u;
  ButtonMap map{};
  GyroCalibrator gyro_cal;
  GyroOutput gyro_out(GyroSettings{});

  std::unique_ptr<Controller> pad;
  XusbFeeder virt;
  bool steam_game = false;
  std::string steam_game_name;
  bool paused = false;
  bool waiting_msg_shown = false;
  bool driver_error_shown = false;
  auto next_steam_check = Clock::now();
  auto next_open_try = Clock::now();
  auto next_driver_try = Clock::now();

  constexpr float kDeadzone = 0.08f;
  InputState st;
  uint8_t buf[64];
  uint32_t prev_buttons = 0;
  auto last_report = Clock::now();
  auto last_rumble = Clock::now();
  bool rumbling = false;
  int current_player = -1;
  bool home_held = false;
  auto home_since = Clock::now();

  auto set_status = [&](auto&& fn) {
    std::lock_guard<std::mutex> l(mu_);
    fn(status_);
  };

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
    set_status([](EngineStatus& s) {
      s.connected = false;
      s.buttons = 0;
    });
  };

  while (!stop_) {
    auto now = Clock::now();

    // Configurações novas?
    if (settings_version_ != seen_version) {
      seen_version = settings_version_;
      Settings old = cfg;
      cfg = GetSettings();
      map = cfg.buttons;
      gyro_out.SetSettings(ToGyroSettings(cfg));
      gyro_out.Reset();
      if (pad && old.home_light != cfg.home_light) pad->SetHomeLight(cfg.home_light);
      if (old.steam != cfg.steam) next_steam_check = now;
    }

    if (recalibrate_.exchange(false)) {
      gyro_cal.StartManual();
      Emit(EngineEvent::Info, "Recalibrando o giroscópio, deixe o controle parado...");
    }

    // A cada 2 s, procura jogo aberto pela Steam.
    if (cfg.steam == SteamMode::Auto && now >= next_steam_check) {
      std::string name;
      steam_game = FindSteamGame(ListProcesses(), &name);
      steam_game_name = name;
      next_steam_check = now + std::chrono::seconds(2);
    }

    bool want_pause = ShouldPause(cfg.steam, steam_game);
    if (want_pause != paused) {
      paused = want_pause;
      std::string game = cfg.steam == SteamMode::Always ? std::string() : steam_game_name;
      set_status([&](EngineStatus& s) {
        s.paused = paused;
        s.steam_game = paused ? game : std::string();
      });
      if (paused) {
        release();
        Emit(EngineEvent::Paused,
             game.empty() ? "Pausado: a Steam fica com o controle."
                          : "Pausado: jogo da Steam aberto (" + game +
                                "). O Steam Input fica com o controle.");
      } else {
        Emit(EngineEvent::Resumed, "Voltando: o controle de Xbox 360 virtual está ativo de novo.");
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
          Emit(EngineEvent::Info, "Esperando o Pro Controller (" + err + ")...");
          waiting_msg_shown = true;
        }
        next_open_try = now + std::chrono::seconds(1);
        continue;
      }
      pad = std::move(p);
      pad->SetHomeLight(cfg.home_light);
      waiting_msg_shown = false;
      last_report = Clock::now();
      bool usb = pad->IsUsb();
      set_status([&](EngineStatus& s) {
        s.connected = true;
        s.usb = usb;
      });
      Emit(EngineEvent::Connected,
           std::string("Pro Controller conectado (") + (usb ? "USB" : "Bluetooth") + ").");
    }

    // Controle de Xbox 360 virtual (aparece no Windows a partir daqui).
    if (!virt.IsOpen()) {
      if (now < next_driver_try) {
        Sleep(100);
        continue;
      }
      std::string err;
      if (!virt.Open(err)) {
        set_status([&](EngineStatus& s) { s.driver_error = err; });
        if (!driver_error_shown) Emit(EngineEvent::DriverError, err);
        driver_error_shown = true;
        next_driver_try = now + std::chrono::seconds(5);
        continue;
      }
      driver_error_shown = false;
      set_status([](EngineStatus& s) { s.driver_error.clear(); });
      Emit(EngineEvent::Info, "O Pro Controller agora é um controle de Xbox 360.");
    }

    int n = pad->ReadReport(buf, sizeof(buf), 100);
    if (n < 0) {
      release();
      Emit(EngineEvent::Disconnected, "O controle desconectou. Esperando ele voltar...");
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

    set_status([&](EngineStatus& s) {
      s.battery = st.battery_level;
      s.charging = st.charging;
      s.buttons = st.buttons;
    });

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
      Emit(EngineEvent::Info, "Recalibrando o giroscópio, deixe o controle parado...");
    }

    // Segurar HOME desliga o controle.
    if (!(st.buttons & BtnHome)) {
      home_held = false;
    } else if (!home_held) {
      home_held = true;
      home_since = now;
    } else if (now - home_since >= std::chrono::seconds(3)) {
      if (pad->IsUsb()) {
        Emit(EngineEvent::Info,
             "No cabo o controle não desliga (o USB mantém ele ligado). "
             "Desconecte o cabo para desligar.");
        home_held = false;
        home_since = now;
      } else {
        if (rumbling) pad->SendRumble(kRumbleNeutral, kRumbleNeutral);
        rumbling = false;
        pad->PowerOff();
        release();
        Emit(EngineEvent::PoweredOff, "Controle desligado. Aperte um botão nele para ligar de novo.");
        waiting_msg_shown = true;
        next_open_try = Clock::now() + std::chrono::seconds(2);
        continue;
      }
    }

    XboxState xs = MapToXbox(st, map);
    if (cfg.gyro == GyroMode::RightStick) {
      xs.rx = AxisToXbox(st.rx + gyro_out.StickX());
      xs.ry = AxisToXbox(st.ry + gyro_out.StickY());
    } else if (cfg.gyro == GyroMode::Mouse) {
      int dx, dy;
      gyro_out.TakeMouseDelta(dx, dy);
      MoveMouse(dx, dy);
    }
    virt.Submit(ToReport(xs));
  }

  release();
}

}  // namespace procon
