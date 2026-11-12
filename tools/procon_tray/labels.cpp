#include "labels.h"

const wchar_t* SourceLabel(int i) {
  static const wchar_t* names[] = {
      L"A", L"B", L"X", L"Y", L"L", L"R", L"ZL", L"ZR",
      L"\x2212 (menos)", L"+ (mais)", L"L3 (clique esq.)", L"R3 (clique dir.)", L"HOME",
      L"Captura", L"Direcional \x2191", L"Direcional \x2193", L"Direcional \x2190",
      L"Direcional \x2192"};
  static_assert(sizeof(names) / sizeof(names[0]) == kSources, "names");
  return names[i];
}

const wchar_t* TargetLabel(int i) {
  static const wchar_t* names[] = {
      L"(nada)", L"A", L"B", L"X", L"Y", L"LB", L"RB", L"LT (gatilho esq.)",
      L"RT (gatilho dir.)", L"Back / View", L"Start / Menu", L"LS (clique analógico esq.)",
      L"RS (clique analógico dir.)", L"Guide (botão Xbox)", L"D-pad \x2191", L"D-pad \x2193",
      L"D-pad \x2190", L"D-pad \x2192"};
  static_assert(sizeof(names) / sizeof(names[0]) == kTargets, "names");
  return names[i];
}

const wchar_t* PcLabel(int i) {
  static const wchar_t* names[] = {
      L"(nada)", L"Clique esquerdo", L"Clique direito", L"Clique do meio", L"Enter", L"Esc",
      L"Espaço", L"Apagar (Backspace)", L"Tab", L"Delete", L"Seta \x2191", L"Seta \x2193",
      L"Seta \x2190", L"Seta \x2192", L"Page Up", L"Page Down", L"Tecla Windows (Iniciar)",
      L"Alt+Tab (trocar janela)", L"Ver todas as janelas", L"Fechar janela (Alt+F4)",
      L"Teclado virtual", L"Recortar a tela", L"Volume +", L"Volume \x2212", L"Mudo",
      L"Play / pausa", L"Voltar (navegador)", L"Avançar (navegador)"};
  static_assert(sizeof(names) / sizeof(names[0]) == kPcActions, "names");
  return names[i];
}
