// SPDX-License-Identifier: GPL-2.0-or-later

#include <cassert>
#include <gtkmm/menubutton.h>
#include <gtkmm/searchentry.h>

#include "completion-popup.h"
#include "ui/builder-utils.h"

namespace Inkscape::UI::Widget {

enum Columns {
    ColID = 0,
    ColName,
    ColIcon,
    ColSearch
};

CompletionPopup::CompletionPopup() :
    _builder(create_builder("completion-box.glade")),
    _search(get_widget<Gtk::SearchEntry>(_builder, "search")),
    _button(get_widget<Gtk::MenuButton>(_builder, "menu-btn")),
    _completion(get_object<Gtk::EntryCompletion>(_builder, "completion"))
{
    _popover_menu.show_all_children();
    _button.set_popover(_popover_menu);

    _list = Glib::RefPtr<Gtk::ListStore>::cast_dynamic(_builder->get_object("list"));
    assert(_list);

    add(get_widget<Gtk::Box>(_builder, "main-box"));

    _completion->set_match_func([=](const Glib::ustring& text, const Gtk::TreeModel::const_iterator& it){
        Glib::ustring str;
        it->get_value(ColSearch, str);
        if (str.empty()) {
            return false;
        }
        return str.lowercase().find(text.lowercase()) != Glib::ustring::npos;
    });

    _completion->signal_match_selected().connect([this](const Gtk::TreeModel::iterator& it){
        int id;
        it->get_value(ColID, id);
        _match_selected.emit(id);
        clear();
        return true;
    }, false);

    _search.property_is_focus().signal_changed().connect([&]{
        if (_search.is_focus()) {
            _on_focus.emit();
        }
        clear();
    });

    _button.signal_toggled().connect([&]{
        if (!_button.get_active()) {
            return;
        }
        _button_press.emit();
        clear();
    }, false);

    _search.signal_stop_search().connect([this](){
        clear();
    });

    set_visible(true);
}

void CompletionPopup::clear_completion_list() {
    _list->clear();
}

void CompletionPopup::add_to_completion_list(int id, Glib::ustring name, Glib::ustring icon_name, Glib::ustring search_text) {
    auto row = *_list->append();
    row.set_value(ColID, id);
    row.set_value(ColName, name);
    row.set_value(ColIcon, icon_name);
    row.set_value(ColSearch, search_text.empty() ? name : search_text);
}

PopoverMenu& CompletionPopup::get_menu() {
    return _popover_menu;
}

Gtk::SearchEntry& CompletionPopup::get_entry() {
    return _search;
}

sigc::signal<void (int)>& CompletionPopup::on_match_selected() {
    return _match_selected;
}

sigc::signal<void ()>& CompletionPopup::on_button_press() {
    return _button_press;
}

sigc::signal<bool ()>& CompletionPopup::on_focus() {
    return _on_focus;
}

/// Clear search box without triggering completion popup menu
void CompletionPopup::clear() {
    _search.get_buffer()->set_text(Glib::ustring());
}

} // namespace Inkscape::UI::Widget
