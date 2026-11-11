// Modo computador: o Pro Controller vira mouse e teclado.
//
//   analógico direito  move o mouse
//   analógico esquerdo rola a página (vertical e horizontal)
//   botões             viram cliques ou teclas (mapa editável, PcMap)
//
// Esta parte não depende do Windows: calcula o que mandar (movimento, rolagem
// e eventos de apertar/soltar) e o motor converte em SendInput.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "procon/mapping.h"
#include "procon/protocol.h"

namespace procon {

enum class PcAction : uint8_t {
  None,
  LeftClick, RightClick, MiddleClick,
  Enter, Esc, Space, Backspace, Tab, Delete,
  Up, Down, Left, Right, PageUp, PageDown,
  Windows,      // menu Iniciar (segurar + outra tecla funciona como no teclado)
  AltTab,       // trocar de janela
  TaskView,     // Win+Tab, todas as janelas
  CloseWindow,  // Alt+F4
  Keyboard,     // teclado virtual (Win+Ctrl+O)
  Screenshot,   // recorte da tela (Win+Shift+S)
  VolumeUp, VolumeDown, Mute, PlayPause,
  Back, Forward,  // voltar e avançar do navegador
  Count
};

using PcMap = std::array<PcAction, static_cast<size_t>(Source::Count)>;

// ZL = clique esquerdo, ZR = clique direito, A = Enter, B = Esc, setas no
// direcional, + = Iniciar, - = todas as janelas, L3 = teclado virtual,
// R3 = clique do meio, L/R = voltar/avançar. HOME e Captura ficam livres
// (segurar Captura liga/desliga o modo; segurar HOME desliga o controle).
PcMap DefaultPcMap();

const char* PcActionId(PcAction a);  // nome no arquivo ("clique_esquerdo")
bool PcActionFromId(const std::string& id, PcAction& out);

// Teclas que repetem enquanto o botão está apertado (setas, apagar, volume).
bool PcActionRepeats(PcAction a);

struct PcEvent {
  enum Kind : uint8_t { Down, Up, Repeat };
  PcAction action;
  Kind kind;
  bool operator==(const PcEvent& o) const { return action == o.action && kind == o.kind; }
};

struct DesktopFrame {
  int mouse_dx = 0, mouse_dy = 0;  // pixels (Y positivo = para baixo, como no Windows)
  int wheel = 0, hwheel = 0;       // unidades do Windows (120 = um "clique" da rodinha)
  std::vector<PcEvent> events;
};

struct DesktopSettings {
  float mouse_speed = 1600.0f;   // pixels por segundo com o analógico no máximo
  float scroll_speed = 12.0f;    // cliques da rodinha por segundo no máximo
  float repeat_delay = 0.40f;    // segundos até começar a repetir
  float repeat_interval = 0.05f;
};

class DesktopMapper {
 public:
  // dt: segundos desde o último relatório. now: relógio em segundos.
  DesktopFrame Update(const InputState& in, const PcMap& map, const DesktopSettings& s, float dt,
                      double now);
  // Solta tudo que estiver apertado (ao desligar o modo ou perder o controle).
  DesktopFrame Release();

 private:
  static constexpr size_t kN = static_cast<size_t>(Source::Count);
  std::array<PcAction, kN> held_{};  // ação apertada por botão (None = solto)
  std::array<double, kN> next_repeat_{};
  float acc_x_ = 0, acc_y_ = 0, acc_wheel_ = 0, acc_hwheel_ = 0;
};

}  // namespace procon
