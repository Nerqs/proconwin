// Relatórios do 8BitDo Ultimate 2 Wireless conectado pelo Bluetooth (modo
// "BT" da chave). Nesse modo ele não fala o protocolo do Pro Controller: é um
// controle HID genérico, que a Steam entende mas os jogos de Xbox não.
// Referência: SDL, src/joystick/hidapi/SDL_hidapi_8bitdo.c (licença zlib).
#pragma once

#include <cstddef>
#include <cstdint>

#include "procon/protocol.h"

namespace procon {

constexpr uint16_t kVendor8BitDo = 0x2DC8;
constexpr uint16_t kProduct8BitDoUltimate2 = 0x6012;

// IDs de relatório que trazem o estado dos botões.
constexpr uint8_t k8BitDoReportBt = 0x01;
constexpr uint8_t k8BitDoReportOldFw = 0x03;
constexpr uint8_t k8BitDoReportUsb = 0x04;
// Relatório de saída da vibração: {0x05, motor forte, motor fraco, 0, 0}.
constexpr uint8_t k8BitDoOutputRumble = 0x05;

// A partir deste tamanho o relatório traz bateria e giroscópio (firmware
// 1.03 em diante; o 1.02 manda só 12 bytes).
constexpr size_t k8BitDoFullReportSize = 34;

// Os botões extras de trás (L4/R4) e as pás (PL/PR) usam bits que o Pro
// Controller não tem (os SL/SR dos Joy-Cons). Por enquanto não vão para o
// controle de Xbox: o 8BitDo pode copiar outro botão neles pelo próprio app
// dele.
constexpr uint32_t k8BitDoBtnL4 = BtnLSL;
constexpr uint32_t k8BitDoBtnR4 = BtnRSR;
constexpr uint32_t k8BitDoBtnPL = BtnLSR;
constexpr uint32_t k8BitDoBtnPR = BtnRSL;

// Gatilho analógico a partir do qual ZL/ZR contam como "apertados" (para o
// modo computador e para botões remapeados).
constexpr int k8BitDoTriggerPressed = 40;

// Interpreta um relatório do 8BitDo e preenche o estado no mesmo formato do
// Pro Controller, pela POSIÇÃO dos botões (o de baixo vira o B da Nintendo,
// que o mapa padrão manda para o A do Xbox). Retorna false se não for um
// relatório de botões.
bool Parse8BitDoReport(const uint8_t* r, size_t len, float deadzone, InputState& out);

}  // namespace procon
