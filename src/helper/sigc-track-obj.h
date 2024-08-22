// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef INKSCAPE_HELPER_SIGC_TRACK_OBJ_H
#define INKSCAPE_HELPER_SIGC_TRACK_OBJ_H

/**
 * @file
 * Macros to handle API transition in libsigc++, replacing the function template
 * sigc::track_obj with sigc::track_object.
 *
 * Specifically, this file provides the macro SIGC_TRACKING_ADAPTOR which expands
 * to the correct identifier (including the sigc:: namespace qualification).
 */
/*
 * Author:
 *   Rafael Siejakowski <rs@rs-math.net>
 *
 * Copyright (C) 2023 Rafael Siejakowski
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <sigc++/sigc++.h>

// The replacement of sigc::track_obj with sigc::track_object takes place:
// - in sigc++ 2.12 for major version 2,
// - in sigc++ 3.4  for major version 3.
#if SIGCXX_MAJOR_VERSION < 2
#    define USE_SIGCXX_TRACK_OBJ 1
#elif SIGCXX_MAJOR_VERSION == 2
#    if SIGCXX_MINOR_VERSION < 12
#        define USE_SIGCXX_TRACK_OBJ 1
#    else
#        define USE_SIGCXX_TRACK_OBJ 0
#    endif
#elif SIGCXX_MAJOR_VERSION == 3
#    if SIGCXX_MINOR_VERSION < 4
#        define USE_SIGCXX_TRACK_OBJ 1
#    else
#        define USE_SIGCXX_TRACK_OBJ 0
#    endif
#else
#    define USE_SIGCXX_TRACK_OBJ 0
#endif

#if USE_SIGCXX_TRACK_OBJ
#    define SIGC_TRACKING_ADAPTOR (sigc::track_obj)
#else
#    define SIGC_TRACKING_ADAPTOR (sigc::track_object)
#endif

#undef USE_SIGCXX_TRACK_OBJ

#endif // INKSCAPE_HELPER_SIGC_TRACK_OBJ_H

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
