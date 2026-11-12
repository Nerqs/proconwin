// Atualização pelo GitHub: procura versão nova, baixa o instalador, confere o
// SHA-256 e abre o instalador (que fecha o app, atualiza e abre de novo).
//
// Tudo roda numa thread separada; a janela e a bandeja só leem o estado.
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>

namespace updater {

enum class State { Idle, Checking, UpToDate, Available, Downloading, Ready, Failed };

struct Status {
  State state = State::Idle;
  std::string current;  // versão deste programa
  std::string latest;   // versão nova (quando há)
  std::string page_url;
  float progress = 0;   // 0 a 1 durante o download
  std::string error;
};

// Mensagem postada para notify quando o estado muda (wParam = State).
void Init(HWND notify, UINT msg);
Status Get();

// Procura versão nova (não faz nada se já está procurando ou baixando).
void Check();
// Baixa e confere o instalador da versão nova. Ao terminar: State::Ready.
void Download();
// Abre o instalador baixado em modo silencioso. Retorna false se não abriu.
bool RunInstaller();

}  // namespace updater
