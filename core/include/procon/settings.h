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

#include "procon/desktop.h"
#include "procon/gyro.h"
#include "procon/mapping.h"
#include "procon/steam.h"

namespace procon {

struct Settings {
  // Para onde vai cada botão. Padrão: pela posição (B da Nintendo = A do Xbox).
  ButtonMap buttons = DefaultPositionalMap();
  GyroMode gyro = GyroMode::Off;
  // Sensibilidade do giroscópio (padrão = nível 5, veja GyroLevel*).
  float mouse_pixels_per_degree = 30.0f;
  float stick_full_speed_dps = 110.0f;
  bool gyro_invert_x = false;
  bool gyro_invert_y = false;
  SteamMode steam = SteamMode::Auto;
  bool home_light = true;
  bool rumble = true;  // vibração vinda dos jogos (e o aviso do modo computador)
  bool check_updates = true;  // o app procura versão nova no GitHub ao abrir
  // Modo computador: o controle vira mouse e teclado, e o controle de Xbox
  // virtual fica parado. É global (não muda com o perfil).
  bool pc_mode = false;
  PcMap pc_buttons = DefaultPcMap();
  float pc_mouse_speed = 1600.0f;  // pixels por segundo com o analógico no máximo
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

// Sensibilidade do giroscópio em níveis de 1 (mais lenta) a 10 (mais
// rápida), para o menu. Cada modo tem a sua escala.
constexpr int kGyroLevels = 10;
float MouseSensForLevel(int level);   // pixels por grau
float StickSpeedForLevel(int level);  // °/s para o analógico ir até o fim
// Nível mais próximo do valor salvo (o arquivo pode ter qualquer número).
int MouseLevelOf(float pixels_per_degree);
int StickLevelOf(float full_speed_dps);

// Ajustes do modo computador que saem destas configurações.
DesktopSettings ToDesktopSettings(const Settings& s);

// Um perfil guarda o que muda de jogo para jogo: botões e giroscópio.
// Steam, luz do HOME e o nome do perfil em uso continuam globais.
void ApplyProfile(const Settings& profile, Settings& current);

// Nome de perfil seguro para virar nome de arquivo: tira caracteres que o
// Windows não aceita e espaços nas pontas. Pode retornar vazio.
std::string SanitizeProfileName(const std::string& name);

}  // namespace procon
