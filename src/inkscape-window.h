// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * Inkscape - An SVG editor.
 */
/*
 * Authors:
 *   Tavmjong Bah
 *
 * Copyright (C) 2018 Authors
 *
 * The contents of this file may be used under the GNU General Public License Version 2 or later.
 * Read the file 'COPYING' for more information.
 *
 */

#ifndef INKSCAPE_WINDOW_H
#define INKSCAPE_WINDOW_H

#include <gtkmm/applicationwindow.h>

namespace Gtk { class Box; }

class InkscapeApplication;
class SPDocument;
class SPDesktop;
class SPDesktopWidget;

class InkscapeWindow : public Gtk::ApplicationWindow {
public:
    InkscapeWindow(SPDocument* document);
    ~InkscapeWindow() override;

    SPDocument*      get_document()       { return _document; }
    SPDesktop*       get_desktop()        { return _desktop; }
    SPDesktopWidget* get_desktop_widget() { return _desktop_widget; }

    void change_document(SPDocument* document);

private:
    InkscapeApplication *_app = nullptr;

    SPDocument*          _document = nullptr;
    SPDesktop*           _desktop = nullptr;
    SPDesktopWidget*     _desktop_widget = nullptr;

    Gtk::Box*      _mainbox = nullptr;

    void setup_view();

    void add_document_actions();

    // Callbacks
public:
    bool on_key_press_event(GdkEventKey* event) override;
private:
    bool on_focus_in_event(GdkEventFocus* event) override;
    bool on_delete_event(GdkEventAny* event) override;
    bool on_configure_event(GdkEventConfigure *event) override;

    // Helpers
    void update_dialogs();
};

#endif // INKSCAPE_WINDOW_H

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4 :
