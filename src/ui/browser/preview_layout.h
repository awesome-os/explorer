#pragma once

#include "model/thumbnail_options.h"
#include "ui/style.h"

#include <algorithm>
#include <cmath>

namespace luciole {
  struct PreviewImageLayout {
    float width, height;
    int pixels;
  };

  inline PreviewImageLayout
  previewImageLayout(int sourceWidth, int sourceHeight, float width, float height, float scale, bool quick = false) {
    const float budget = std::max(40.0F, width - Style::spaceMd * 2 - Style::scrollbarWidth - Style::scrollbarGap);
    const float imageWidth = quick ? std::min(budget, 460.0F) : budget;
    const float maximumHeight = std::max(80.0F, height - 250.0F);
    const bool known = sourceWidth > 0 && sourceHeight > 0;
    const float imageHeight = std::clamp(
        known ? imageWidth * static_cast<float>(sourceHeight) / static_cast<float>(sourceWidth) : 96.0F, 48.0F,
        maximumHeight
    );
    float extent = std::max(imageWidth, maximumHeight);
    if (known) {
      const float fit =
          std::min(imageWidth / static_cast<float>(sourceWidth), imageHeight / static_cast<float>(sourceHeight));
      extent = static_cast<float>(std::max(sourceWidth, sourceHeight)) * fit;
    }
    const int pixels = static_cast<int>(
        std::ceil(std::clamp(extent * std::max(0.01F, scale), 1.0F, static_cast<float>(kMaxPreviewImageSide)))
    );
    return {imageWidth, imageHeight, pixels};
  }
} // namespace luciole
