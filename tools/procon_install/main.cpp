// procon_install: instala ou remove o driver ProconWinXusb (controle virtual).
//
// Uso (em um terminal como Administrador):
//   procon_install install <caminho\para\ProconWinXusb.inf>
//   procon_install uninstall
//
// Retorna 0 se deu certo, 3010 se deu certo mas precisa reiniciar o PC.
//
// O "install" cria o dispositivo raiz Root\ProconWinXusb (se ainda não
// existir) e instala o driver nele, como o devcon faz. Normalmente ele é
// chamado pelo driver\instalar.ps1, que antes assina o pacote.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <newdev.h>
#include <setupapi.h>
#include <shlobj.h>

#include <cstdio>
#include <string>
#include <vector>

namespace {

constexpr wchar_t kHardwareId[] = L"Root\\ProconWinXusb";

void PrintLastError(const char* what) {
  DWORD e = GetLastError();
  std::fprintf(stderr, "%s falhou (erro %lu / 0x%08lX).\n", what, e, e);
}

// Procura dispositivos (presentes ou não) com o nosso hardware ID.
std::vector<SP_DEVINFO_DATA> FindDevices(HDEVINFO set) {
  std::vector<SP_DEVINFO_DATA> found;
  SP_DEVINFO_DATA d{};
  d.cbSize = sizeof(d);
  for (DWORD i = 0; SetupDiEnumDeviceInfo(set, i, &d); ++i) {
    wchar_t buf[1024] = {};
    if (!SetupDiGetDeviceRegistryPropertyW(set, &d, SPDRP_HARDWAREID, nullptr,
                                           reinterpret_cast<PBYTE>(buf), sizeof(buf) - 4,
                                           nullptr)) {
      continue;
    }
    for (const wchar_t* p = buf; *p; p += wcslen(p) + 1) {
      if (_wcsicmp(p, kHardwareId) == 0) {
        found.push_back(d);
        break;
      }
    }
  }
  return found;
}

int Install(const std::wstring& inf_arg) {
  wchar_t inf[MAX_PATH];
  if (!GetFullPathNameW(inf_arg.c_str(), MAX_PATH, inf, nullptr)) {
    PrintLastError("GetFullPathName");
    return 1;
  }
  if (GetFileAttributesW(inf) == INVALID_FILE_ATTRIBUTES) {
    std::fwprintf(stderr, L"Arquivo não encontrado: %ls\n", inf);
    return 1;
  }

  GUID class_guid;
  wchar_t class_name[64];
  if (!SetupDiGetINFClassW(inf, &class_guid, class_name, 64, nullptr)) {
    PrintLastError("SetupDiGetINFClass");
    return 1;
  }

  // Só cria o dispositivo raiz se ele ainda não existir.
  HDEVINFO all = SetupDiGetClassDevsW(&class_guid, nullptr, nullptr, 0);
  bool exists = all != INVALID_HANDLE_VALUE && !FindDevices(all).empty();
  if (all != INVALID_HANDLE_VALUE) SetupDiDestroyDeviceInfoList(all);

  if (!exists) {
    HDEVINFO set = SetupDiCreateDeviceInfoList(&class_guid, nullptr);
    if (set == INVALID_HANDLE_VALUE) {
      PrintLastError("SetupDiCreateDeviceInfoList");
      return 1;
    }
    SP_DEVINFO_DATA d{};
    d.cbSize = sizeof(d);
    // Lista multi-string: o ID seguido de dois terminadores.
    wchar_t hwid[64] = {};
    wcscpy_s(hwid, kHardwareId);
    const DWORD hwid_bytes = static_cast<DWORD>((wcslen(hwid) + 2) * sizeof(wchar_t));
    bool ok = SetupDiCreateDeviceInfoW(set, class_name, &class_guid, nullptr, nullptr,
                                       DICD_GENERATE_ID, &d) &&
              SetupDiSetDeviceRegistryPropertyW(set, &d, SPDRP_HARDWAREID,
                                                reinterpret_cast<const BYTE*>(hwid), hwid_bytes) &&
              SetupDiCallClassInstaller(DIF_REGISTERDEVICE, set, &d);
    if (!ok) PrintLastError("Criar o dispositivo raiz");
    SetupDiDestroyDeviceInfoList(set);
    if (!ok) return 1;
    std::printf("Dispositivo raiz criado.\n");
  } else {
    std::printf("Dispositivo raiz já existe, atualizando o driver.\n");
  }

  BOOL reboot = FALSE;
  if (!UpdateDriverForPlugAndPlayDevicesW(nullptr, kHardwareId, inf, INSTALLFLAG_FORCE, &reboot)) {
    PrintLastError("Instalar o driver");
    std::fprintf(stderr,
                 "Confira se o pacote foi assinado e o certificado local importado "
                 "(rode o instalar.ps1 em vez de chamar o procon_install direto).\n");
    return 1;
  }
  std::printf("Driver instalado.%s\n", reboot ? " Reinicie o computador para concluir." : "");
  // 3010 = ERROR_SUCCESS_REBOOT_REQUIRED (o instalador entende e pede para reiniciar).
  return reboot ? 3010 : 0;
}

int Uninstall() {
  HDEVINFO set = SetupDiGetClassDevsW(nullptr, nullptr, nullptr, DIGCF_ALLCLASSES);
  if (set == INVALID_HANDLE_VALUE) {
    PrintLastError("SetupDiGetClassDevs");
    return 1;
  }
  auto devices = FindDevices(set);
  int removed = 0;
  for (auto& d : devices) {
    if (SetupDiCallClassInstaller(DIF_REMOVE, set, &d)) ++removed;
    else PrintLastError("Remover o dispositivo");
  }
  SetupDiDestroyDeviceInfoList(set);
  std::printf("%d dispositivo(s) removido(s).\n", removed);
  std::printf("Para apagar o pacote do driver e o certificado, use o desinstalar.ps1.\n");
  return 0;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  SetConsoleOutputCP(CP_UTF8);
  if (!IsUserAnAdmin()) {
    std::fprintf(stderr, "Rode este programa em um terminal como Administrador.\n");
    return 1;
  }
  if (argc == 3 && _wcsicmp(argv[1], L"install") == 0) return Install(argv[2]);
  if (argc == 2 && _wcsicmp(argv[1], L"uninstall") == 0) return Uninstall();
  std::fprintf(stderr,
               "Uso:\n  procon_install install <ProconWinXusb.inf>\n  procon_install uninstall\n");
  return 1;
}
