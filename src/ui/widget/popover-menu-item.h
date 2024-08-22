// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * A replacement for GTK3ʼs Gtk::MenuItem, as removed in GTK4.
 */
/*
 * Authors:
 *   Daniel Boles <dboles.src+inkscape@gmail.com>
 *
 * Copyright (C) 2023 Daniel Boles
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef SEEN_UI_WIDGET_POPOVER_MENU_ITEM_H
#define SEEN_UI_WIDGET_POPOVER_MENU_ITEM_H

#include <glibmm/ustring.h>
#include <gtkmm/button.h>
#include <gtkmm/enums.h> // Gtk::IconSize
#include "ui/widget/css-name-class-init.h"

namespace Inkscape::UI::Widget {

class PopoverMenu;

/// A replacement for GTK3ʼs Gtk::MenuItem, as removed in GTK4.
/// Aim is to be a minimal but mostly “drop-in” replacement.
class PopoverMenuItem final
    : public CssNameClassInit
    , public Gtk::Button
{
public:
    // Construct a flat Button with CSS name `menuitem` and class `.menuitem`.
    [[nodiscard]] explicit PopoverMenuItem(Glib::ustring const &label_with_mnemonic = {},
                                           Glib::ustring const &icon_name = {},
                                           Gtk::IconSize const icon_size = Gtk::ICON_SIZE_MENU,
                                           bool popdown_on_activate = true);

    /// A convenience, “drop-in” alias for signal_clicked().
    [[nodiscard]] Glib::SignalProxy<void> signal_activate();

private:
    [[nodiscard]] PopoverMenu *get_menu();
};

} // namespace Inkscape::UI::Widget

#endif // SEEN_UI_WIDGET_POPOVER_MENU_ITEM_H

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
