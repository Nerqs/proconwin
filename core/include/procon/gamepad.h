// Um controle físico qualquer que o motor sabe ler: o Pro Controller ou o
// 8BitDo Ultimate 2 pelo Bluetooth. Os dois entregam o estado no mesmo
// formato (InputState, com os bits do Pro Controller), então o resto do motor
// (mapa de botões, giroscópio, modo computador) não muda.
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "procon/protocol.h"

namespace procon {

class Gamepad {
 public:
  virtual ~Gamepad() = default;

  virtual std::string Name() const = 0;
  virtual bool IsUsb() const = 0;

  // Configura o controle depois de aberto (pode ser chamado de novo).
  virtual bool Initialize(std::string& error) = 0;

  // Espera um relatório. 1 = estado novo em out, 0 = nada (timeout ou
  // relatório de outro tipo), -1 = o controle desconectou.
  virtual int Poll(InputState& out, int timeout_ms) = 0;

  // Vibração: motor forte e fraco do Xbox (0 a 255). 0 e 0 = parar.
  virtual bool Rumble(uint8_t big, uint8_t little) = 0;

  virtual void SetPlayer(int /*player*/) {}
  virtual void SetHomeLight(bool /*on*/) {}

  // Desligar pelo HOME segurado (só o Pro Controller pelo Bluetooth).
  virtual bool CanPowerOff() const { return false; }
  virtual bool PowerOff() { return false; }

  // true se o controle manda relatórios o tempo todo e ficar mudo quer dizer
  // que algo mudou o modo dele (então o motor configura de novo).
  virtual bool NeedsWatchdog() const { return false; }
};

// Procura um controle suportado e abre. Tenta o Pro Controller primeiro.
std::unique_ptr<Gamepad> OpenGamepad(std::string& error);

}  // namespace procon
