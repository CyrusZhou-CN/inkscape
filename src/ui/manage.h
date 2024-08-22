// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Helper function to tie lifetime of a RefPtr-owned object to a managing object
 */
/*
 * Authors:
 *   Daniel Boles <dboles.src+inkscape@gmail.com>
 *
 * Copyright (C) 2023 Daniel Boles
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef SEEN_UI_MANAGE_HPP
#define SEEN_UI_MANAGE_HPP

#include <glibmm/refptr.h>

namespace Glib {
class ObjectBase;
}

namespace Inkscape::UI {

/* Glib::Binding overrides GBindingʼs reference-counting. GBinding will be
   freed when either of the bound objects is or when unbind() is called on it.
   However, Glib::Binding is only unreferenced when the final RefPtr to it goes
   out of scope. So, keep a RefPtr to each binding for each bound object. The
   end result of all this is basically getting the C-style lifetime management
   back… so that bindings are automatically managed by their objects in gtkmmui.
   The same goes for Gtk::Gesture, which normally needs a RefPtr kept around.
   This will not be needed in gtkmm 4; Bindings and Gestures are managed there.
   See https://gitlab.gnome.org/GNOME/glibmm/issues/62 for discussion on this
   and controller.h for a practical example of how it is used. */

/// Ensure that a managed object will remain referenced as long as a manager is.
/// This can be used to tie lifetime of Gtk::Gestures or Glib::Bindings to their
/// widget/object(s), instead of having to keep a RefPtr as is otherwise needed.
void manage(Glib::RefPtr<Glib::ObjectBase const> managed,
            Glib::ObjectBase const &manager);

} // namespace Inkscape::UI

#endif // SEEN_UI_MANAGE_HPP

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace .0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim:filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:fileencoding=utf-8:textwidth=99:
