// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * @brief New From Template main dialog - implementation
 */
/* Authors:
 *   Jan Darowski <jan.darowski@gmail.com>, supervised by Krzysztof Kosiński
 *
 * Copyright (C) 2013 Authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "new-from-template.h"

#include <glibmm/i18n.h>

#include "desktop.h"
#include "file.h"
#include "inkscape-application.h"
#include "inkscape-window.h"
#include "inkscape.h"
#include "object/sp-namedview.h"
#include "ui/widget/template-list.h"
#include "ui/dialog-run.h"

namespace Inkscape {
namespace UI {


NewFromTemplate::NewFromTemplate()
    : _create_template_button(_("Create from template"))
{
    set_title(_("New From Template"));
    resize(750, 500);

    templates = Gtk::make_managed<Inkscape::UI::Widget::TemplateList>();
    get_content_area()->pack_start(*templates);
    templates->init(Inkscape::Extension::TEMPLATE_NEW_FROM);

    _create_template_button.set_halign(Gtk::ALIGN_END);
    _create_template_button.set_valign(Gtk::ALIGN_END);
    _create_template_button.set_margin_end(15);

    get_content_area()->pack_end(_create_template_button, Gtk::PACK_SHRINK);
    
    _create_template_button.signal_clicked().connect(
    sigc::mem_fun(*this, &NewFromTemplate::_createFromTemplate));
    _create_template_button.set_sensitive(false);

    templates->connectItemSelected([=]() { _create_template_button.set_sensitive(true); });
    templates->connectItemActivated(sigc::mem_fun(*this, &NewFromTemplate::_createFromTemplate));
    templates->signal_switch_page().connect([=](Gtk::Widget *const widget, int num) {
        _create_template_button.set_sensitive(templates->has_selected_preset());
    });

    show_all();
}

void NewFromTemplate::_createFromTemplate()
{
    SPDesktop *old_desktop = SP_ACTIVE_DESKTOP;

    auto doc = templates->new_document();

    // Cancel button was pressed.
    if (!doc)
        return;

    auto app = InkscapeApplication::instance();
    InkscapeWindow *win = app->window_open(doc);
    SPDesktop *new_desktop = win->get_desktop();
    sp_namedview_window_from_document(new_desktop);

    if (old_desktop)
        old_desktop->clearWaitingCursor();

    _onClose();
}

void NewFromTemplate::_onClose()
{
    response(0);
}

void NewFromTemplate::load_new_from_template()
{
    NewFromTemplate dl;
    Inkscape::UI::dialog_run(dl);
}

}
}
