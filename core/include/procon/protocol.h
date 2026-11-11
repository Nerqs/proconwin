// Estruturas e conversões do protocolo do Pro Controller (Switch 1).
// Referência: https://github.com/dekuNukem/Nintendo_Switch_Reverse_Engineering
#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace procon {

constexpr uint16_t kVendorNintendo = 0x057E;
constexpr uint16_t kProductProController = 0x2009;

// Relatórios de entrada (controle -> PC)
constexpr uint8_t kInputSubcmdReply = 0x21;
constexpr uint8_t kInputFull = 0x30;
constexpr uint8_t kInputUsbReply = 0x81;

// Relatórios de saída (PC -> controle)
constexpr uint8_t kOutputRumbleSubcmd = 0x01;
constexpr uint8_t kOutputRumbleOnly = 0x10;
constexpr uint8_t kOutputUsb = 0x80;

// Subcomandos
namespace subcmd {
constexpr uint8_t DeviceInfo = 0x02;
constexpr uint8_t SetReportMode = 0x03;
constexpr uint8_t SetHciState = 0x06;  // 0x00 = desconecta e dorme
constexpr uint8_t SpiRead = 0x10;
constexpr uint8_t PlayerLights = 0x30;
constexpr uint8_t HomeLight = 0x38;
constexpr uint8_t EnableImu = 0x40;
constexpr uint8_t EnableVibration = 0x48;
}  // namespace subcmd

// Comandos USB (relatório 0x80)
namespace usbcmd {
constexpr uint8_t Status = 0x01;
constexpr uint8_t Handshake = 0x02;
constexpr uint8_t HighSpeed = 0x03;
constexpr uint8_t ForceUsb = 0x04;
constexpr uint8_t DisableForceUsb = 0x05;
}  // namespace usbcmd

// Endereços da memória SPI
namespace spi {
constexpr uint32_t FactoryImuCal = 0x6020;    // 24 bytes
constexpr uint32_t FactoryStickCal = 0x603D;  // 18 bytes (9 esq. + 9 dir.)
constexpr uint32_t Colors = 0x6050;           // 6 bytes (corpo RGB, botões RGB)
constexpr uint32_t UserLeftStickCal = 0x8010; // 2 bytes mágicos + 9
constexpr uint32_t UserRightStickCal = 0x801B;// 2 bytes mágicos + 9
constexpr uint32_t UserImuCal = 0x8026;       // 2 bytes mágicos + 24
}  // namespace spi

// Bits dos botões, na ordem dos bytes 3, 4 e 5 do relatório 0x30.
enum Button : uint32_t {
  // byte 3
  BtnY = 1u << 0, BtnX = 1u << 1, BtnB = 1u << 2, BtnA = 1u << 3,
  BtnRSR = 1u << 4, BtnRSL = 1u << 5, BtnR = 1u << 6, BtnZR = 1u << 7,
  // byte 4
  BtnMinus = 1u << 8, BtnPlus = 1u << 9, BtnRStick = 1u << 10, BtnLStick = 1u << 11,
  BtnHome = 1u << 12, BtnCapture = 1u << 13, BtnChargingGrip = 1u << 15,
  // byte 5
  BtnDown = 1u << 16, BtnUp = 1u << 17, BtnRight = 1u << 18, BtnLeft = 1u << 19,
  BtnLSR = 1u << 20, BtnLSL = 1u << 21, BtnL = 1u << 22, BtnZL = 1u << 23,
};

struct StickCal {
  int x_center = 2048, y_center = 2048;
  int x_max_above = 1600, y_max_above = 1600;  // distância do centro até o máximo
  int x_min_below = 1600, y_min_below = 1600;  // distância do centro até o mínimo
};

struct ImuCal {
  std::array<int16_t, 3> acc_origin{0, 0, 0};
  std::array<int16_t, 3> acc_sens{16384, 16384, 16384};
  std::array<int16_t, 3> gyro_origin{0, 0, 0};
  std::array<int16_t, 3> gyro_sens{13371, 13371, 13371};
};

struct Calibration {
  StickCal left, right;
  ImuCal imu;
  bool left_from_user = false, right_from_user = false, imu_from_user = false;
};

struct ImuSample {
  float accel_g[3];   // em G
  float gyro_dps[3];  // em graus por segundo
};

struct InputState {
  uint8_t timer = 0;
  int battery_level = 0;  // 0 a 8
  bool charging = false;
  uint32_t buttons = 0;
  int raw_lx = 0, raw_ly = 0, raw_rx = 0, raw_ry = 0;  // 0 a 4095
  float lx = 0, ly = 0, rx = 0, ry = 0;                // -1.0 a 1.0
  std::array<ImuSample, 3> imu{};
};

// Lê dois valores de 12 bits de 3 bytes (formato dos analógicos).
void Decode12(const uint8_t* d, int& a, int& b);

// Decodifica a calibração dos analógicos (9 bytes). O esquerdo e o direito
// usam ordens diferentes.
StickCal DecodeLeftStickCal(const uint8_t* d);
StickCal DecodeRightStickCal(const uint8_t* d);
ImuCal DecodeImuCal(const uint8_t* d);

// Normaliza o valor cru do analógico para -1.0 a 1.0, aplicando zona morta.
float NormalizeAxis(int raw, int center, int max_above, int min_below, float deadzone);

// Interpreta um relatório 0x30 (ou o cabeçalho de um 0x21). Retorna false se
// o tamanho não bater.
bool ParseFullReport(const uint8_t* r, size_t len, const Calibration& cal,
                     float deadzone, InputState& out);

// Codifica um lado do rumble HD (4 bytes). amp de 0.0 a 1.0.
// Frequência alta: ~81 a 1253 Hz. Frequência baixa: ~41 a 626 Hz.
std::array<uint8_t, 4> EncodeRumble(float high_hz, float high_amp,
                                    float low_hz, float low_amp);
constexpr std::array<uint8_t, 4> kRumbleNeutral{0x00, 0x01, 0x40, 0x40};

std::string ButtonsToString(uint32_t buttons);

}  // namespace procon
