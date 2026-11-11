// Arquivos do app: configurações atuais e perfis.
//
//   %APPDATA%\ProconWin\config.ini          configurações em uso
//   %APPDATA%\ProconWin\perfis\<nome>.ini   um arquivo por perfil
#pragma once

#include <string>
#include <vector>

#include "procon/settings.h"

std::wstring Widen(const std::string& s);
std::string Narrow(const std::wstring& w);

std::wstring ConfigDir();
procon::Settings LoadSettings();
void SaveSettings(const procon::Settings& s);

// Nomes dos perfis salvos, em ordem alfabética.
std::vector<std::string> ListProfiles();
bool LoadProfile(const std::string& name, procon::Settings& out);
bool SaveProfile(const std::string& name, const procon::Settings& s);
bool DeleteProfile(const std::string& name);
