// Mapeamento dos botões do Pro Controller para um controle de Xbox 360.
#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "procon/protocol.h"

namespace procon {

// Mesmos valores do XINPUT_GAMEPAD do Windows, copiados aqui para o núcleo
// não depender do Windows (e poder ser testado em qualquer sistema).
namespace xbtn {
constexpr uint16_t DpadUp = 0x0001, DpadDown = 0x0002, DpadLeft = 0x0004, DpadRight = 0x0008;
constexpr uint16_t Start = 0x0010, Back = 0x0020, LeftThumb = 0x0040, RightThumb = 0x0080;
constexpr uint16_t LeftShoulder = 0x0100, RightShoulder = 0x0200, Guide = 0x0400;
constexpr uint16_t A = 0x1000, B = 0x2000, X = 0x4000, Y = 0x8000;
}  // namespace xbtn

struct XboxState {
  uint16_t buttons = 0;
  uint8_t left_trigger = 0, right_trigger = 0;  // 0 a 255
  int16_t lx = 0, ly = 0, rx = 0, ry = 0;       // -32768 a 32767, Y positivo = para cima
};

// Botões físicos do Pro Controller que podem ser remapeados.
enum class Source : uint8_t {
  A, B, X, Y, L, R, ZL, ZR, Minus, Plus, LStick, RStick, Home, Capture,
  Up, Down, Left, Right,
  Count
};

// Para onde cada botão físico vai no controle de Xbox.
enum class Target : uint8_t {
  None, A, B, X, Y, LB, RB, LT, RT, Back, Start, LS, RS, Guide,
  DpadUp, DpadDown, DpadLeft, DpadRight,
  Count
};

using ButtonMap = std::array<Target, static_cast<size_t>(Source::Count)>;

// Padrão: pela POSIÇÃO dos botões (o de baixo da Nintendo, B, vira o A do
// Xbox). É o que os jogos de PC esperam.
ButtonMap DefaultPositionalMap();
// Alternativa: pelo NOME (A da Nintendo vira A do Xbox).
ButtonMap NintendoLabelMap();

// Converte o estado do Pro Controller para o estado do Xbox usando o mapa.
XboxState MapToXbox(const InputState& in, const ButtonMap& map);

int16_t AxisToXbox(float v);
const char* SourceName(Source s);
const char* TargetName(Target t);

// Bit do Pro Controller (InputState::buttons) de cada botão físico.
uint32_t SourceBit(Source s);

// Nomes sem acento usados no arquivo de configuração ("zr", "rt", "nada").
const char* SourceId(Source s);
const char* TargetId(Target t);
bool SourceFromId(const std::string& id, Source& out);
bool TargetFromId(const std::string& id, Target& out);
std::string XboxButtonsToString(uint16_t buttons);

}  // namespace procon
