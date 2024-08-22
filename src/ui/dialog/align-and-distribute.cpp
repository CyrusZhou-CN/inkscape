// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * @brief Align and Distribute widget
 */
/* Authors:
 *   Tavmjong Bah
 *
 *   Based on dialog by:
 *     Bryce W. Harrington <bryce@bryceharrington.org>
 *     Aubanel MONNIER <aubi@libertysurf.fr>
 *     Frank Felfe <innerspace@iname.com>
 *     Lauris Kaplinski <lauris@kaplinski.com>
 *
 * Copyright (C) 2021 Tavmjong Bah
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <iostream>
#include <sigc++/adaptors/bind.h>
#include <sigc++/functors/mem_fun.h>
#include <giomm/application.h>
#include <gtkmm/button.h>
#include <gtkmm/combobox.h>
#include <gtkmm/spinbutton.h>
#include <gtkmm/togglebutton.h>

#include "align-and-distribute.h" // widget
#include "desktop.h"               // Tool switching.
#include "inkscape-window.h"       // Activate window action.
#include "actions/actions-tools.h" // Tool switching.
#include "io/resource.h"
#include "ui/dialog/dialog-base.h" // Tool switching.
#include "ui/util.h"

namespace Inkscape {
namespace UI {
namespace Dialog {

using Inkscape::IO::Resource::get_filename;
using Inkscape::IO::Resource::UIS;

AlignAndDistribute::AlignAndDistribute(Inkscape::UI::Dialog::DialogBase* dlg)
    : Gtk::Box(Gtk::ORIENTATION_VERTICAL)
{
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();

    Glib::ustring builder_file = get_filename(UIS, "align-and-distribute.ui");
    auto builder = Gtk::Builder::create();
    try
    {
        builder->add_from_file(builder_file);
    }
    catch (const Glib::Error& ex)
    {
        std::cerr << "AlignAndDistribute::AlignAndDistribute: " << builder_file.raw() << " file not read! " << ex.what().raw() << std::endl;
    }

    builder->get_widget("align-and-distribute-box", align_and_distribute_box);
    if (!align_and_distribute_box) {
        std::cerr << "AlignAndDistribute::AlignAndDistribute: failed to load widget (box)!" << std::endl;
    } else {
        add(*align_and_distribute_box);
    }

    builder->get_widget("align-and-distribute-object", align_and_distribute_object);
    if (!align_and_distribute_object) {
        std::cerr << "AlignAndDistribute::AlignAndDistribute: failed to load widget (object)!" << std::endl;
    } else {
        align_and_distribute_object->set_visible(true);
    }

    builder->get_widget("align-and-distribute-node", align_and_distribute_node);
    if (!align_and_distribute_node) {
        std::cerr << "AlignAndDistribute::AlignAndDistribute: failed to load widget (node)!" << std::endl;
    } else {
        align_and_distribute_node->set_visible(false);
    }

    // ------------  Object Align  -------------

    builder->get_widget("align-relative-object", align_relative_object);
    if (!align_relative_object) {
        std::cerr << "AlignAndDistribute::AlignAndDistribute: failed to load widget (combobox)!" << std::endl;
    } else {
        std::string align_to = prefs->getString("/dialogs/align/objects-align-to", "selection");
        align_relative_object->set_active_id(align_to);
        align_relative_object->signal_changed().connect(sigc::mem_fun(*this, &AlignAndDistribute::on_align_relative_object_changed));
    }

    builder->get_widget("align-move-as-group", align_move_as_group);
    if (!align_move_as_group) {
        std::cerr << "AlignAndDistribute::AlignAndDistribute: failed to load widget (group button)!" << std::endl;
    } else {
        bool sel_as_group = prefs->getBool("/dialogs/align/sel-as-groups");
        align_move_as_group->set_active(sel_as_group);
        align_move_as_group->signal_clicked().connect(sigc::mem_fun(*this, &AlignAndDistribute::on_align_as_group_clicked));
    }

    // clang-format off
    std::vector<std::pair<std::string, std::string>> align_buttons = {
        {"align-horizontal-right-to-anchor", "right anchor"  },
        {"align-horizontal-left",            "left"          },
        {"align-horizontal-center",          "hcenter"       },
        {"align-horizontal-right",           "right"         },
        {"align-horizontal-left-to-anchor",  "left anchor"   },
        {"align-horizontal-baseline",        "horizontal"    },
        {"align-vertical-bottom-to-anchor",  "bottom anchor" },
        {"align-vertical-top",               "top"           },
        {"align-vertical-center",            "vcenter"       },
        {"align-vertical-bottom",            "bottom"        },
        {"align-vertical-top-to-anchor",     "top anchor"    },
        {"align-vertical-baseline",          "vertical"      }
    };
    // clang-format on

    for (auto align_button: align_buttons) {
        Gtk::Button* button;
        builder->get_widget(align_button.first, button);
        if (!button) {
            std::cerr << "AlignAndDistribute::AlignAndDisribute: failed to get button: "
                      << align_button.first << " " << align_button.second << std::endl;
        } else {
            button->signal_clicked().connect(
                sigc::bind(sigc::mem_fun(*this, &AlignAndDistribute::on_align_clicked), align_button.second));
        }
    }


    // ------------ Remove overlap -------------

    builder->get_widget("remove-overlap-button", remove_overlap_button);
    if (!remove_overlap_button) {
        std::cerr << "AlignAndDistribute::AlignAndDistribute: failed to load widget!" << std::endl;
    } else {
        remove_overlap_button->signal_clicked().connect(
            sigc::mem_fun(*this, &AlignAndDistribute::on_remove_overlap_clicked));
    }

    builder->get_widget("remove-overlap-hgap", remove_overlap_hgap);
    if (!remove_overlap_hgap) {
        std::cerr << "AlignAndDistribute::AlignAndDistribute: failed to load widget!" << std::endl;
    }

    builder->get_widget("remove-overlap-vgap", remove_overlap_vgap);
    if (!remove_overlap_vgap) {
        std::cerr << "AlignAndDistribute::AlignAndDistribute: failed to load widget!" << std::endl;
    }

    // ------------  Node Align  -------------

    builder->get_widget("align-relative-node", align_relative_node);
    if (!align_relative_node) {
        std::cerr << "AlignAndDistribute::AlignAndDistribute: failed to load widget (combobox)!" << std::endl;
    } else {
        std::string align_nodes_to = prefs->getString("/dialogs/align/nodes-align-to", "first");
        align_relative_node->set_active_id(align_nodes_to);
        align_relative_node->signal_changed().connect(sigc::mem_fun(*this, &AlignAndDistribute::on_align_relative_node_changed));
    }

    std::vector<std::pair<std::string, std::string>> align_node_buttons = {
        {"align-node-horizontal", "horizontal"},
        {"align-node-vertical",   "vertical"  }
    };

    for (auto align_button: align_node_buttons) {
        Gtk::Button* button;
        builder->get_widget(align_button.first, button);
        if (!button) {
            std::cerr << "AlignAndDistribute::AlignAndDisribute: failed to get button: "
                      << align_button.first << " " << align_button.second << std::endl;
        } else {
            button->signal_clicked().connect(
                sigc::bind(sigc::mem_fun(*this, &AlignAndDistribute::on_align_node_clicked), align_button.second));
        }
    }


    // ------------ Set initial values ------------

    // Normal or node alignment?
    auto desktop = dlg->getDesktop();
    if (desktop) {
        desktop_changed(desktop);
    }

    auto set_icon_size_prefs = [=]() {
        int size = prefs->getIntLimited("/toolbox/tools/iconsize", -1, 16, 48);
        Inkscape::UI::set_icon_sizes(this, size);
    };

    // For now we are going to track the toolbox icon size, in the future we will have our own
    // dialog based icon sizes, perhaps done via css instead.
    _icon_sizes_changed = prefs->createObserver("/toolbox/tools/iconsize", set_icon_size_prefs);
    set_icon_size_prefs();
}

void
AlignAndDistribute::desktop_changed(SPDesktop* desktop)
{
    tool_connection.disconnect();
    if (desktop) {
        tool_connection =
            desktop->connectEventContextChanged(sigc::mem_fun(*this, &AlignAndDistribute::tool_changed_callback));
        tool_changed(desktop);
    }
}

void
AlignAndDistribute::tool_changed(SPDesktop* desktop)
{
    bool node = get_active_tool(desktop) == "Node";
    if (node) {
        align_and_distribute_object->set_visible(false);
        align_and_distribute_node->set_visible(true);
    } else {
        align_and_distribute_object->set_visible(true);
        align_and_distribute_node->set_visible(false);
    }
}

void
AlignAndDistribute::tool_changed_callback(SPDesktop* desktop, Inkscape::UI::Tools::ToolBase* ec)
{
    tool_changed(desktop);
}


void
AlignAndDistribute::on_align_as_group_clicked()
{
    bool state = align_move_as_group->get_active();
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    prefs->setBool("/dialogs/align/sel-as-groups", state);
}

void
AlignAndDistribute::on_align_relative_object_changed()
{
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    prefs->setString("/dialogs/align/objects-align-to", align_relative_object->get_active_id());
}

void
AlignAndDistribute::on_align_relative_node_changed()
{
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    prefs->setString("/dialogs/align/nodes-align-to", align_relative_node->get_active_id());
}

void
AlignAndDistribute::on_align_clicked(std::string const &align_to)
{
    Glib::ustring argument = align_to;

    argument += " " + align_relative_object->get_active_id();

    if (align_move_as_group->get_active()) {
        argument += " group";
    }

    auto variant = Glib::Variant<Glib::ustring>::create(argument);
    auto app = Gio::Application::get_default();

    if (align_to.find("vertical") != Glib::ustring::npos or align_to.find("horizontal") != Glib::ustring::npos) {
        app->activate_action("object-align-text", variant);
    } else {
        app->activate_action("object-align",      variant);
    }
}

void
AlignAndDistribute::on_remove_overlap_clicked()
{
    double hgap = remove_overlap_hgap->get_value();
    double vgap = remove_overlap_vgap->get_value();

    auto variant = Glib::Variant<std::tuple<double, double>>::create(std::tuple<double, double>(hgap, vgap));
    auto app = Gio::Application::get_default();
    app->activate_action("object-remove-overlaps", variant);
}

void
AlignAndDistribute::on_align_node_clicked(std::string const &direction)
{
    Glib::ustring argument = align_relative_node->get_active_id();

    auto variant = Glib::Variant<Glib::ustring>::create(argument);
    InkscapeWindow* win = InkscapeApplication::instance()->get_active_window();

    if (!win) {
        return;
    }

    if (direction == "horizontal") {
        win->activate_action("node-align-horizontal", variant);
    } else {
        win->activate_action("node-align-vertical", variant);
    }
}

} // namespace Dialog
} // namespace UI
} // namespace Inkscape

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
