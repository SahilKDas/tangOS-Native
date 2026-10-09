#pragma once
#include "descriptor.h"
namespace lite {
// Original Controller view semantics, independent of native window rendering.
Json controllerView(const Json &agent, const Json &batches, const Json &runs);
double controllerProgress(double from, double to, double elapsedMilliseconds);
} // namespace lite
