#pragma once
#include <stdint.h>
namespace ui {
inline bool advanceCelebration(uint32_t now, uint32_t since, bool certificate,
                               bool held, bool demo) {
  if (held || (!demo && certificate)) return false;
  return uint32_t(now-since) >= (certificate ? 12000u : 4000u);
}
}
