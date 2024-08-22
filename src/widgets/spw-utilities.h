// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Inkscape Widget Utilities
 *
 * Author:
 *   Bryce W. Harrington <brycehar@bryceharrington.org>
 *
 * Copyright (C) 2003 Bryce Harrington
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

/* The following are helper routines for making Inkscape dialog widgets.
   All are prefixed with spw_, short for inkscape_widget.  This is not to
   be confused with SPWidget, an existing datatype associated with Inkscape::XML::Node/
   SPObject, that reacts to modification.
*/

#ifndef SEEN_SPW_UTILITIES_H
#define SEEN_SPW_UTILITIES_H

#include <glibmm/ustring.h>
#include <functional>

namespace Gtk {
  class Label;
  class Grid;
  class HBox;
  class Widget;
}

Gtk::Label * spw_label(Gtk::Grid *table, gchar const *label_text, int col, int row, Gtk::Widget *target);
Gtk::Box * spw_hbox(Gtk::Grid *table, int width, int col, int row);

Gtk::Widget * sp_search_by_name_recursive(Gtk::Widget          *parent,
                                          const Glib::ustring&  name);

///See ui/util:for_each_child(), a generalisation of this and used as its basis.
Gtk::Widget* sp_traverse_widget_tree(Gtk::Widget* widget, const std::function<bool (Gtk::Widget*)>& eval);

Gtk::Widget* sp_find_focusable_widget(Gtk::Widget* widget);

/// Get string action target, if available.
Glib::ustring sp_get_action_target(Gtk::Widget* widget);

#endif // SEEN_SPW_UTILITIES_H

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
