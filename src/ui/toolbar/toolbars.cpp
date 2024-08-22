// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 *
 * A container for tool toolbars, displaying one toolbar at a time.
 *
 *//*
 * Authors:
 *  Tavmjong Bah
 *  Alex Valavanis
 *  Mike Kowalski
 * 
 * Copyright (C) 2023 Tavmjong Bah
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "toolbars.h"

#include <iostream>

#include <glibmm/i18n.h>
#include <gtkmm.h>

// Access create() functions
#include "ui/toolbar/arc-toolbar.h"
#include "ui/toolbar/box3d-toolbar.h"
#include "ui/toolbar/calligraphy-toolbar.h"
#include "ui/toolbar/connector-toolbar.h"
#include "ui/toolbar/dropper-toolbar.h"
#include "ui/toolbar/eraser-toolbar.h"
#include "ui/toolbar/gradient-toolbar.h"
#include "ui/toolbar/lpe-toolbar.h"
#include "ui/toolbar/mesh-toolbar.h"
#include "ui/toolbar/measure-toolbar.h"
#include "ui/toolbar/node-toolbar.h"
#include "ui/toolbar/booleans-toolbar.h"
#include "ui/toolbar/rect-toolbar.h"
#include "ui/toolbar/marker-toolbar.h"
#include "ui/toolbar/page-toolbar.h"
#include "ui/toolbar/paintbucket-toolbar.h"
#include "ui/toolbar/pencil-toolbar.h"
#include "ui/toolbar/select-toolbar.h"
#include "ui/toolbar/spray-toolbar.h"
#include "ui/toolbar/spiral-toolbar.h"
#include "ui/toolbar/star-toolbar.h"
#include "ui/toolbar/tweak-toolbar.h"
#include "ui/toolbar/text-toolbar.h"
#include "ui/toolbar/zoom-toolbar.h"

#include "ui/tools/tool-base.h"
#include "ui/widget/style-swatch.h"

#include "ui/util.h"  // Icon sizes

// Data for building and tracking toolbars.
static struct {
    gchar const *type_name; // Used by preferences
    Glib::ustring const tool_name;
    GtkWidget *(*create_func)(SPDesktop *desktop);
    gchar const *swatch_tip;
} const aux_toolboxes[] = {
    // If you change the tool_name for Measure or Text here, change it also in desktop-widget.cpp.
    // clang-format off
    { "/tools/select",          "Select",       Inkscape::UI::Toolbar::SelectToolbar::create,        nullptr},
    { "/tools/nodes",           "Node",         Inkscape::UI::Toolbar::NodeToolbar::create,          nullptr},
    { "/tools/booleans",        "Booleans",     Inkscape::UI::Toolbar::BooleansToolbar::create,      nullptr},
    { "/tools/marker",          "Marker",       Inkscape::UI::Toolbar::MarkerToolbar::create,        nullptr},
    { "/tools/shapes/rect",     "Rect",         Inkscape::UI::Toolbar::RectToolbar::create,          N_("Style of new rectangles")},
    { "/tools/shapes/arc",      "Arc",          Inkscape::UI::Toolbar::ArcToolbar::create,           N_("Style of new ellipses")},
    { "/tools/shapes/star",     "Star",         Inkscape::UI::Toolbar::StarToolbar::create,          N_("Style of new stars")},
    { "/tools/shapes/3dbox",    "3DBox",        Inkscape::UI::Toolbar::Box3DToolbar::create,         N_("Style of new 3D boxes")},
    { "/tools/shapes/spiral",   "Spiral",       Inkscape::UI::Toolbar::SpiralToolbar::create,        N_("Style of new spirals")},
    { "/tools/freehand/pencil", "Pencil",       Inkscape::UI::Toolbar::PencilToolbar::create_pencil, N_("Style of new paths created by Pencil")},
    { "/tools/freehand/pen",    "Pen",          Inkscape::UI::Toolbar::PencilToolbar::create_pen,    N_("Style of new paths created by Pen")},
    { "/tools/calligraphic",    "Calligraphic", Inkscape::UI::Toolbar::CalligraphyToolbar::create,   N_("Style of new calligraphic strokes")},
    { "/tools/text",            "Text",         Inkscape::UI::Toolbar::TextToolbar::create,          nullptr},
    { "/tools/gradient",        "Gradient",     Inkscape::UI::Toolbar::GradientToolbar::create,      nullptr},
    { "/tools/mesh",            "Mesh",         Inkscape::UI::Toolbar::MeshToolbar::create,          nullptr},
    { "/tools/zoom",            "Zoom",         Inkscape::UI::Toolbar::ZoomToolbar::create,          nullptr},
    { "/tools/measure",         "Measure",      Inkscape::UI::Toolbar::MeasureToolbar::create,       nullptr},
    { "/tools/dropper",         "Dropper",      Inkscape::UI::Toolbar::DropperToolbar::create,       nullptr},
    { "/tools/tweak",           "Tweak",        Inkscape::UI::Toolbar::TweakToolbar::create,         N_("Color/opacity used for color tweaking")},
    { "/tools/spray",           "Spray",        Inkscape::UI::Toolbar::SprayToolbar::create,         nullptr},
    { "/tools/connector",       "Connector",    Inkscape::UI::Toolbar::ConnectorToolbar::create,     nullptr},
    { "/tools/pages",           "Pages",        Inkscape::UI::Toolbar::PageToolbar::create,          nullptr},
    { "/tools/paintbucket",     "Paintbucket",  Inkscape::UI::Toolbar::PaintbucketToolbar::create,   N_("Style of Paint Bucket fill objects")},
    { "/tools/eraser",          "Eraser",       Inkscape::UI::Toolbar::EraserToolbar::create,        _("TBD")},
    { "/tools/lpetool",         "LPETool",      Inkscape::UI::Toolbar::LPEToolbar::create,           _("TBD")},
    { nullptr,                  "",             nullptr,                                             nullptr }
    // clang-format on
};

namespace Inkscape::UI::Toolbar {

// We only create an empty box, it is filled later after the desktop is created.
Toolbars::Toolbars()
    : Gtk::Box(Gtk::ORIENTATION_VERTICAL)
{
    set_name("Tool-Toolbars");
}

// Fill the toolbars widget with toolbars.
// Toolbars are contained inside a grid with an optional swatch.
void Toolbars::create_toolbars(SPDesktop* desktop) {

    Inkscape::Preferences *prefs = Inkscape::Preferences::get();

    // Create the toolbars using their "create" methods.
    for (int i = 0 ; aux_toolboxes[i].type_name ; i++ ) {

        if (aux_toolboxes[i].create_func) {

            // Change create_func to return Gtk::Box!
            GtkWidget* sub_toolbox_c = aux_toolboxes[i].create_func(desktop);
            Gtk::Toolbar* sub_toolbox = Glib::wrap(GTK_TOOLBAR(sub_toolbox_c));
            sub_toolbox->set_name("SubToolBox");

            //               ===== Styling =====

            // Center buttons to prevent stretching; all buttons will look
            // uniform across toolbars if their original size is preserved.
            for (auto&& button : sub_toolbox->get_children()) {
                if (dynamic_cast<Gtk::Button*>(button) ||
                    dynamic_cast<Gtk::SpinButton*>(button) ||
                    dynamic_cast<Gtk::ToolButton*>(button)) {  // FIXME FOR GTK4
                    button->set_valign(Gtk::ALIGN_CENTER);
                    button->set_halign(Gtk::ALIGN_CENTER);
                }
            }

            if ( prefs->getBool( "/toolbox/icononly", true) ) {
                sub_toolbox->set_toolbar_style(Gtk::TOOLBAR_ICONS);
            }

            // TODO Remove and rely on CSS (add class)
            int pixel_size = prefs->getIntLimited("/toolbox/controlbars/icons_size", 16, 16, 48);
            Inkscape::UI::set_icon_sizes(sub_toolbox, pixel_size);

            sub_toolbox->set_hexpand(true);

            //             ===== End Styling =====

            // Use a grid to wrap the toolbar and a possible swatch.
            auto const grid = Gtk::make_managed<Gtk::Grid>();

            // Store a pointer to the grid so we can show/hide it as the tool changes.
            toolbar_map[aux_toolboxes[i].tool_name] = grid;

            Glib::ustring ui_name = aux_toolboxes[i].tool_name + "Toolbar";  // If you change "Toolbar" here, change it also in desktop-widget.cpp.
            grid->set_name(ui_name);

            grid->attach(*sub_toolbox, 0, 0, 1, 1);

            // Add a swatch widget if swatch tooltip is defined.
            if ( aux_toolboxes[i].swatch_tip) {
                auto const swatch = Gtk::make_managed<Inkscape::UI::Widget::StyleSwatch>( nullptr, _(aux_toolboxes[i].swatch_tip) );
                swatch->setDesktop( desktop );
                swatch->setToolName(aux_toolboxes[i].tool_name);
                swatch->setWatchedTool( aux_toolboxes[i].type_name, true );

                //               ===== Styling =====
                // TODO: Remove and use CSS
                swatch->set_margin_start(7);
                swatch->set_margin_end(7);
                swatch->set_margin_top(3);
                swatch->set_margin_bottom(3);
                //             ===== End Styling =====

                grid->attach(*swatch, 1, 0, 1, 1);
            }

            grid->show_all();

            add(*grid);

        } else if (aux_toolboxes[i].swatch_tip) {

            std::cerr << "Toolbars::create_toolbars: Coould not create: "
                      << aux_toolboxes[i].tool_name
                      << std::endl;
        }
    }

    desktop->connectEventContextChanged(sigc::mem_fun(*this, &Toolbars::change_toolbar));

    // Show initial toolbar, hide others.
    change_toolbar(desktop, desktop->event_context);

    // Show this widget (not necessary in Gtk4).
    set_visible(true);
}

void Toolbars::change_toolbar(SPDesktop* /* desktop */, Inkscape::UI::Tools::ToolBase *eventcontext)
{
    if (eventcontext == nullptr) {
        std::cerr << "Toolbars::change_toolbar: eventcontext is null!" << std::endl;
        return;
    }

    for (int i = 0; aux_toolboxes[i].type_name; i++) {
        if (eventcontext->getPrefsPath() == aux_toolboxes[i].type_name) {
            toolbar_map[aux_toolboxes[i].tool_name]->show_now();
        } else {
            toolbar_map[aux_toolboxes[i].tool_name]->set_visible(false);
        }
    }
}

} // namespace

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
