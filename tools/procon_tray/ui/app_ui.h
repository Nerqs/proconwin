// Interface da janela do ProconWin, desenhada com Dear ImGui.
//
// Esta parte não depende do Windows: quem hospeda (a janela Win32 com
// Direct3D 11, ou a ferramenta de prévia) implementa o UiModel e chama
// DrawApp a cada quadro.
#pragma once

#include <string>
#include <vector>

#include "engine.h"
#include "procon/settings.h"

struct ImFont;

namespace ui {

// Estado da atualização (espelha o updater do app).
struct UpdateView {
  enum State { Idle, Checking, UpToDate, Available, Downloading, Ready, Failed };
  State state = Idle;
  std::string current, latest, error;
  float progress = 0;
};

// O que a janela precisa do resto do programa.
class UiModel {
 public:
  virtual ~UiModel() = default;
  virtual procon::Settings Get() = 0;
  virtual void Apply(const procon::Settings& s) = 0;  // aplica, salva (e no perfil)
  virtual procon::EngineStatus Status() = 0;
  virtual std::string Diagnostics() = 0;

  virtual bool Autostart() = 0;
  virtual void SetAutostart(bool on) = 0;
  virtual procon::SteamMode SteamBase() = 0;  // modo sem "Estou usando a Steam"
  virtual void SetSteamBase(procon::SteamMode m) = 0;
  virtual void Recalibrate() = 0;
  virtual void OpenFolder() = 0;
  virtual void HideWindow() = 0;
  virtual void Exit() = 0;

  virtual UpdateView Update() = 0;
  virtual void CheckUpdate() = 0;
  virtual void StartUpdate() = 0;  // baixa e instala a versão nova
  virtual void OpenReleasePage() = 0;

  virtual std::vector<std::string> ListProfiles() = 0;
  virtual bool LoadProfile(const std::string& name, procon::Settings& out) = 0;
  virtual bool SaveProfile(const std::string& name, const procon::Settings& s) = 0;
  virtual bool DeleteProfile(const std::string& name) = 0;
};

struct Fonts {
  ImFont* regular = nullptr;
  ImFont* bold = nullptr;
  ImFont* title = nullptr;
  ImFont* pad = nullptr;  // letras dos botões no desenho
};

// Faixas de caracteres usadas (português + setas e símbolos do desenho).
const unsigned short* GlyphRanges();

// Cores, cantos e espaçamentos. scale = DPI / 96.
void ApplyTheme(float scale);

// Desenha a janela inteira (ocupa a área toda).
void DrawApp(UiModel& model, const Fonts& fonts, float scale);

}  // namespace ui
