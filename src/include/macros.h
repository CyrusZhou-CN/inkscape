// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef SEEN_MACROS_H
#define SEEN_MACROS_H

#include <gdk/gdk.h>

/**
 * Useful macros for inkscape
 *
 * Author:
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *
 * Copyright (C) 2002 Lauris Kaplinski
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

// "primary" modifier: Ctrl on Linux/Windows and Cmd on macOS.
// note: Could query this at runtime with
// `gdk_keymap_get_modifier_mask(..., GDK_MODIFIER_INTENT_PRIMARY_ACCELERATOR)`
#ifdef GDK_WINDOWING_QUARTZ
inline constexpr auto INK_GDK_PRIMARY_MASK = GDK_MOD2_MASK;
#else
inline constexpr auto INK_GDK_PRIMARY_MASK = GDK_CONTROL_MASK;
#endif
// Todo: (GTK4) Replace INK_GDK_PRIMARY_MASK with GDK_CONTROL_MASK.
// Reference: https://docs.gtk.org/gtk4/migrating-3to4.html#adapt-to-changes-in-keyboard-modifier-handling

// all modifiers used by Inkscape
inline constexpr auto INK_GDK_MODIFIER_MASK = GDK_SHIFT_MASK | INK_GDK_PRIMARY_MASK | GDK_MOD1_MASK;

// keyboard modifiers in an event
inline bool MOD__SHIFT(unsigned modifiers) { return modifiers & GDK_SHIFT_MASK; }
inline bool MOD__CTRL(unsigned modifiers) { return modifiers & INK_GDK_PRIMARY_MASK; }
inline bool MOD__ALT(unsigned modifiers) { return modifiers & GDK_MOD1_MASK; }
inline bool MOD__SHIFT_ONLY(unsigned modifiers) { return (modifiers & INK_GDK_MODIFIER_MASK) == GDK_SHIFT_MASK; }
inline bool MOD__CTRL_ONLY(unsigned modifiers) { return (modifiers & INK_GDK_MODIFIER_MASK) == INK_GDK_PRIMARY_MASK; }
inline bool MOD__ALT_ONLY(unsigned modifiers) { return (modifiers & INK_GDK_MODIFIER_MASK) == GDK_MOD1_MASK; }

inline bool MOD__SHIFT(GdkEvent const *event) { return MOD__SHIFT(event->key.state); }
inline bool MOD__CTRL(GdkEvent const *event) { return MOD__CTRL(event->key.state); }
inline bool MOD__ALT(GdkEvent const *event) { return MOD__ALT(event->key.state); }
inline bool MOD__SHIFT_ONLY(GdkEvent const *event) { return MOD__SHIFT_ONLY(event->key.state); }
inline bool MOD__CTRL_ONLY(GdkEvent const *event) { return MOD__CTRL_ONLY(event->key.state); }
inline bool MOD__ALT_ONLY(GdkEvent const *event) { return MOD__ALT_ONLY(event->key.state); }

#endif // SEEN_MACROS_H

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:fileencoding=utf-8 :
