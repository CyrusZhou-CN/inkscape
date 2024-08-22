// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef SEEN_TOOLBAR_SNAP_H
#define SEEN_TOOLBAR_SNAP_H

/*
 * Authors:
 *   Tavmjong Bah
 *
 * Copyright (C) 2023 Tavmjong Bah
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <gtkmm.h>

#include "preferences.h"

class InkscapeWindow;
class SPDesktop;

namespace Inkscape::UI::Toolbar {

class SnapToolbar : public Gtk::Box {
public:
    SnapToolbar();
    ~SnapToolbar() override = default;
    void mode_update();

private:
    static int show_popover(void* button);
    Inkscape::PrefObserver _observer;

    // Order defined by preferences (legacy).
    enum Mode {
        ADVANCED,
        SIMPLE,
        PERMANENT,
        UNDEFINED
    };
    Mode mode = Mode::UNDEFINED;

    // The widgets to show/hide depending on mode.
    Gtk::Box*    snap_toolbar = nullptr;
    Gtk::Button* btn_simple = nullptr;
    Gtk::Button* btn_advanced = nullptr;
    Gtk::ScrolledWindow* scroll_permanent = nullptr;
    Gtk::Box*    box_permanent = nullptr;
};

} // namespace Inkscape::UI::Toolbar


#endif /* SEEN_TOOLBAR_SNAP_H */

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
