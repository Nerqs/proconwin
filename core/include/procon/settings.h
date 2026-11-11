// Configurações do usuário, salvas num arquivo de texto simples:
//
//   # comentário
//   layout = posicao
//   giroscopio = mouse
//
// Chaves desconhecidas e valores inválidos são ignorados (fica o padrão),
// para um arquivo antigo ou editado à mão nunca impedir o programa de abrir.
#pragma once

#include <string>

#include "procon/gyro.h"
#include "procon/steam.h"

namespace procon {

struct Settings {
  bool nintendo_layout = false;  // false: pela posição (B da Nintendo = A do Xbox)
  GyroMode gyro = GyroMode::Off;
  float mouse_pixels_per_degree = 20.0f;
  float stick_full_speed_dps = 360.0f;
  bool gyro_invert_x = false;
  bool gyro_invert_y = false;
  SteamMode steam = SteamMode::Auto;
  bool home_light = true;

  bool operator==(const Settings& o) const;
  bool operator!=(const Settings& o) const { return !(*this == o); }
};

std::string SerializeSettings(const Settings& s);
Settings ParseSettings(const std::string& text);

// Ajustes do giroscópio que saem destas configurações.
GyroSettings ToGyroSettings(const Settings& s);

}  // namespace procon
