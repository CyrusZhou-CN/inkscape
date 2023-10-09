// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef SEEN_INKSCAPE_UTIL_USTRING_HASH_H
#define SEEN_INKSCAPE_UTIL_USTRING_HASH_H

#include <cstddef>
#include <string>
#include <glibmm/ustring.h>

// TODO: Once we require a new enough version of glibmm, we can use <glibmm/ustring_hash.h> instead

namespace std {

template <>
struct hash<Glib::ustring>
{
    std::size_t operator()(Glib::ustring const &s) const
    {
        return hash<std::string>()(s.raw());
    }
};

} // namespace std

#endif // SEEN_INKSCAPE_UTIL_USTRING_HASH_H

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
