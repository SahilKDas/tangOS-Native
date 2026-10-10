#include "splash_overlay.h"
#include <algorithm>
#include <cmath>
namespace lite {
namespace {
double bezier(double progress, double x1, double y1, double x2, double y2) {
  progress = std::clamp(progress, 0., 1.);
  if (progress == 0 || progress == 1)
    return progress;
  auto curve = [](double t, double a, double b) {
    return 3 * (1 - t) * (1 - t) * t * a + 3 * (1 - t) * t * t * b + t * t * t;
  };
  double low = 0, high = 1;
  for (int i = 0; i < 40; ++i) {
    double middle = (low + high) / 2;
    if (curve(middle, x1, x2) < progress)
      low = middle;
    else
      high = middle;
  }
  return curve((low + high) / 2, y1, y2);
}
} // namespace
SplashFrame splashFrame(double milliseconds) {
  if (!std::isfinite(milliseconds))
    milliseconds = 1750;
  double t = std::clamp(milliseconds, 0., 1750.);
  double alpha = t < 350     ? bezier(t / 350, .42, 0, .58, 1)
                 : t <= 1050 ? 1
                             : 1 - bezier((t - 1050) / 700, .42, 0, .58, 1);
  double whoosh =
      t < 1225 ? bezier(t / 1225, .2, 1.4, .3, 1) : bezier((t - 1225) / 525, .2, 1.4, .3, 1);
  double scale = t < 1225 ? .8 + .2 * whoosh : 1 + .06 * whoosh;
  double translate = t < 1225 ? 26 * (1 - whoosh) : 0;
  double centerAlpha = t < 437.5   ? bezier(t / 437.5, .2, 1.4, .3, 1)
                       : t <= 1225 ? 1
                                   : 1 - .15 * whoosh;
  return {std::clamp(alpha, 0., 1.),       scale,    translate,
          std::clamp(centerAlpha, 0., 1.), t >= 450, milliseconds >= 1750};
}
} // namespace lite
