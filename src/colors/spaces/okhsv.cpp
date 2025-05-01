// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 *//*
 * Authors:
 *   Mike Kowalski
 *
 * Copyright (C) 2025 Authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "okhsv.h"
#include <algorithm>

#include "colors/spaces/ok-color.h"

namespace Inkscape::Colors::Space {

void OkHsv::spaceToProfile(std::vector<double>& output) const {
    auto h = static_cast<float>(std::clamp(output[0], 0.0, 1.0));
    auto s = static_cast<float>(std::clamp(output[1], 0.0, 1.0));
    auto v = static_cast<float>(std::clamp(output[2], 0.0, 1.0));
    ok_color::HSV hsv{h, s, v};
    auto rgb = ok_color::okhsv_to_srgb(hsv);
    output[0] = rgb.r;
    output[1] = rgb.g;
    output[2] = rgb.b;
}

void OkHsv::profileToSpace(std::vector<double>& output) const {
    ok_color::RGB rgb{(float)output[0], (float)output[1], (float)output[2]};
    auto hsv = ok_color::srgb_to_okhsv(rgb);
    output[0] = hsv.h;
    output[1] = hsv.s;
    output[2] = hsv.v;
}

} // namespace Inkscape::Colors::Space
