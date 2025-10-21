#pragma once

#include <utility>

class ITerminal {
public:
  virtual ~ITerminal() = default;

  virtual std::pair<int, int> dimensions() const = 0;
  virtual void clear() const = 0;
};
