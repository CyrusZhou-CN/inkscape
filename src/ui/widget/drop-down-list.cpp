// SPDX-License-Identifier: GPL-2.0-or-later

#include "drop-down-list.h"
#include <gtkmm/dropdown.h>
#include <gtkmm/label.h>
#include <gtkmm/stringobject.h>

namespace Inkscape::UI::Widget {

DropDownList::DropDownList() {
    _init();
}

DropDownList::DropDownList(BaseObjectType* cobject, const Glib::RefPtr<Gtk::Builder>& builder):
    Gtk::DropDown(cobject) {

    //TODO: verify if we want/need to set model too
    _init();
}

void DropDownList::_init() {
    set_name("DropDownList");

    _factory->signal_setup().connect([this](const Glib::RefPtr<Gtk::ListItem>& list_item) {
        auto label = Gtk::make_managed<Gtk::Label>();
        label->set_xalign(0);
        label->set_valign(Gtk::Align::CENTER);
        list_item->set_child(*label);
    });

    _factory->signal_bind().connect([this](const Glib::RefPtr<Gtk::ListItem>& list_item) {
        auto obj = list_item->get_item();
        auto& label = dynamic_cast<Gtk::Label&>(*list_item->get_child());
        if (_separator_callback) {
            auto pos = list_item->get_position();
            if (_separator_callback(pos)) {
                label.get_parent()->add_css_class("top-separator");
            }
        }
        auto item = std::dynamic_pointer_cast<Gtk::StringObject>(obj);
        label.set_label(item->get_string());
    });

    set_list_factory(_factory);
    set_model(_model);
}

unsigned int DropDownList::append(const Glib::ustring& item) {
    auto n = _model->get_n_items();
    _model->append(item);
    return n;
}

void DropDownList::enable_search(bool enable) {
    if (enable) {
        auto expression = Gtk::ClosureExpression<Glib::ustring>::create([this](auto& item){
            return std::dynamic_pointer_cast<Gtk::StringObject>(item)->get_string();
        });
        set_expression(expression);
    }
    set_enable_search(enable);
    // search or expression reset item factory; restore it
    set_list_factory(_factory);
}

void DropDownList::set_row_separator_func(std::function<bool (unsigned int)> callback) {
    _separator_callback = callback;
}

} // namespace
