// Motor do ProconWin: lê o Pro Controller, alimenta o controle de Xbox 360
// virtual, cuida do giroscópio, da Steam e da reconexão. Roda numa thread
// própria; o programa de console e o app da bandeja só mostram e mudam as
// configurações.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include "procon/settings.h"

namespace procon {

struct EngineStatus {
  bool connected = false;  // Pro Controller conectado e ativo
  bool usb = false;
  int battery = 0;  // 0 a 8 (8 = cheia)
  bool charging = false;
  uint32_t buttons = 0;  // botões do Pro Controller apertados agora (bits Btn*)
  bool paused = false;  // pausado por causa da Steam
  bool pc_mode = false;  // modo computador ligado (mouse e teclado)
  std::string steam_game;  // jogo que causou a pausa (se houver)
  std::string driver_error;  // vazio se o driver está ok

  // Diagnóstico (para descobrir onde a entrada para).
  uint64_t reports = 0;         // relatórios do Pro Controller lidos
  uint64_t submits_ok = 0;      // estados entregues ao driver
  uint64_t submits_failed = 0;  // entregas recusadas pelo driver
  unsigned long last_error = 0; // erro do Windows na última entrega recusada
  uint16_t xbox_buttons = 0;    // botões do Xbox no último estado entregue
  int16_t xbox_lx = 0, xbox_ly = 0;
  int player = -1;              // jogador que o XInput deu ao controle virtual
};

enum class EngineEvent {
  Connected,
  Disconnected,
  PoweredOff,
  Paused,
  Resumed,
  DriverError,
  PcModeChanged,  // ligado ou desligado pelo controle (segurando Captura)
  Info,
};

// Texto do diagnóstico (bandeja e console mostram o mesmo).
std::string DiagnosticsText(const EngineStatus& st, const Settings& cfg);

class Engine {
 public:
  // Chamado da thread do motor. Deve retornar rápido.
  using Listener = std::function<void(EngineEvent, const std::string& text)>;

  Engine() = default;
  ~Engine();
  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;

  void Start(const Settings& s, Listener listener);
  void Stop();

  void SetSettings(const Settings& s);
  Settings GetSettings() const;
  EngineStatus GetStatus() const;
  void RecalibrateGyro() { recalibrate_ = true; }

 private:
  void Run();
  void Emit(EngineEvent e, const std::string& text);

  Listener listener_;
  std::thread thread_;
  std::atomic<bool> stop_{false};
  std::atomic<bool> recalibrate_{false};
  std::atomic<unsigned> settings_version_{0};
  mutable std::mutex mu_;
  Settings settings_;
  EngineStatus status_;
};

}  // namespace procon
