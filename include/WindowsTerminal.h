#pragma once

#include "ITerminal.h"

class WindowsTerminal final : public ITerminal {
public:
  std::pair<int, int> dimensions() const override;
  void clear() const override;
};
