// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Toolbar for Commands.
 */
/* Authors:
 *   Tavmjong Bah
 *
 * Copyright (C) 2023 Tavmjong Bah
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "command-toolbar.h"

#include "ui/builder-utils.h"

namespace Inkscape::UI::Toolbar {

CommandToolbar::CommandToolbar()
    : Gtk::Box()
{
    set_name("CommandToolbar");
    set_homogeneous(false);

    Gtk::Toolbar* command_toolbar = nullptr;

    auto builder = Inkscape::UI::create_builder("toolbar-commands.ui"); // Note 's'
    builder->get_widget("commands-toolbar", command_toolbar);
    if (!command_toolbar) {
        std::cerr << "CommandToolbar: Failed to load command toolbar!" << std::endl;
        return;
    }

    // REMOVE AFTER CONVERSION FROM GTK::TOOLBAR
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    if ( prefs->getBool("/toolbox/icononly", true) ) {
        command_toolbar->set_toolbar_style( Gtk::TOOLBAR_ICONS );
    }

    pack_start(*command_toolbar, false, false);
}

} // namespace Inkscape::UI::Toolbar

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
