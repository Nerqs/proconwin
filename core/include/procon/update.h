// Aviso de versão nova: lê a resposta da API do GitHub
// (GET /repos/Nerqs/proconwin/releases/latest) e compara as versões.
//
// Esta parte não depende do Windows; o download fica no app.
#pragma once

#include <string>

namespace procon {

struct ReleaseInfo {
  std::string version;        // "1.2.0" (sem o "v" da tag)
  std::string page_url;       // página da versão no GitHub
  std::string installer_url;  // ProconWin-Setup-*.exe
  std::string sha256;         // em hexadecimal minúsculo; vazio se o GitHub não informou
};

// Retorna false se a resposta não tem a tag ou o instalador.
bool ParseLatestRelease(const std::string& json, ReleaseInfo& out);

// Compara "1.2.10" com "1.2.9" (números, separados por ponto; "v" na frente
// é ignorado). Retorna <0, 0 ou >0. Versão inválida conta como 0.0.0.
int CompareVersions(const std::string& a, const std::string& b);

}  // namespace procon
