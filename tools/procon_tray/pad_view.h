// Desenho do Pro Controller (feito no programa, sem imagem da Nintendo).
// Mostra os botões no lugar certo, acende o que está apertado e avisa o pai
// quando o mouse passa ou clica num botão.
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <array>
#include <cstdint>

#include "labels.h"

// Notificações (WM_COMMAND, HIWORD(wParam)).
constexpr WORD kPadHover = 1;  // o botão sob o mouse mudou (PadViewHover)
constexpr WORD kPadClick = 2;  // clicou num botão (PadViewHover diz qual)

HWND CreatePadView(HWND parent, int id, int x, int y, int w, int h);
// pressed: bits do Pro Controller; changed: botões fora do padrão (destaque).
void PadViewSet(HWND view, uint32_t pressed, const std::array<bool, kSources>& changed);
int PadViewHover(HWND view);  // índice do botão sob o mouse, ou -1
