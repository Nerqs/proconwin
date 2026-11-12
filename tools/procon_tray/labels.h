// Nomes mostrados na tela para botões e ações.
#pragma once

#include "procon/desktop.h"
#include "procon/mapping.h"

constexpr int kSources = static_cast<int>(procon::Source::Count);
constexpr int kTargets = static_cast<int>(procon::Target::Count);
constexpr int kPcActions = static_cast<int>(procon::PcAction::Count);

const wchar_t* SourceLabel(int i);  // botão do Pro Controller
const wchar_t* TargetLabel(int i);  // botão do Xbox
const wchar_t* PcLabel(int i);      // ação do modo computador
