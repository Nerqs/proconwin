#include "procon/desktop.h"

#include <cmath>

namespace procon {

namespace {

constexpr size_t Idx(Source s) { return static_cast<size_t>(s); }

const char* const kIds[] = {
    "nada",       "clique_esquerdo", "clique_direito", "clique_meio", "enter",
    "esc",        "espaco",          "apagar",         "tab",         "delete",
    "cima",       "baixo",           "esquerda",       "direita",     "page_up",
    "page_down",  "windows",         "alt_tab",        "janelas",     "fechar_janela",
    "teclado",    "print",           "volume_mais",    "volume_menos", "mudo",
    "play_pausa", "voltar",          "avancar",
};
static_assert(sizeof(kIds) / sizeof(kIds[0]) == static_cast<size_t>(PcAction::Count), "kIds");

// Curva quadrática: movimento fino perto do centro, rápido no fim.
float Curve(float v) { return v * std::fabs(v); }

// Tira a parte inteira do acumulador (o resto fica para o próximo quadro).
int Take(float& acc) {
  int whole = static_cast<int>(acc);  // trunca em direção a zero
  acc -= static_cast<float>(whole);
  return whole;
}

}  // namespace

PcMap DefaultPcMap() {
  PcMap m{};
  m.fill(PcAction::None);
  m[Idx(Source::ZL)] = PcAction::LeftClick;
  m[Idx(Source::ZR)] = PcAction::RightClick;
  m[Idx(Source::A)] = PcAction::Enter;
  m[Idx(Source::B)] = PcAction::Esc;
  m[Idx(Source::X)] = PcAction::Backspace;
  m[Idx(Source::Y)] = PcAction::Space;
  m[Idx(Source::L)] = PcAction::Back;
  m[Idx(Source::R)] = PcAction::Forward;
  m[Idx(Source::Minus)] = PcAction::TaskView;
  m[Idx(Source::Plus)] = PcAction::Windows;
  m[Idx(Source::LStick)] = PcAction::Keyboard;
  m[Idx(Source::RStick)] = PcAction::MiddleClick;
  m[Idx(Source::Up)] = PcAction::Up;
  m[Idx(Source::Down)] = PcAction::Down;
  m[Idx(Source::Left)] = PcAction::Left;
  m[Idx(Source::Right)] = PcAction::Right;
  return m;
}

const char* PcActionId(PcAction a) { return kIds[static_cast<size_t>(a)]; }

bool PcActionFromId(const std::string& id, PcAction& out) {
  for (size_t i = 0; i < static_cast<size_t>(PcAction::Count); ++i) {
    if (id == kIds[i]) {
      out = static_cast<PcAction>(i);
      return true;
    }
  }
  return false;
}

bool PcActionRepeats(PcAction a) {
  switch (a) {
    case PcAction::Up: case PcAction::Down: case PcAction::Left: case PcAction::Right:
    case PcAction::PageUp: case PcAction::PageDown: case PcAction::Backspace:
    case PcAction::Delete: case PcAction::VolumeUp: case PcAction::VolumeDown:
      return true;
    default:
      return false;
  }
}

DesktopFrame DesktopMapper::Update(const InputState& in, const PcMap& map,
                                   const DesktopSettings& s, float dt, double now) {
  DesktopFrame f;

  for (size_t i = 0; i < kN; ++i) {
    bool down = (in.buttons & SourceBit(static_cast<Source>(i))) != 0;
    if (down && held_[i] == PcAction::None && map[i] != PcAction::None) {
      held_[i] = map[i];
      next_repeat_[i] = now + s.repeat_delay;
      f.events.push_back({held_[i], PcEvent::Down});
    } else if (!down && held_[i] != PcAction::None) {
      // Solta a ação que foi apertada, mesmo que o mapa tenha mudado no meio.
      f.events.push_back({held_[i], PcEvent::Up});
      held_[i] = PcAction::None;
    } else if (down && held_[i] != PcAction::None && PcActionRepeats(held_[i]) &&
               now >= next_repeat_[i]) {
      f.events.push_back({held_[i], PcEvent::Repeat});
      next_repeat_[i] = now + s.repeat_interval;
    }
  }

  // Analógico direito: mouse. Y do controle é positivo para cima; o da tela,
  // para baixo.
  acc_x_ += Curve(in.rx) * s.mouse_speed * dt;
  acc_y_ -= Curve(in.ry) * s.mouse_speed * dt;
  f.mouse_dx = Take(acc_x_);
  f.mouse_dy = Take(acc_y_);

  // Analógico esquerdo: rolagem. Rodinha positiva = para cima, como o analógico.
  acc_wheel_ += Curve(in.ly) * s.scroll_speed * 120.0f * dt;
  acc_hwheel_ += Curve(in.lx) * s.scroll_speed * 120.0f * dt;
  f.wheel = Take(acc_wheel_);
  f.hwheel = Take(acc_hwheel_);
  if (in.ly == 0) acc_wheel_ = 0;
  if (in.lx == 0) acc_hwheel_ = 0;
  if (in.rx == 0) acc_x_ = 0;
  if (in.ry == 0) acc_y_ = 0;
  return f;
}

DesktopFrame DesktopMapper::Release() {
  DesktopFrame f;
  for (size_t i = 0; i < kN; ++i) {
    if (held_[i] != PcAction::None) f.events.push_back({held_[i], PcEvent::Up});
    held_[i] = PcAction::None;
  }
  acc_x_ = acc_y_ = acc_wheel_ = acc_hwheel_ = 0;
  return f;
}

}  // namespace procon
