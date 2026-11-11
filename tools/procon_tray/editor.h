// Janela do editor de botões e perfis.
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <functional>

#include "procon/settings.h"

struct EditorCallbacks {
  std::function<procon::Settings()> get;                // configurações atuais
  std::function<void(const procon::Settings&)> apply;   // aplica e salva
  std::function<uint32_t()> pressed_buttons;            // botões apertados agora
};

// Abre o editor (ou traz para frente, se já estiver aberto). pc_view abre
// direto nas teclas do modo computador.
void ShowButtonEditor(HINSTANCE inst, const EditorCallbacks& cb, bool pc_view = false);

// Janela do editor, para o laço de mensagens tratar Tab/Enter (ou nullptr).
HWND ButtonEditorWindow();

// Atualiza o editor depois de uma mudança feita pelo menu da bandeja.
void RefreshButtonEditor();
