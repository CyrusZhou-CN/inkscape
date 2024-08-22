// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Utilities to for working with ICC profiles. Used by CMSSystem and ColorProfile classes.
 *//*
 * Authors: see git history
 *
 * Copyright (C) 2018 Authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <cassert>
#include <iostream> // Debug output
#include <vector>
#include <fcntl.h> // File open flags
#include <unistd.h> // Posix read, close.

#include <glib/gstdio.h>
#include <glib/gi18n.h>

#include "cms-util.h"

namespace Inkscape {

ICCProfileInfo::ICCProfileInfo(cmsHPROFILE profile, std::string &&path, bool in_home)
    : _path(std::move(path))
    , _in_home(in_home)
{
    assert(profile);

    _name = get_color_profile_name(profile);
    _colorspace = cmsGetColorSpace(profile);
    _profileclass = cmsGetDeviceClass(profile);
}

bool is_icc_file(std::string const &filepath)
{
    bool is_icc_file = false;
    GStatBuf st;
    if (g_stat(filepath.c_str(), &st) == 0 && st.st_size > 128) {
        // 0-3 == size
        // 36-39 == 'acsp' 0x61637370
        int fd = g_open(filepath.c_str(), O_RDONLY, S_IRWXU);
        if (fd != -1) {
            guchar scratch[40] = {0};
            size_t len = sizeof(scratch);

            ssize_t got = read(fd, scratch, len);
            if (got != -1) {
                size_t calcSize =
                    (scratch[0] << 24) |
                    (scratch[1] << 16) |
                    (scratch[2] << 8)  |
                    (scratch[3]);
                if ( calcSize > 128 && calcSize <= static_cast<size_t>(st.st_size) ) {
                    is_icc_file =
                        (scratch[36] == 'a') &&
                        (scratch[37] == 'c') &&
                        (scratch[38] == 's') &&
                        (scratch[39] == 'p');
                }
            }
            close(fd);

            if (is_icc_file) {
                cmsHPROFILE profile = cmsOpenProfileFromFile(filepath.c_str(), "r");
                if (profile) {
                    cmsProfileClassSignature profClass = cmsGetDeviceClass(profile);
                    if (profClass == cmsSigNamedColorClass) {
                        is_icc_file = false; // Ignore named color profiles for now.
                    }
                    cmsCloseProfile(profile);
                }
            }
        }
    }

    return is_icc_file;
}

std::string get_color_profile_name(cmsHPROFILE profile)
{
    std::string name;

    if (profile) {
        cmsUInt32Number byteLen = cmsGetProfileInfoASCII(profile, cmsInfoDescription, "en", "US", nullptr, 0);
        if (byteLen > 0) {
            std::vector<char> data(byteLen);
            cmsUInt32Number readLen = cmsGetProfileInfoASCII(profile, cmsInfoDescription,
                                                             "en", "US",
                                                             data.data(), data.size());
            if (readLen < data.size()) {
                std::cerr << "get_color_profile_name(): read less than expected!" << std::endl;
                data.resize(readLen);
            }

            // Remove nulls at end which will cause an invalid utf8 string.
            while (!data.empty() && data.back() == 0) {
                data.pop_back();
            }

            name = std::string(data.begin(), data.end());
        }

        if (name.empty()) {
            name = _("(Unnamed)");
        }
    }

    return name;
}

} // namespace Inkscape

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
