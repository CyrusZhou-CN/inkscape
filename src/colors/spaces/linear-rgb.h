// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Authors:
 *   Martin Owens <doctormo@geek-2.com>
 *
 * Copyright (C) 2023 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef SEEN_COLORS_SPACES_LINEARRGB_H
#define SEEN_COLORS_SPACES_LINEARRGB_H

#include "rgb.h"

namespace Inkscape::Colors::Space {

class LinearRGB : public RGB
{
public:
    LinearRGB(): RGB(Type::linearRGB, 3, "linearRGB", "linearRGB", "color-selector-linear-rgb") {}
    ~LinearRGB() override = default;

protected:
    friend class Inkscape::Colors::Color;

    void spaceToProfile(std::vector<double> &output) const override { LinearRGB::toRGB(output); }
    void profileToSpace(std::vector<double> &output) const override { LinearRGB::fromRGB(output); }

    std::string toString(std::vector<double> const &values, bool opacity = true) const override;

public:
    static void toRGB(std::vector<double> &output);
    static void fromRGB(std::vector<double> &output);
};

} // namespace Inkscape::Colors::Space

#endif // SEEN_COLORS_SPACES_LINEARRGB_H
