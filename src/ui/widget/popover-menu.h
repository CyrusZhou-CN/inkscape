// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * A replacement for GTK3ʼs Gtk::Menu, as removed in GTK4.
 */
/*
 * Authors:
 *   Daniel Boles <dboles.src+inkscape@gmail.com>
 *
 * Copyright (C) 2023 Daniel Boles
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef SEEN_UI_WIDGET_POPOVER_MENU_H
#define SEEN_UI_WIDGET_POPOVER_MENU_H

#include <gtkmm/popover.h>

namespace Inkscape::UI::Widget {

/// Gtk::Grid subclass provides CSS name `menu`
class PopoverMenuGrid;

class PopoverMenuItem;

/// A replacement for GTK3ʼs Gtk::Menu, as removed in GTK4.
/// Aim is to be a minimal but mostly “drop-in” replacement
/// for Menus, including grid and activation functionality.
class PopoverMenu final : public Gtk::Popover {
public:
    /// Construct popover with `.menu` CSS class, set to be
    /// positioned below the :relative-to/popup_at() widget
    [[nodiscard]] PopoverMenu();

    /// Add child at pos as per Gtk::Menu::attach()
    void attach(Gtk::Widget &child,
                int left_attach, int right_attach,
                int top_attach, int bottom_attach);
    /// Add new row containing child
    void append(Gtk::Widget &child);
    /// Remove/unparent added child.
    void remove(Gtk::Widget &child);

    /// Replaces Gtk::Menu::popup_at_pointer
    void popup_at(Gtk::Widget &relative_to,
                  int x_offset = 0, int y_offset = 0);

private:
    PopoverMenuGrid &_grid;
    bool _restore_tooltip = false;

    // Let PopoverMenuItem call this without making it public API
    friend class PopoverMenuItem;
    void unset_items_focus_hover(Gtk::Widget *except_active);
};

} // namespace Inkscape::UI::Widget

#endif // SEEN_UI_WIDGET_POPOVER_MENU_H

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
