// Conexão com um Pro Controller via hidapi (USB ou Bluetooth).
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "procon/protocol.h"

struct hid_device_;
typedef struct hid_device_ hid_device;

namespace procon {

struct DeviceInfo {
  int fw_major = 0, fw_minor = 0;
  int type = 0;  // 3 = Pro Controller
  std::string mac;
  std::array<uint8_t, 3> body_color{}, button_color{};
};

class Controller {
 public:
  Controller() = default;
  ~Controller();
  Controller(const Controller&) = delete;
  Controller& operator=(const Controller&) = delete;

  // Procura e abre o primeiro Pro Controller. Retorna false se não achar.
  bool Open(std::string& error);
  void Close();

  bool IsUsb() const { return usb_; }
  const std::string& Path() const { return path_; }

  // Faz toda a inicialização: handshake USB, leitura da calibração,
  // IMU, vibração, modo 0x30 e LED do jogador.
  bool Initialize(std::string& error);

  // Lê um relatório de entrada. Retorna o tamanho, 0 em timeout, -1 em erro.
  int ReadReport(uint8_t* buf, size_t len, int timeout_ms);

  // Envia rumble para os dois lados (4 bytes cada).
  bool SendRumble(const std::array<uint8_t, 4>& left, const std::array<uint8_t, 4>& right);

  bool SetPlayerLights(uint8_t mask);  // bits 0-3 acesos, 4-7 piscando
  bool SetHomeLight(bool on);

  const Calibration& Cal() const { return cal_; }
  const DeviceInfo& Info() const { return info_; }

 private:
  bool Write(const uint8_t* data, size_t len);
  bool UsbCommand(uint8_t cmd, bool wait_reply);
  // Envia um subcomando e espera a resposta 0x21. reply recebe o relatório inteiro.
  bool Subcommand(uint8_t id, const std::vector<uint8_t>& args,
                  std::vector<uint8_t>* reply = nullptr);
  bool SpiRead(uint32_t addr, uint8_t size, std::vector<uint8_t>& out);
  bool LoadCalibration();
  bool LoadDeviceInfo();

  hid_device* dev_ = nullptr;
  bool usb_ = false;
  std::string path_;
  uint8_t counter_ = 0;
  std::array<uint8_t, 4> rumble_l_ = kRumbleNeutral;
  std::array<uint8_t, 4> rumble_r_ = kRumbleNeutral;
  Calibration cal_;
  DeviceInfo info_;
};

}  // namespace procon
