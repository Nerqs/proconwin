// Processamento do giroscópio: correção de drift e conversão para mouse ou
// analógico direito, pensado para jogos de tiro.
#pragma once

#include <array>
#include <deque>

#include "procon/protocol.h"

namespace procon {

enum class GyroMode { Off, Mouse, RightStick };

struct GyroSettings {
  GyroMode mode = GyroMode::Off;

  // Eixos: com o controle na mão, "yaw" é girar para os lados e "pitch" é
  // inclinar para cima/baixo. Índices 0=X, 1=Y, 2=Z do giroscópio.
  int yaw_axis = 2;
  int pitch_axis = 1;
  bool invert_x = false;
  bool invert_y = false;

  // Modo mouse: pixels por grau girado. Quanto maior, mais rápido.
  float mouse_pixels_per_degree = 20.0f;

  // Suavização só para movimentos pequenos (abaixo deste valor em °/s),
  // para tirar a tremedeira da mão sem atrasar movimentos rápidos.
  float smoothing_threshold_dps = 5.0f;
  float smoothing_window_s = 0.1f;

  // Modo analógico: velocidade (°/s) que leva o analógico até o fim.
  float stick_full_speed_dps = 360.0f;
  // Deflexão mínima para vencer a zona morta do jogo (0 a 1).
  float stick_anti_deadzone = 0.15f;
};

// Calibração de drift. Aprende sozinha quando o controle fica parado e
// também aceita uma calibração manual.
class GyroCalibrator {
 public:
  // Adiciona uma amostra. dt em segundos.
  void AddSample(const ImuSample& s, float dt);
  // Começa uma calibração manual: o controle deve ficar parado por ~1 s.
  void StartManual();
  bool ManualInProgress() const { return manual_; }
  bool HasBias() const { return has_bias_; }
  const std::array<float, 3>& Bias() const { return bias_; }
  // Velocidade angular já sem o drift.
  std::array<float, 3> Correct(const ImuSample& s) const;

 private:
  struct Entry { std::array<float, 3> g; float accel_mag; float dt; };
  void FinishWindow();

  std::deque<Entry> window_;
  float window_time_ = 0;
  std::array<float, 3> bias_{0, 0, 0};
  bool has_bias_ = false;
  bool manual_ = false;
};

// Converte velocidade angular (já corrigida) em movimento de mouse ou de
// analógico.
class GyroOutput {
 public:
  explicit GyroOutput(const GyroSettings& s) : settings_(s) {}
  void SetSettings(const GyroSettings& s) { settings_ = s; }
  const GyroSettings& Settings() const { return settings_; }

  // Processa uma amostra (°/s). dt em segundos.
  void AddSample(const std::array<float, 3>& gyro_dps, float dt);

  // Modo mouse: retorna os pixels inteiros acumulados desde a última chamada
  // (a parte fracionária fica guardada para a próxima, sem perder precisão).
  void TakeMouseDelta(int& dx, int& dy);

  // Modo analógico: deflexão atual (-1 a 1) para somar ao analógico direito.
  float StickX() const { return stick_x_; }
  float StickY() const { return stick_y_; }

  void Reset();

 private:
  GyroSettings settings_;
  std::deque<std::array<float, 3>> recent_;  // para a suavização
  float acc_x_ = 0, acc_y_ = 0;              // pixels acumulados
  float stick_x_ = 0, stick_y_ = 0;
};

}  // namespace procon
