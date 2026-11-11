// Lista os processos abertos no Windows (para detectar jogos da Steam).
#pragma once

#include <vector>

#include "procon/steam.h"

std::vector<procon::ProcessEntry> ListProcesses();
