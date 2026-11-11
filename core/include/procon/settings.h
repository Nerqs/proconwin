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
#include "procon/mapping.h"
#include "procon/steam.h"

namespace procon {

struct Settings {
  // Para onde vai cada botão. Padrão: pela posição (B da Nintendo = A do Xbox).
  ButtonMap buttons = DefaultPositionalMap();
  GyroMode gyro = GyroMode::Off;
  float mouse_pixels_per_degree = 20.0f;
  float stick_full_speed_dps = 360.0f;
  bool gyro_invert_x = false;
  bool gyro_invert_y = false;
  SteamMode steam = SteamMode::Auto;
  bool home_light = true;
  // Perfil em uso (vazio = nenhum). Só o nome; os perfis ficam em arquivos
  // separados (veja SaveProfile/LoadProfile no app).
  std::string profile;

  bool operator==(const Settings& o) const;
  bool operator!=(const Settings& o) const { return !(*this == o); }
};

std::string SerializeSettings(const Settings& s);
Settings ParseSettings(const std::string& text);

// Ajustes do giroscópio que saem destas configurações.
GyroSettings ToGyroSettings(const Settings& s);

// Um perfil guarda o que muda de jogo para jogo: botões e giroscópio.
// Steam, luz do HOME e o nome do perfil em uso continuam globais.
void ApplyProfile(const Settings& profile, Settings& current);

// Nome de perfil seguro para virar nome de arquivo: tira caracteres que o
// Windows não aceita e espaços nas pontas. Pode retornar vazio.
std::string SanitizeProfileName(const std::string& name);

}  // namespace procon
