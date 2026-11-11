#include "procon/settings.h"

#include <cstdlib>
#include <sstream>

namespace procon {

namespace {

std::string Trim(const std::string& s) {
  size_t b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) return {};
  size_t e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}

bool ParseBool(const std::string& v, bool& out) {
  if (v == "sim" || v == "on" || v == "1") { out = true; return true; }
  if (v == "nao" || v == "não" || v == "off" || v == "0") { out = false; return true; }
  return false;
}

bool ParsePositive(const std::string& v, float min, float max, float& out) {
  char* end = nullptr;
  float f = std::strtof(v.c_str(), &end);
  if (end == v.c_str() || *end != '\0' || !(f >= min && f <= max)) return false;
  out = f;
  return true;
}

const char* GyroName(GyroMode m) {
  switch (m) {
    case GyroMode::Off: return "desligado";
    case GyroMode::Mouse: return "mouse";
    case GyroMode::RightStick: return "analogico";
  }
  return "desligado";
}

const char* SteamName(SteamMode m) {
  switch (m) {
    case SteamMode::Auto: return "auto";
    case SteamMode::Always: return "sempre";
    case SteamMode::Never: return "nunca";
  }
  return "auto";
}

}  // namespace

bool Settings::operator==(const Settings& o) const {
  return buttons == o.buttons && profile == o.profile && gyro == o.gyro &&
         mouse_pixels_per_degree == o.mouse_pixels_per_degree &&
         stick_full_speed_dps == o.stick_full_speed_dps && gyro_invert_x == o.gyro_invert_x &&
         gyro_invert_y == o.gyro_invert_y && steam == o.steam && home_light == o.home_light;
}

std::string SerializeSettings(const Settings& s) {
  std::ostringstream o;
  o << "# Configurações do ProconWin. Pode editar à mão com o programa fechado.\n";
  o << "giroscopio = " << GyroName(s.gyro) << "\n";
  o << "sensibilidade_mouse = " << s.mouse_pixels_per_degree << "\n";
  o << "velocidade_analogico = " << s.stick_full_speed_dps << "\n";
  o << "inverter_x = " << (s.gyro_invert_x ? "sim" : "nao") << "\n";
  o << "inverter_y = " << (s.gyro_invert_y ? "sim" : "nao") << "\n";
  o << "steam = " << SteamName(s.steam) << "\n";
  o << "luz_home = " << (s.home_light ? "sim" : "nao") << "\n";
  if (!s.profile.empty()) o << "perfil = " << s.profile << "\n";
  o << "# Botões: botao.<botão da Nintendo> = <botão do Xbox>\n";
  for (size_t i = 0; i < s.buttons.size(); ++i) {
    o << "botao." << SourceId(static_cast<Source>(i)) << " = " << TargetId(s.buttons[i]) << "\n";
  }
  return o.str();
}

Settings ParseSettings(const std::string& text) {
  Settings s;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    size_t hash = line.find('#');
    if (hash != std::string::npos) line.resize(hash);
    size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::string key = Trim(line.substr(0, eq));
    std::string v = Trim(line.substr(eq + 1));

    if (key == "layout") {
      // Arquivos antigos: o layout define o mapa inteiro. Linhas botao.*
      // que vierem depois ajustam em cima dele.
      if (v == "nome") s.buttons = NintendoLabelMap();
      else if (v == "posicao") s.buttons = DefaultPositionalMap();
    } else if (key == "giroscopio") {
      if (v == "desligado") s.gyro = GyroMode::Off;
      else if (v == "mouse") s.gyro = GyroMode::Mouse;
      else if (v == "analogico") s.gyro = GyroMode::RightStick;
    } else if (key == "sensibilidade_mouse") {
      ParsePositive(v, 0.1f, 1000.0f, s.mouse_pixels_per_degree);
    } else if (key == "velocidade_analogico") {
      ParsePositive(v, 10.0f, 5000.0f, s.stick_full_speed_dps);
    } else if (key == "inverter_x") {
      ParseBool(v, s.gyro_invert_x);
    } else if (key == "inverter_y") {
      ParseBool(v, s.gyro_invert_y);
    } else if (key == "steam") {
      if (v == "auto") s.steam = SteamMode::Auto;
      else if (v == "sempre") s.steam = SteamMode::Always;
      else if (v == "nunca") s.steam = SteamMode::Never;
    } else if (key == "luz_home") {
      ParseBool(v, s.home_light);
    } else if (key == "perfil") {
      s.profile = SanitizeProfileName(v);
    } else if (key.rfind("botao.", 0) == 0) {
      Source src;
      Target dst;
      if (SourceFromId(key.substr(6), src) && TargetFromId(v, dst)) {
        s.buttons[static_cast<size_t>(src)] = dst;
      }
    }
  }
  return s;
}

void ApplyProfile(const Settings& p, Settings& c) {
  c.buttons = p.buttons;
  c.gyro = p.gyro;
  c.mouse_pixels_per_degree = p.mouse_pixels_per_degree;
  c.stick_full_speed_dps = p.stick_full_speed_dps;
  c.gyro_invert_x = p.gyro_invert_x;
  c.gyro_invert_y = p.gyro_invert_y;
}

std::string SanitizeProfileName(const std::string& name) {
  std::string out;
  for (char ch : name) {
    unsigned char c = static_cast<unsigned char>(ch);
    if (c < 32 || std::string("\\/:*?\"<>|#=").find(ch) != std::string::npos) continue;
    out += ch;
  }
  out = Trim(out);
  // "." e ".." não são nomes de arquivo válidos; nem nomes terminados em ponto.
  while (!out.empty() && out.back() == '.') out.pop_back();
  if (out.size() > 60) out.resize(60);
  return out;
}

GyroSettings ToGyroSettings(const Settings& s) {
  GyroSettings g;
  g.mode = s.gyro;
  g.mouse_pixels_per_degree = s.mouse_pixels_per_degree;
  g.stick_full_speed_dps = s.stick_full_speed_dps;
  g.invert_x = s.gyro_invert_x;
  g.invert_y = s.gyro_invert_y;
  return g;
}

}  // namespace procon
