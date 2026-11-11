// Contrato entre o driver ProconWinXusb (modo usuário, UMDF2) e o programa
// que alimenta o controle virtual (procon_virtual).
//
// O driver publica duas interfaces no mesmo dispositivo:
//  - GUID_DEVINTERFACE_XUSB: a interface que o XInput procura. Ela só fica
//    ligada enquanto um programa estiver alimentando o controle, assim o
//    controle virtual aparece e some junto com o procon_virtual.
//  - GUID_DEVINTERFACE_PROCONWIN_FEED: a interface do nosso programa.
//
// Uso pelo programa:
//  1. Abre a interface FEED e manda IOCTL_PROCONWIN_REGISTER (só um programa
//     por vez; o segundo recebe ERROR_BUSY).
//  2. Manda IOCTL_PROCONWIN_SUBMIT a cada leitura do controle real.
//  3. Em outra thread, repete IOCTL_PROCONWIN_WAIT_FEEDBACK para receber
//     vibração e LED que o jogo pediu.
//  4. Fecha o handle para desligar o controle virtual.
#pragma once

#ifdef __cplusplus
#include <cstdint>
#else
#include <stdint.h>
#endif

// {EC87F1E3-C13B-4100-B5F7-8B84D54260CB}: interface dos controles de Xbox 360
// (a mesma do xusb22.sys, que o XInput usa para enumerar controles).
#define PROCONWIN_XUSB_INTERFACE_GUID_INIT \
  {0xEC87F1E3, 0xC13B, 0x4100, {0xB5, 0xF7, 0x8B, 0x84, 0xD5, 0x42, 0x60, 0xCB}}

// {B3F1F8C4-1F4E-4B7B-9C2D-6A1E3D5F7A21}: interface do nosso programa.
#define PROCONWIN_FEED_INTERFACE_GUID_INIT \
  {0xB3F1F8C4, 0x1F4E, 0x4B7B, {0x9C, 0x2D, 0x6A, 0x1E, 0x3D, 0x5F, 0x7A, 0x21}}

// IOCTLs do nosso programa (FILE_DEVICE_UNKNOWN, METHOD_BUFFERED).
#define PROCONWIN_CTL(fn, access) (((0x22u) << 16) | ((access) << 14) | ((fn) << 2) | 0u)
#define IOCTL_PROCONWIN_REGISTER      PROCONWIN_CTL(0x900u, 3u)  // sem dados
#define IOCTL_PROCONWIN_SUBMIT        PROCONWIN_CTL(0x901u, 3u)  // in: ProconwinReport
#define IOCTL_PROCONWIN_WAIT_FEEDBACK PROCONWIN_CTL(0x902u, 3u)  // in: uint32 seq; out: ProconwinFeedback

#pragma pack(push, 1)

// Estado do controle, no formato do XInput (XINPUT_GAMEPAD).
typedef struct ProconwinReport {
  uint16_t buttons;  // bits XINPUT_GAMEPAD_* (0x0400 = Guide)
  uint8_t left_trigger;
  uint8_t right_trigger;
  int16_t lx, ly, rx, ry;
} ProconwinReport;

// Pedido do jogo. WAIT_FEEDBACK responde na hora se seq for diferente do
// que o programa já viu; se for igual, espera a próxima mudança.
typedef struct ProconwinFeedback {
  uint32_t seq;
  uint8_t large_motor;  // motor esquerdo (forte, frequência baixa)
  uint8_t small_motor;  // motor direito (fraco, frequência alta)
  uint8_t led;          // código de LED do XInput (6..9 = jogador 1..4)
  uint8_t player;       // 0..3, ou 0xFF se ainda não tem jogador
} ProconwinFeedback;

#pragma pack(pop)
