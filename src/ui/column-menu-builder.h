// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef COLUMN_MENU_BUILDER_INCLUDED
#define COLUMN_MENU_BUILDER_INCLUDED

#include <cassert>
#include <optional>
#include <utility>
#include <sigc++/slot.h>
#include <glibmm/ustring.h>
#include <gtkmm/enums.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/separator.h>
#include "ui/widget/popover-menu.h"
#include "ui/widget/popover-menu-item.h"

namespace Inkscape {
namespace UI {

template <typename SectionData>
class ColumnMenuBuilder {
public:
    ColumnMenuBuilder(Widget::PopoverMenu& menu, int columns,
                      Gtk::IconSize icon_size = Gtk::ICON_SIZE_MENU,
                      int const first_row = 0)
        : _menu(menu)
        , _row(first_row)
        , _columns(columns)
        , _icon_size(static_cast<int>(icon_size))
    {
        assert(_row >= 0);
        assert(_columns >= 1);
    }

    Widget::PopoverMenuItem* add_item(
        Glib::ustring const &label, SectionData section, Glib::ustring const &tooltip,
        Glib::ustring const &icon_name, bool const sensitive, bool const customtooltip,
        sigc::slot<void ()> callback)
    {
        _new_section = false;
        _section = nullptr;
        if (!_last_section || *_last_section != section) {
            _new_section = true;
        }

        if (_new_section) {
            if (_col > 0) _row++;

            // add separator
            if (_row > 0) {
                auto const separator = Gtk::make_managed<Gtk::Separator>(Gtk::ORIENTATION_HORIZONTAL);
                separator->set_visible(true);
                _menu.attach(*separator, 0, _columns, _row, _row + 1);
                _row++;
            }

            _last_section = std::move(section);

            auto const sep = Gtk::make_managed<Widget::PopoverMenuItem>();
            sep->get_style_context()->add_class("menu-category");
            sep->set_sensitive(false);
            sep->set_halign(Gtk::ALIGN_START);
            sep->show_all();
            _menu.attach(*sep, 0, _columns, _row, _row + 1);
            _section = sep;
            _row++;
            _col = 0;
        }

        auto const item = Gtk::make_managed<Widget::PopoverMenuItem>(label, icon_name, _icon_size);
        if (!customtooltip) {
            item->set_tooltip_markup(tooltip);
        }
        item->set_sensitive(sensitive);
        item->signal_activate().connect(std::move(callback));
        item->show_all();
        _menu.attach(*item, _col, _col + 1, _row, _row + 1);
        _col++;
        if (_col >= _columns) {
            _col = 0;
            _row++;
        }

        return item;
    }

    bool new_section() {
        return _new_section;
    }

    void set_section(Glib::ustring name) {
        // name lastest section
        if (_section) {
            _section->set_label(name.uppercase());
        }
    }

private:
    int _row = 0;
    int _col = 0;
    int _columns;
    Widget::PopoverMenu &_menu;
    bool _new_section = false;
    std::optional<SectionData> _last_section;
    Widget::PopoverMenuItem *_section = nullptr;
    Gtk::IconSize _icon_size;
};

}} // namespace

#endif // COLUMN_MENU_BUILDER_INCLUDED
