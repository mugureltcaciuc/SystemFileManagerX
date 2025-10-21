#pragma once

#include "ITerminal.h"

class LinuxTerminal final : public ITerminal {
public:
  std::pair<int, int> dimensions() const override;
  void clear() const override;
};
