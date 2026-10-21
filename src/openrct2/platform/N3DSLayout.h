/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include <cstdint>

// The 3DS port renders one 400x480 screen: rows 0-239 are shown on the top screen, rows
// 240-479 on the bottom (touch) screen, of which columns 40-359 are visible. All windows,
// the toolbar and anything else that has to be touched lives in the bottom rectangle.
namespace OpenRCT2::N3DS
{
    constexpr int32_t kTopScreenWidth = 400;
    constexpr int32_t kScreenHeight = 240;
    constexpr int32_t kBottomScreenWidth = 320;
    constexpr int32_t kBottomScreenX = 40;
    constexpr int32_t kBottomScreenY = 240;
    constexpr int32_t kVirtualWidth = 400;
    constexpr int32_t kVirtualHeight = 480;
    constexpr int32_t kBottomScreenRight = kBottomScreenX + kBottomScreenWidth; // 360
    constexpr int32_t kBottomScreenBottom = kBottomScreenY + kScreenHeight;     // 480
} // namespace OpenRCT2::N3DS
