// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * Simple DropDown widget presenting popup list of string items to choose from
 */
/*
 * Authors:
 *   Mike Kowalski
 *
 * Copyright (C) 2024 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <glibmm/ustring.h>
#include <gtkmm/builder.h>
#include <gtkmm/dropdown.h>
#include <gtkmm/signallistitemfactory.h>
#include <gtkmm/stringlist.h>

#ifndef INCLUDE_DROP_DOWN_LIST_H
#define INCLUDE_DROP_DOWN_LIST_H

namespace Inkscape::UI::Widget {

class DropDownList : public Gtk::DropDown {
public:
    DropDownList();

    // GtkBuilder constructor
    DropDownList(BaseObjectType* cobject, const Glib::RefPtr<Gtk::Builder>& builder);

    // append a new item to the dropdown list, return its position
    unsigned int append(const Glib::ustring& item);
    // get n-th item
    Glib::ustring get_string(unsigned int position) const { return _model->get_string(position); }
    // get number of items
    unsigned int get_item_count() const { return _model->get_n_items(); }
    // delete all items
    void remove_all() { _model->splice(0, _model->get_n_items(), {}); }

    // selected item changed signal
    Glib::SignalProxyProperty signal_changed() { return property_selected().signal_changed(); }

    // enable searching in a popup list
    void enable_search(bool enable = true);

    // if set, this callback will be invoked for each item position - returning true will
    // insert a separator on top of that item
    void set_row_separator_func(std::function<bool (unsigned int)> callback);

private:
    void _init();
    Glib::RefPtr<Gtk::StringList> _model = Gtk::StringList::create({});
    Glib::RefPtr<Gtk::SignalListItemFactory> _factory = Gtk::SignalListItemFactory::create();
    std::function<bool (unsigned int)> _separator_callback;
};

} // namespace

#endif // INCLUDE_DROP_DOWN_LIST_H
