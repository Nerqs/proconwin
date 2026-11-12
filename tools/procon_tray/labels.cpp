#include "labels.h"

const char* SourceLabel(int i) {
  static const char* names[] = {
      u8"A", u8"B", u8"X", u8"Y", u8"L", u8"R", u8"ZL", u8"ZR",
      u8"− (menos)", u8"+ (mais)", u8"L3 (clique esq.)", u8"R3 (clique dir.)", u8"HOME",
      u8"Captura", u8"Direcional ↑", u8"Direcional ↓", u8"Direcional ←",
      u8"Direcional →"};
  static_assert(sizeof(names) / sizeof(names[0]) == kSources, "names");
  return names[i];
}

const char* TargetLabel(int i) {
  static const char* names[] = {
      u8"(nada)", u8"A", u8"B", u8"X", u8"Y", u8"LB", u8"RB", u8"LT (gatilho esq.)",
      u8"RT (gatilho dir.)", u8"Back / View", u8"Start / Menu", u8"LS (clique analógico esq.)",
      u8"RS (clique analógico dir.)", u8"Guide (botão Xbox)", u8"D-pad ↑", u8"D-pad ↓",
      u8"D-pad ←", u8"D-pad →"};
  static_assert(sizeof(names) / sizeof(names[0]) == kTargets, "names");
  return names[i];
}

const char* PcLabel(int i) {
  static const char* names[] = {
      u8"(nada)", u8"Clique esquerdo", u8"Clique direito", u8"Clique do meio", u8"Enter", u8"Esc",
      u8"Espaço", u8"Apagar (Backspace)", u8"Tab", u8"Delete", u8"Seta ↑", u8"Seta ↓",
      u8"Seta ←", u8"Seta →", u8"Page Up", u8"Page Down", u8"Tecla Windows (Iniciar)",
      u8"Alt+Tab (trocar janela)", u8"Ver todas as janelas", u8"Fechar janela (Alt+F4)",
      u8"Teclado virtual", u8"Recortar a tela", u8"Volume +", u8"Volume −", u8"Mudo",
      u8"Play / pausa", u8"Voltar (navegador)", u8"Avançar (navegador)"};
  static_assert(sizeof(names) / sizeof(names[0]) == kPcActions, "names");
  return names[i];
}
