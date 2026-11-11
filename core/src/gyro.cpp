#include "procon/gyro.h"

#include <algorithm>
#include <cmath>

namespace procon {

namespace {

constexpr float kWindowSeconds = 1.0f;     // tempo parado para calibrar
constexpr float kMaxDeviationDps = 1.0f;   // variação máxima aceita "parado"
constexpr float kMaxAccelDeviationG = 0.02f;
constexpr float kAutoMaxBiasJumpDps = 2.0f;  // auto não muda o drift de repente
constexpr float kStickCutoffDps = 1.0f;      // abaixo disso, analógico fica parado

}  // namespace

void GyroCalibrator::StartManual() {
  manual_ = true;
  window_.clear();
  window_time_ = 0;
}

void GyroCalibrator::AddSample(const ImuSample& s, float dt) {
  float mag = std::sqrt(s.accel_g[0] * s.accel_g[0] + s.accel_g[1] * s.accel_g[1] +
                        s.accel_g[2] * s.accel_g[2]);
  window_.push_back({{s.gyro_dps[0], s.gyro_dps[1], s.gyro_dps[2]}, mag, dt});
  window_time_ += dt;
  while (!window_.empty() && window_time_ - window_.front().dt >= kWindowSeconds) {
    window_time_ -= window_.front().dt;
    window_.pop_front();
  }
  if (window_time_ >= kWindowSeconds * 0.95f) FinishWindow();
}

void GyroCalibrator::FinishWindow() {
  std::array<float, 3> mean{0, 0, 0};
  float accel_mean = 0;
  for (const auto& e : window_) {
    for (int i = 0; i < 3; ++i) mean[i] += e.g[i];
    accel_mean += e.accel_mag;
  }
  const float n = static_cast<float>(window_.size());
  for (auto& m : mean) m /= n;
  accel_mean /= n;

  // Parado = giroscópio e acelerômetro quase constantes durante a janela.
  for (const auto& e : window_) {
    for (int i = 0; i < 3; ++i) {
      if (std::fabs(e.g[i] - mean[i]) > kMaxDeviationDps) return;
    }
    if (std::fabs(e.accel_mag - accel_mean) > kMaxAccelDeviationG) return;
  }
  // Gravidade tem que estar perto de 1 G (controle não está sendo sacudido).
  if (accel_mean < 0.9f || accel_mean > 1.1f) return;

  if (!manual_ && has_bias_) {
    // Na calibração automática, um salto grande provavelmente é um movimento
    // lento e constante do jogador, não drift. Ignora.
    for (int i = 0; i < 3; ++i) {
      if (std::fabs(mean[i] - bias_[i]) > kAutoMaxBiasJumpDps) return;
    }
  }
  bias_ = mean;
  has_bias_ = true;
  manual_ = false;
  window_.clear();
  window_time_ = 0;
}

std::array<float, 3> GyroCalibrator::Correct(const ImuSample& s) const {
  return {s.gyro_dps[0] - bias_[0], s.gyro_dps[1] - bias_[1], s.gyro_dps[2] - bias_[2]};
}

void GyroOutput::Reset() {
  recent_.clear();
  acc_x_ = acc_y_ = 0;
  stick_x_ = stick_y_ = 0;
}

void GyroOutput::AddSample(const std::array<float, 3>& g, float dt) {
  const auto& s = settings_;
  if (s.mode == GyroMode::Off || dt <= 0) {
    stick_x_ = stick_y_ = 0;
    return;
  }

  // Girar o controle para a direita dá rotação negativa no eixo vertical,
  // então o sinal é invertido para que direita = positivo.
  float x = -g[static_cast<size_t>(s.yaw_axis)];
  float y = -g[static_cast<size_t>(s.pitch_axis)];
  if (s.invert_x) x = -x;
  if (s.invert_y) y = -y;

  // Suavização em camadas: movimentos pequenos usam a média recente,
  // movimentos acima do limite passam direto, sem atraso.
  recent_.push_back({x, y, dt});
  float t = 0;
  for (const auto& r : recent_) t += r[2];
  while (recent_.size() > 1 && t - recent_.front()[2] >= s.smoothing_window_s) {
    t -= recent_.front()[2];
    recent_.pop_front();
  }
  float sx = 0, sy = 0;
  for (const auto& r : recent_) { sx += r[0]; sy += r[1]; }
  sx /= static_cast<float>(recent_.size());
  sy /= static_cast<float>(recent_.size());

  float speed = std::hypot(x, y);
  float th = std::max(s.smoothing_threshold_dps, 0.001f);
  float direct = std::clamp((speed - th * 0.5f) / (th * 0.5f), 0.0f, 1.0f);
  float ox = direct * x + (1.0f - direct) * sx;
  float oy = direct * y + (1.0f - direct) * sy;

  if (s.mode == GyroMode::Mouse) {
    acc_x_ += ox * dt * s.mouse_pixels_per_degree;
    // Mouse: Y positivo = para baixo na tela.
    acc_y_ += -oy * dt * s.mouse_pixels_per_degree;
    return;
  }

  // Modo analógico direito.
  float out_speed = std::hypot(ox, oy);
  if (out_speed < kStickCutoffDps) {
    stick_x_ = stick_y_ = 0;
    return;
  }
  float mag = std::min(out_speed / s.stick_full_speed_dps, 1.0f);
  float out_mag = s.stick_anti_deadzone + (1.0f - s.stick_anti_deadzone) * mag;
  stick_x_ = ox / out_speed * out_mag;
  stick_y_ = oy / out_speed * out_mag;
}

void GyroOutput::TakeMouseDelta(int& dx, int& dy) {
  dx = static_cast<int>(std::trunc(acc_x_));
  dy = static_cast<int>(std::trunc(acc_y_));
  acc_x_ -= static_cast<float>(dx);
  acc_y_ -= static_cast<float>(dy);
}

}  // namespace procon
