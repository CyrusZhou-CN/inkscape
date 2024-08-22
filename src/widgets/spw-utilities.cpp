// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Inkscape Widget Utilities
 *
 * Authors:
 *   Bryce W. Harrington <brycehar@bryceharrington.org>
 *   bulia byak <buliabyak@users.sf.net>
 *
 * Copyright (C) 2003 Bryce W. Harrington
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <cstring>
#include <string>

#include <gtkmm/box.h>
#include <gtkmm/label.h>
#include <gtkmm/grid.h>

#include "spw-utilities.h"
#include "selection.h"
#include "ui/util.h"

/**
 * Creates a label widget with the given text, at the given col, row
 * position in the table.
 */
Gtk::Label * spw_label(Gtk::Grid *table, const gchar *label_text, int col, int row, Gtk::Widget* target)
{
  Gtk::Label *label_widget = new Gtk::Label();
  g_assert(label_widget != nullptr);
  if (target != nullptr) {
    label_widget->set_text_with_mnemonic(label_text);
    label_widget->set_mnemonic_widget(*target);
  } else {
    label_widget->set_text(label_text);
  }
  label_widget->set_visible(true);

  label_widget->set_halign(Gtk::ALIGN_START);
  label_widget->set_valign(Gtk::ALIGN_CENTER);
  label_widget->set_margin_start(4);
  label_widget->set_margin_end(4);

  table->attach(*label_widget, col, row, 1, 1);

  return label_widget;
}

/**
 * Creates a horizontal layout manager with 4-pixel spacing between children
 * and space for 'width' columns.
 */
Gtk::Box * spw_hbox(Gtk::Grid * table, int width, int col, int row)
{
  /* Create a new hbox with a 4-pixel spacing between children */
  auto const hb = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 4);
  g_assert(hb != nullptr);
  hb->set_visible(true);
  hb->set_hexpand();
  hb->set_halign(Gtk::ALIGN_FILL);
  hb->set_valign(Gtk::ALIGN_CENTER);
  table->attach(*hb, col, row, width, 1);

  return hb;
}

/**
 * Returns a named descendent of parent, which has the given name, or nullptr if there's none.
 *
 * \param[in] parent The widget to search
 * \param[in] name   The name of the desired child widget
 *
 * \return The specified child widget, or nullptr if it cannot be found
 */
Gtk::Widget *
sp_search_by_name_recursive(Gtk::Widget *parent, const Glib::ustring& name)
{
    return sp_traverse_widget_tree(parent, [&](Gtk::Widget* widget)
                                           { return widget->get_name() == name; });
}

/**
 * This function traverses a tree of widgets descending into bins and containers.
 * It stops and returns a pointer to the first child widget for which 'eval' evaluates to true.
 * If 'eval' never returns true then this function visits all widgets and returns nullptr.
 *
 * \param[in] widget The widget to start traversal from - top of the tree
 * \param[in] eval   The callback invoked for each visited widget
 *
 * \return The widget for which 'eval' returned true, or nullptr otherwise.
 * Note: it could be a starting widget too.
 *
 * See ui/util:for_each_child(), a generalisation of this and used as its basis.
 */
Gtk::Widget* sp_traverse_widget_tree(Gtk::Widget* widget, const std::function<bool (Gtk::Widget*)>& eval) {
    if (!widget) return nullptr;

    if (eval(widget)) return widget;

    if (auto bin = dynamic_cast<Gtk::Bin*>(widget)) {
        return sp_traverse_widget_tree(bin->get_child(), eval);
    }
    else if (auto container = dynamic_cast<Gtk::Container*>(widget)) {
        using namespace Inkscape::UI;
        Gtk::Widget *result = nullptr;
        for_each_child(*container, [&](Gtk::Widget &child){
            if (auto const found = sp_traverse_widget_tree(&child, eval)) {
                result = found;
                return ForEachResult::_break;
            }
            return ForEachResult::_continue;
        });
        return result;
    }

    return nullptr;
}

/**
 * This function traverses a tree of widgets searching for first focusable widget.
 *
 * \param[in] widget The widget to start traversal from - top of the tree
 *
 * \return The first focusable widget or nullptr if none are focusable.
 */
Gtk::Widget* sp_find_focusable_widget(Gtk::Widget* widget) {
    return sp_traverse_widget_tree(widget, [](Gtk::Widget* w) { return w->get_can_focus(); });
}


Glib::ustring sp_get_action_target(Gtk::Widget* widget) {
    Glib::ustring target;

    if (widget && GTK_IS_ACTIONABLE(widget->gobj())) {
        auto variant = gtk_actionable_get_action_target_value(GTK_ACTIONABLE(widget->gobj()));
        auto type = variant ? g_variant_get_type_string(variant) : nullptr;
        if (type && strcmp(type, "s") == 0) {
            target = g_variant_get_string(variant, nullptr);
        }
    }

    return target;
}
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
