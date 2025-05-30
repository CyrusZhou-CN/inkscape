// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Authors:
 *   Martin Owens <doctormo@geek-2.com>
 *
 * Copyright (C) 2023-2025 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef SEEN_COLORS_SPACES_BASE_H
#define SEEN_COLORS_SPACES_BASE_H

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "colors/parser.h"
#include "enum.h"
#include "colors/cms/profile.h"

constexpr double SCALE_UP(double v, double a, double b)
{
    return (v * (b - a)) + a;
}
constexpr double SCALE_DOWN(double v, double a, double b)
{
    return (v - a) / (b - a);
}

namespace Inkscape::Colors {
namespace CMS {
class Profile;
class Transform;
} // namespace CMS
class Color;
class Manager;

namespace Space {

class Components;

class AnySpace : public std::enable_shared_from_this<AnySpace>
{
public:
    virtual ~AnySpace() = default;

    bool operator==(AnySpace const &other) const { return other.getName() == getName(); }
    bool operator!=(AnySpace const &other) const { return !(*this == other); };

    Type getType() const { return _type; }
    std::string const& getName() const { return _name; };
    std::string const& getShortName() const { return _shortName; };
    std::string const& getIcon() const { return _icon; }
    virtual Type getComponentType() const { return getType(); }
    virtual unsigned int getComponentCount() const { return _components; }
    virtual std::shared_ptr<Colors::CMS::Profile> const getProfile() const = 0;
    virtual RenderingIntent getIntent() const { return RenderingIntent::UNKNOWN; }
    // Some color spaces (like XYZ or LAB) to not put restrictions on valid ranges of values;
    // others (like sRGB) do, which means that channels outside those bounds represent colors out of gamut.
    bool isUnbounded() const { return _spaceIsUnbounded; }
    // Check if 'color' is out of gamut in '*this' color space;
    // use epsilon value to ignore some small deviations from valid domain (they can arise during conversions).
    bool isOutOfGamut(const Colors::Color& color, double eps = 0.0001);
    // Bring 'color' into gamut of '*this' color space
    Color toGamut(const Colors::Color& color);

    Components const &getComponents(bool alpha = false) const;
    std::string const getPrefsPath() const { return "/colorselector/" + getName() + "/"; }

    virtual bool isValid() const { return true; }

protected:
    friend class Colors::Color;

    AnySpace(Type type, int components, std::string name, std::string shortName, std::string icon, bool spaceIsUnbounded = false);

    bool isValidData(std::vector<double> const &values) const;
    virtual std::vector<Parser> getParsers() const { return {}; }
    virtual std::string toString(std::vector<double> const &values, bool opacity = true) const = 0;
    virtual uint32_t toRGBA(std::vector<double> const &values, double opacity = 1.0) const = 0;

    bool convert(std::vector<double> &io, std::shared_ptr<AnySpace> to_space) const;
    bool profileToProfile(std::vector<double> &io, std::shared_ptr<AnySpace> to_space) const;
    virtual void spaceToProfile(std::vector<double> &io) const;
    virtual void profileToSpace(std::vector<double> &io) const;
    virtual bool overInk(std::vector<double> const &input) const { return false; }

    std::shared_ptr<Colors::CMS::Profile> srgb_profile = Colors::CMS::Profile::create_srgb();

    bool outOfGamut(std::vector<double> const &input, std::shared_ptr<AnySpace> to_space) const;

private:
    mutable std::map<std::string, std::shared_ptr<Colors::CMS::Transform>> _transforms;
    mutable std::map<std::string, std::shared_ptr<Colors::CMS::Transform>> _gamut_checkers;
    std::string _name;
    std::string _shortName;
    std::string _icon;
    Type _type;
    int _components;
    bool _spaceIsUnbounded;
};

} // namespace Space
} // namespace Inkscape::Colors

#endif // SEEN_COLORS_SPACES_BASE_H
