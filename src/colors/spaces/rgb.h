// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Authors:
 *   Martin Owens <doctormo@geek-2.com>
 *
 * Copyright (C) 2023 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef SEEN_COLORS_SPACES_RGB_H
#define SEEN_COLORS_SPACES_RGB_H

#include "base.h"

namespace Inkscape::Colors::Space {

class RGB : public AnySpace
{
public:
    RGB(): AnySpace(Type::RGB, 3, "RGB", "RGB", "color-selector-rgb") {}
    ~RGB() override = default;

    std::shared_ptr<Colors::CMS::Profile> const getProfile() const override;

protected:
    RGB(Type type, int components, std::string name, std::string shortName, std::string icon, bool spaceIsUnbounded = false);

    friend class Inkscape::Colors::Color;

    std::string toString(std::vector<double> const &values, bool opacity = true) const override;
    uint32_t toRGBA(std::vector<double> const &values, double opacity = 1.0) const override;

public:
    class Parser : public LegacyParser
    {
    public:
        Parser(bool alpha)
            : LegacyParser("rgb", Type::RGB, alpha)
        {}
        bool parse(std::istringstream &input, std::vector<double> &output) const override;
    };

private:
    uint32_t _to_rgba(std::vector<double> const &values, double opacity) const;
};

} // namespace Inkscape::Colors::Space

#endif // SEEN_COLORS_SPACES_RGB_H
