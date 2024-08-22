// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Utilities to for working with ICC profiles. Used by CMSSystem and ColorProfile classes.
 *//*
 * Authors: see git history
 *
 * Copyright (C) 2018 Authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef INKSCAPE_COLOR_CMS_UTIL_H
#define INKSCAPE_COLOR_CMS_UTIL_H

#include <string>

#include <lcms2.h>

namespace Inkscape {

// Helper class to store info
class ICCProfileInfo
{
public:
    ICCProfileInfo(cmsHPROFILE profile, std::string &&path, bool in_home);
    bool operator<(ICCProfileInfo const &other) const;
    std::string const &get_path() const { return _path; }
    std::string const &get_name() const { return _name; }
    cmsColorSpaceSignature get_colorspace() const { return _colorspace; }
    cmsProfileClassSignature get_profileclass() const { return _profileclass; }
    bool in_home() const { return _in_home; }

private:
    std::string _path;
    std::string _name;
    bool _in_home;
    cmsColorSpaceSignature _colorspace;
    cmsProfileClassSignature _profileclass;
};

bool is_icc_file(std::string const &filepath);
std::string get_color_profile_name(cmsHPROFILE profile); // Read as ASCII from profile.

} // namespace Inkscape

#endif // INKSCAPE_COLOR_CMS_UTIL_H

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:fileencoding=utf-8:textwidth=99 :
