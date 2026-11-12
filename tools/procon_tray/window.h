// Janela principal do ProconWin: estado do controle, todas as opções,
// perfis e o desenho do Pro Controller para escolher a função de cada botão.
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <functional>

#include "engine.h"
#include "procon/settings.h"

struct AppHooks {
  std::function<procon::Settings()> get;               // configurações atuais
  std::function<void(const procon::Settings&)> apply;  // aplica, salva (e no perfil)
  std::function<procon::EngineStatus()> status;
  std::function<std::wstring()> status_text;
  std::function<bool()> autostart;
  std::function<void(bool)> set_autostart;
  std::function<procon::SteamMode()> steam_base;  // modo sem "Estou usando a Steam"
  std::function<void(procon::SteamMode)> set_steam_base;
  std::function<void()> recalibrate;
  std::function<void()> open_folder;
  std::function<void()> diagnostics;
  std::function<void()> exit_app;
};

// Mostra a janela (cria na primeira vez). Fechar só esconde: o programa
// continua na bandeja.
void ShowMainWindow(HINSTANCE inst, const AppHooks& hooks);
// Atualiza os controles depois de uma mudança feita por fora (bandeja, controle).
void RefreshMainWindow();
// Tab, Enter e Esc nas janelas do programa. Retorna true se tratou a mensagem.
bool HandleWindowMessage(MSG* msg);
