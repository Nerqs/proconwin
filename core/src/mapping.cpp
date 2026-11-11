#include "procon/mapping.h"

#include <algorithm>
#include <cmath>

namespace procon {

namespace {

constexpr size_t Idx(Source s) { return static_cast<size_t>(s); }

// Bit do Pro Controller para cada Source (na mesma ordem do enum).
constexpr uint32_t kSourceBits[] = {
    BtnA, BtnB, BtnX, BtnY, BtnL, BtnR, BtnZL, BtnZR, BtnMinus, BtnPlus,
    BtnLStick, BtnRStick, BtnHome, BtnCapture, BtnUp, BtnDown, BtnLeft, BtnRight,
};
static_assert(sizeof(kSourceBits) / sizeof(kSourceBits[0]) == Idx(Source::Count),
              "kSourceBits precisa ter um item por Source");

ButtonMap CommonMap() {
  ButtonMap m{};
  m.fill(Target::None);
  m[Idx(Source::L)] = Target::LB;
  m[Idx(Source::R)] = Target::RB;
  m[Idx(Source::ZL)] = Target::LT;
  m[Idx(Source::ZR)] = Target::RT;
  m[Idx(Source::Minus)] = Target::Back;
  m[Idx(Source::Plus)] = Target::Start;
  m[Idx(Source::LStick)] = Target::LS;
  m[Idx(Source::RStick)] = Target::RS;
  m[Idx(Source::Home)] = Target::Guide;
  m[Idx(Source::Capture)] = Target::None;  // livre para o usuário mapear
  m[Idx(Source::Up)] = Target::DpadUp;
  m[Idx(Source::Down)] = Target::DpadDown;
  m[Idx(Source::Left)] = Target::DpadLeft;
  m[Idx(Source::Right)] = Target::DpadRight;
  return m;
}

}  // namespace

ButtonMap DefaultPositionalMap() {
  ButtonMap m = CommonMap();
  m[Idx(Source::B)] = Target::A;  // baixo
  m[Idx(Source::A)] = Target::B;  // direita
  m[Idx(Source::Y)] = Target::X;  // esquerda
  m[Idx(Source::X)] = Target::Y;  // cima
  return m;
}

ButtonMap NintendoLabelMap() {
  ButtonMap m = CommonMap();
  m[Idx(Source::A)] = Target::A;
  m[Idx(Source::B)] = Target::B;
  m[Idx(Source::X)] = Target::X;
  m[Idx(Source::Y)] = Target::Y;
  return m;
}

int16_t AxisToXbox(float v) {
  v = std::clamp(v, -1.0f, 1.0f);
  return static_cast<int16_t>(std::lround(v * 32767.0f));
}

XboxState MapToXbox(const InputState& in, const ButtonMap& map) {
  XboxState out;
  for (size_t i = 0; i < map.size(); ++i) {
    if (!(in.buttons & kSourceBits[i])) continue;
    switch (map[i]) {
      case Target::None: break;
      case Target::A: out.buttons |= xbtn::A; break;
      case Target::B: out.buttons |= xbtn::B; break;
      case Target::X: out.buttons |= xbtn::X; break;
      case Target::Y: out.buttons |= xbtn::Y; break;
      case Target::LB: out.buttons |= xbtn::LeftShoulder; break;
      case Target::RB: out.buttons |= xbtn::RightShoulder; break;
      case Target::LT: out.left_trigger = 255; break;   // ZL/ZR são digitais
      case Target::RT: out.right_trigger = 255; break;
      case Target::Back: out.buttons |= xbtn::Back; break;
      case Target::Start: out.buttons |= xbtn::Start; break;
      case Target::LS: out.buttons |= xbtn::LeftThumb; break;
      case Target::RS: out.buttons |= xbtn::RightThumb; break;
      case Target::Guide: out.buttons |= xbtn::Guide; break;
      case Target::DpadUp: out.buttons |= xbtn::DpadUp; break;
      case Target::DpadDown: out.buttons |= xbtn::DpadDown; break;
      case Target::DpadLeft: out.buttons |= xbtn::DpadLeft; break;
      case Target::DpadRight: out.buttons |= xbtn::DpadRight; break;
      case Target::Count: break;
    }
  }
  out.lx = AxisToXbox(in.lx);
  out.ly = AxisToXbox(in.ly);
  out.rx = AxisToXbox(in.rx);
  out.ry = AxisToXbox(in.ry);
  return out;
}

const char* SourceName(Source s) {
  static const char* names[] = {"A", "B", "X", "Y", "L", "R", "ZL", "ZR", "-", "+",
                                "L3", "R3", "HOME", "CAPTURA", "CIMA", "BAIXO", "ESQ", "DIR"};
  return names[Idx(s)];
}

const char* TargetName(Target t) {
  static const char* names[] = {"nada", "A", "B", "X", "Y", "LB", "RB", "LT", "RT",
                                "BACK", "START", "LS", "RS", "GUIDE",
                                "CIMA", "BAIXO", "ESQ", "DIR"};
  return names[static_cast<size_t>(t)];
}

std::string XboxButtonsToString(uint16_t b) {
  struct Name { uint16_t bit; const char* name; };
  static const Name names[] = {
      {xbtn::A, "A"}, {xbtn::B, "B"}, {xbtn::X, "X"}, {xbtn::Y, "Y"},
      {xbtn::LeftShoulder, "LB"}, {xbtn::RightShoulder, "RB"},
      {xbtn::Back, "BACK"}, {xbtn::Start, "START"}, {xbtn::Guide, "GUIDE"},
      {xbtn::LeftThumb, "LS"}, {xbtn::RightThumb, "RS"},
      {xbtn::DpadUp, "CIMA"}, {xbtn::DpadDown, "BAIXO"},
      {xbtn::DpadLeft, "ESQ"}, {xbtn::DpadRight, "DIR"},
  };
  std::string s;
  for (const auto& n : names) {
    if (b & n.bit) {
      if (!s.empty()) s += ' ';
      s += n.name;
    }
  }
  return s.empty() ? "-" : s;
}

}  // namespace procon
