// Envia para o Windows o que o modo computador calculou (SendInput).
//
// Limite do Windows: programas abertos como administrador não recebem
// teclas e cliques de um programa comum (UIPI), e Ctrl+Alt+Del não pode ser
// simulado.
#pragma once

#include "procon/desktop.h"

namespace procon {

void SendDesktopFrame(const DesktopFrame& f);

}  // namespace procon
