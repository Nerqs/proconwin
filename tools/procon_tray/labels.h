// Nomes mostrados na tela para botões e ações (UTF-8).
#pragma once

#include "procon/desktop.h"
#include "procon/mapping.h"

constexpr int kSources = static_cast<int>(procon::Source::Count);
constexpr int kTargets = static_cast<int>(procon::Target::Count);
constexpr int kPcActions = static_cast<int>(procon::PcAction::Count);

const char* SourceLabel(int i);  // botão do Pro Controller
const char* TargetLabel(int i);  // botão do Xbox
const char* PcLabel(int i);      // ação do modo computador
