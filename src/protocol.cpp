#include "protocol.h"

#include <algorithm>
#include <cmath>

namespace procon {

namespace {

int16_t ReadI16(const uint8_t* d) {
  return static_cast<int16_t>(d[0] | (d[1] << 8));
}

// Valores de calibração "vazios" (memória apagada) vêm como 0xFFF.
bool StickCalLooksValid(const StickCal& c) {
  auto ok = [](int v) { return v > 0 && v < 0xFFF; };
  return ok(c.x_center) && ok(c.y_center) && ok(c.x_max_above) &&
         ok(c.y_max_above) && ok(c.x_min_below) && ok(c.y_min_below);
}

}  // namespace

void Decode12(const uint8_t* d, int& a, int& b) {
  a = d[0] | ((d[1] & 0x0F) << 8);
  b = (d[1] >> 4) | (d[2] << 4);
}

StickCal DecodeLeftStickCal(const uint8_t* d) {
  // Esquerdo: máximo acima do centro, centro, mínimo abaixo do centro.
  StickCal c;
  Decode12(d + 0, c.x_max_above, c.y_max_above);
  Decode12(d + 3, c.x_center, c.y_center);
  Decode12(d + 6, c.x_min_below, c.y_min_below);
  return StickCalLooksValid(c) ? c : StickCal{};
}

StickCal DecodeRightStickCal(const uint8_t* d) {
  // Direito: centro, mínimo abaixo do centro, máximo acima do centro.
  StickCal c;
  Decode12(d + 0, c.x_center, c.y_center);
  Decode12(d + 3, c.x_min_below, c.y_min_below);
  Decode12(d + 6, c.x_max_above, c.y_max_above);
  return StickCalLooksValid(c) ? c : StickCal{};
}

ImuCal DecodeImuCal(const uint8_t* d) {
  ImuCal c;
  for (int i = 0; i < 3; ++i) {
    c.acc_origin[i] = ReadI16(d + 0 + i * 2);
    c.acc_sens[i] = ReadI16(d + 6 + i * 2);
    c.gyro_origin[i] = ReadI16(d + 12 + i * 2);
    c.gyro_sens[i] = ReadI16(d + 18 + i * 2);
  }
  // Se a sensibilidade estiver apagada ou igual à origem, usa o padrão.
  for (int i = 0; i < 3; ++i) {
    if (c.acc_sens[i] == -1 || c.acc_sens[i] == c.acc_origin[i]) {
      c.acc_sens[i] = 16384;
      c.acc_origin[i] = 0;
    }
    if (c.gyro_sens[i] == -1 || c.gyro_sens[i] == c.gyro_origin[i]) {
      c.gyro_sens[i] = 13371;
      c.gyro_origin[i] = 0;
    }
  }
  return c;
}

float NormalizeAxis(int raw, int center, int max_above, int min_below, float deadzone) {
  float v;
  if (raw >= center) {
    v = static_cast<float>(raw - center) / static_cast<float>(std::max(1, max_above));
  } else {
    v = static_cast<float>(raw - center) / static_cast<float>(std::max(1, min_below));
  }
  v = std::clamp(v, -1.0f, 1.0f);
  // Zona morta com reescala, para não ter "salto" ao sair dela.
  float mag = std::fabs(v);
  if (mag < deadzone) return 0.0f;
  float scaled = (mag - deadzone) / (1.0f - deadzone);
  return v < 0 ? -scaled : scaled;
}

bool ParseFullReport(const uint8_t* r, size_t len, const Calibration& cal,
                     float deadzone, InputState& out) {
  if (len < 13) return false;
  out.timer = r[1];
  out.battery_level = (r[2] >> 4) & 0x0E;
  out.charging = (r[2] & 0x10) != 0;
  out.buttons = r[3] | (r[4] << 8) | (static_cast<uint32_t>(r[5]) << 16);

  Decode12(r + 6, out.raw_lx, out.raw_ly);
  Decode12(r + 9, out.raw_rx, out.raw_ry);
  const auto& L = cal.left;
  const auto& R = cal.right;
  out.lx = NormalizeAxis(out.raw_lx, L.x_center, L.x_max_above, L.x_min_below, deadzone);
  out.ly = NormalizeAxis(out.raw_ly, L.y_center, L.y_max_above, L.y_min_below, deadzone);
  out.rx = NormalizeAxis(out.raw_rx, R.x_center, R.x_max_above, R.x_min_below, deadzone);
  out.ry = NormalizeAxis(out.raw_ry, R.y_center, R.y_max_above, R.y_min_below, deadzone);

  // O IMU só existe no relatório 0x30 completo (49 bytes).
  if (r[0] == kInputFull && len >= 49) {
    const auto& c = cal.imu;
    for (int s = 0; s < 3; ++s) {
      const uint8_t* p = r + 13 + s * 12;
      for (int i = 0; i < 3; ++i) {
        int16_t acc = ReadI16(p + i * 2);
        int16_t gyr = ReadI16(p + 6 + i * 2);
        float acc_coeff = 4.0f / static_cast<float>(c.acc_sens[i] - c.acc_origin[i]);
        float gyr_coeff = 936.0f / static_cast<float>(c.gyro_sens[i] - c.gyro_origin[i]);
        out.imu[s].accel_g[i] = static_cast<float>(acc) * acc_coeff;
        out.imu[s].gyro_dps[i] = static_cast<float>(gyr - c.gyro_origin[i]) * gyr_coeff;
      }
    }
  }
  return true;
}

namespace {

uint8_t EncodeAmplitude(float amp) {
  if (amp <= 0.0f) return 0;
  amp = std::min(amp, 1.0f);
  float e;
  if (amp > 0.23f) {
    e = std::log2(amp * 8.7f) * 32.0f;
  } else if (amp > 0.12f) {
    e = std::log2(amp * 17.0f) * 16.0f;
  } else {
    e = ((std::log2(amp) * 32.0f) - 96.0f) / (4.0f - 2.0f * amp);
  }
  return static_cast<uint8_t>(std::clamp(std::lround(e), 0L, 100L));
}

uint8_t EncodeFrequency(float hz) {
  return static_cast<uint8_t>(std::lround(std::log2(hz / 10.0f) * 32.0f));
}

}  // namespace

std::array<uint8_t, 4> EncodeRumble(float high_hz, float high_amp,
                                    float low_hz, float low_amp) {
  high_hz = std::clamp(high_hz, 81.75f, 1252.0f);
  low_hz = std::clamp(low_hz, 40.875f, 626.0f);

  uint16_t hf = static_cast<uint16_t>((EncodeFrequency(high_hz) - 0x60) * 4);
  uint8_t lf = static_cast<uint8_t>(EncodeFrequency(low_hz) - 0x40);

  uint8_t ha = EncodeAmplitude(high_amp);
  uint8_t la = EncodeAmplitude(low_amp);
  uint8_t hf_amp = static_cast<uint8_t>(ha * 2);  // máx. 0xC8
  // Amplitude baixa: 16 bits; o bit mais baixo do valor vai para o bit 7 do byte 2.
  uint16_t lf_amp = static_cast<uint16_t>(((la & 1) ? 0x8000 : 0) + (la >> 1) + 0x40);

  return {
      static_cast<uint8_t>(hf & 0xFF),
      static_cast<uint8_t>(hf_amp + ((hf >> 8) & 0xFF)),
      static_cast<uint8_t>(lf + ((lf_amp >> 8) & 0xFF)),
      static_cast<uint8_t>(lf_amp & 0xFF),
  };
}

std::string ButtonsToString(uint32_t b) {
  struct Name { uint32_t bit; const char* name; };
  static const Name names[] = {
      {BtnA, "A"}, {BtnB, "B"}, {BtnX, "X"}, {BtnY, "Y"},
      {BtnL, "L"}, {BtnR, "R"}, {BtnZL, "ZL"}, {BtnZR, "ZR"},
      {BtnMinus, "-"}, {BtnPlus, "+"}, {BtnLStick, "L3"}, {BtnRStick, "R3"},
      {BtnHome, "HOME"}, {BtnCapture, "CAP"},
      {BtnUp, "CIMA"}, {BtnDown, "BAIXO"}, {BtnLeft, "ESQ"}, {BtnRight, "DIR"},
  };
  std::string s;
  for (const auto& n : names) {
    if (b & n.bit) {
      if (!s.empty()) s += ' ';
      s += n.name;
    }
  }
  return s.empty() ? "-" : s;
}

}  // namespace procon
