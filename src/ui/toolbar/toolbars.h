// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 *
 * A container for tool toolbars, displaying one toolbar at a time.
 *
 *//*
 * Authors: Tavmjong Bah
 *
 * Copyright (C) 2023 Tavmjong Bah
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */
#ifndef SEEN_TOOLBARS_H
#define SEEN_TOOLBARS_H

#include <map>

#include <gtkmm/box.h>

class SPDesktop;

namespace Gtk {
  class Grid;
}

namespace Inkscape::UI::Tools {
  class ToolBase;
}

namespace Inkscape::UI::Toolbar {

/**
 * \brief A container for tool toolbars.
 *
 * \detail A container for tool toolbars that display one toolbar at a time.
 *         The container tracks which toolbar is shown.
 */
class Toolbars : public Gtk::Box {

public:
    Toolbars();
    ~Toolbars() override = default;

    void create_toolbars(SPDesktop* desktop);
    void change_toolbar(SPDesktop* desktop, Inkscape::UI::Tools::ToolBase* eventcontext);

private:
    std::map<Glib::ustring, Gtk::Grid*> toolbar_map;
    GtkWidget* current_toolbar = nullptr;
};

} // Namespace

#endif
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
