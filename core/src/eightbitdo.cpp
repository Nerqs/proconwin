#include "procon/eightbitdo.h"

#include <algorithm>
#include <cmath>

namespace procon {

namespace {

int16_t ReadI16(const uint8_t* d) {
  return static_cast<int16_t>(d[0] | (d[1] << 8));
}

// Analógico de 8 bits (0x7F = centro) para -1.0 a 1.0 com zona morta.
float Axis8(uint8_t raw, float deadzone) {
  float v = (static_cast<int>(raw) - 0x7F) / (raw >= 0x7F ? 128.0f : 127.0f);
  v = std::clamp(v, -1.0f, 1.0f);
  float mag = std::fabs(v);
  if (mag < deadzone) return 0.0f;
  float scaled = (mag - deadzone) / (1.0f - deadzone);
  return v < 0 ? -scaled : scaled;
}

// Escalas do SDL: acelerômetro 4096 = 1 G; giroscópio +-2000 graus/s.
constexpr float kAccelPerG = 4096.0f;
constexpr float kGyroDpsPerUnit = 2000.0f / 32767.0f;

}  // namespace

bool Parse8BitDoReport(const uint8_t* r, size_t len, float deadzone, InputState& out) {
  if (len < 10) return false;
  if (r[0] != k8BitDoReportBt && r[0] != k8BitDoReportOldFw && r[0] != k8BitDoReportUsb) {
    return false;
  }

  uint32_t b = 0;
  // Direcional: byte 1 é um "hat" (0 = cima, sentido horário, 8+ = solto).
  switch (r[1]) {
    case 0: b |= BtnUp; break;
    case 1: b |= BtnUp | BtnRight; break;
    case 2: b |= BtnRight; break;
    case 3: b |= BtnDown | BtnRight; break;
    case 4: b |= BtnDown; break;
    case 5: b |= BtnDown | BtnLeft; break;
    case 6: b |= BtnLeft; break;
    case 7: b |= BtnUp | BtnLeft; break;
    default: break;
  }

  // Byte 8: baixo, direita, PR, esquerda, cima, PL, LB, RB.
  const uint8_t b8 = r[8];
  if (b8 & 0x01) b |= BtnB;  // baixo
  if (b8 & 0x02) b |= BtnA;  // direita
  if (b8 & 0x08) b |= BtnY;  // esquerda
  if (b8 & 0x10) b |= BtnX;  // cima
  if (b8 & 0x40) b |= BtnL;
  if (b8 & 0x80) b |= BtnR;
  if (b8 & 0x20) b |= k8BitDoBtnPL;
  if (b8 & 0x04) b |= k8BitDoBtnPR;

  // Byte 9: select, start, home, L3, R3.
  const uint8_t b9 = r[9];
  if (b9 & 0x04) b |= BtnMinus;
  if (b9 & 0x08) b |= BtnPlus;
  if (b9 & 0x10) b |= BtnHome;
  if (b9 & 0x20) b |= BtnLStick;
  if (b9 & 0x40) b |= BtnRStick;

  // Byte 10: L4, R4 (botões de trás).
  if (len > 10) {
    if (r[10] & 0x01) b |= k8BitDoBtnL4;
    if (r[10] & 0x02) b |= k8BitDoBtnR4;
  }

  // Gatilhos analógicos: byte 7 = esquerdo, byte 6 = direito.
  out.lt = r[7];
  out.rt = r[6];
  if (out.lt >= k8BitDoTriggerPressed) b |= BtnZL;
  if (out.rt >= k8BitDoTriggerPressed) b |= BtnZR;
  out.buttons = b;

  // Analógicos: bytes 2 a 5. No HID o Y cresce para baixo; aqui é para cima.
  out.raw_lx = r[2] * 16;
  out.raw_ly = (255 - r[3]) * 16;
  out.raw_rx = r[4] * 16;
  out.raw_ry = (255 - r[5]) * 16;
  out.lx = Axis8(r[2], deadzone);
  out.ly = -Axis8(r[3], deadzone);
  out.rx = Axis8(r[4], deadzone);
  out.ry = -Axis8(r[5], deadzone);

  // Bateria (byte 14): bit 7 = carregando, resto = porcentagem.
  if (len >= k8BitDoFullReportSize) {
    int pct = std::min(r[14] & 0x7F, 100);
    out.charging = (r[14] & 0x80) != 0 && pct < 100;
    out.battery_level = (pct * 8 + 50) / 100;
    out.battery_known = true;
  } else {
    out.battery_level = 0;
    out.charging = false;
    out.battery_known = false;
  }

  // Giroscópio e acelerômetro (bytes 15 a 26): uma amostra por relatório,
  // nos mesmos eixos do Pro Controller (o SDL gira os dois do mesmo jeito).
  if (len >= k8BitDoFullReportSize) {
    ImuSample s{};
    for (int i = 0; i < 3; ++i) {
      s.accel_g[i] = ReadI16(r + 15 + i * 2) / kAccelPerG;
      s.gyro_dps[i] = ReadI16(r + 21 + i * 2) * kGyroDpsPerUnit;
    }
    out.imu[0] = s;
    out.imu_count = 1;
  } else {
    out.imu_count = 0;
  }
  return true;
}

}  // namespace procon
