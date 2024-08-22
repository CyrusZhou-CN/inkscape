// SPDX-License-Identifier: GPL-2.0-or-later

#include <gtkmm/adjustment.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/cssprovider.h>
#include <gtkmm/menu.h>
#include <gtkmm/menubutton.h>
#include <gtkmm/popover.h>
#include <gtkmm/radiomenuitem.h>
#include <gtkmm/scale.h>
#include <gtkmm/scrollbar.h>

#include "color-palette.h"
#include "ui/builder-utils.h"
#include "ui/dialog/color-item.h"

namespace Inkscape {
namespace UI {
namespace Widget {

ColorPalette::ColorPalette():
    _builder(create_builder("color-palette.glade")),
    _normal_box(get_widget<Gtk::FlowBox>(_builder, "flow-box")),
    _pinned_box(get_widget<Gtk::FlowBox>(_builder, "pinned")),
    _menu(get_widget<Gtk::Menu>(_builder, "menu")),
    _scroll_btn(get_widget<Gtk::FlowBox>(_builder, "scroll-buttons")),
    _scroll_left(get_widget<Gtk::Button>(_builder, "btn-left")),
    _scroll_right(get_widget<Gtk::Button>(_builder, "btn-right")),
    _scroll_up(get_widget<Gtk::Button>(_builder, "btn-up")),
    _scroll_down(get_widget<Gtk::Button>(_builder, "btn-down")),
    _scroll(get_widget<Gtk::ScrolledWindow>(_builder, "scroll-wnd"))
    {

    auto& box = get_widget<Gtk::Box>(_builder, "palette-box");
    this->add(box);

    auto& config = get_widget<Gtk::MenuItem>(_builder, "config");
    auto& dlg = get_widget<Gtk::Popover>(_builder, "config-popup");
    config.signal_activate().connect([=,&dlg](){
        dlg.popup();
    });

    auto& size = get_widget<Gtk::Scale>(_builder, "size-slider");
    size.signal_change_value().connect([=,&size](Gtk::ScrollType, double val) {
        _set_tile_size(static_cast<int>(size.get_value()));
        _signal_settings_changed.emit();
        return true;
    });

    auto& aspect = get_widget<Gtk::Scale>(_builder, "aspect-slider");
    aspect.signal_change_value().connect([=,&aspect](Gtk::ScrollType, double val) {
        _set_aspect(aspect.get_value());
        _signal_settings_changed.emit();
        return true;
    });

    auto& border = get_widget<Gtk::Scale>(_builder, "border-slider");
    border.signal_change_value().connect([=,&border](Gtk::ScrollType, double val) {
        _set_tile_border(static_cast<int>(border.get_value()));
        _signal_settings_changed.emit();
        return true;
    });

    auto& rows = get_widget<Gtk::Scale>(_builder, "row-slider");
    rows.signal_change_value().connect([=,&rows](Gtk::ScrollType, double val) {
        _set_rows(static_cast<int>(rows.get_value()));
        _signal_settings_changed.emit();
        return true;
    });

    auto& sb = get_widget<Gtk::CheckButton>(_builder, "use-sb");
    sb.set_active(_force_scrollbar);
    sb.signal_toggled().connect([=,&sb](){
        _enable_scrollbar(sb.get_active());
        _signal_settings_changed.emit();
    });

    auto& stretch = get_widget<Gtk::CheckButton>(_builder, "stretch");
    stretch.set_active(_force_scrollbar);
    stretch.signal_toggled().connect([=,&stretch](){
        _enable_stretch(stretch.get_active());
        _signal_settings_changed.emit();
    });
    update_stretch();

    auto& large = get_widget<Gtk::CheckButton>(_builder, "enlarge");
    large.set_active(_large_pinned_panel);
    large.signal_toggled().connect([=,&large](){
        _set_large_pinned_panel(large.get_active());
        _signal_settings_changed.emit();
    });
    update_checkbox();

    auto& sl = get_widget<Gtk::CheckButton>(_builder, "show-labels");
    sl.set_visible(false);
    sl.set_active(_show_labels);
    sl.signal_toggled().connect([=,&sl](){
        _show_labels = sl.get_active();
        _signal_settings_changed.emit();
        rebuild_widgets();
    });

    _scroll.set_min_content_height(1);

    // set style for small buttons; we need them reasonably small, since they impact min height of color palette strip
    {
        auto css_provider = Gtk::CssProvider::create();
        css_provider->load_from_data(
        ".small {"
        " padding: 1px;"
        " margin: 0;"
        "}"
        );

        auto& btn_menu = get_widget<Gtk::MenuButton>(_builder, "btn-menu");
        Gtk::Widget* small_buttons[5] = {&_scroll_up, &_scroll_down, &_scroll_left, &_scroll_right, &btn_menu};
        for (auto button : small_buttons) {
            button->get_style_context()->add_provider(css_provider, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        }
    }

    _scroll_down.signal_clicked().connect([=](){ scroll(0, get_palette_height(), get_tile_height() + _border, true); });
    _scroll_up.signal_clicked().connect([=](){ scroll(0, -get_palette_height(), get_tile_height() + _border, true); });
    _scroll_left.signal_clicked().connect([=](){ scroll(-10 * (get_tile_width() + _border), 0, 0.0, false); });
    _scroll_right.signal_clicked().connect([=](){ scroll(10 * (get_tile_width() + _border), 0, 0.0, false); });

    {
        auto css_provider = Gtk::CssProvider::create();
        css_provider->load_from_data(
        "flowbox, scrolledwindow {"
        " padding: 0;"
        " border: 0;"
        " margin: 0;"
        " min-width: 1px;"
        " min-height: 1px;"
        "}");
        _scroll.get_style_context()->add_provider(css_provider, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        _normal_box.get_style_context()->add_provider(css_provider, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        _pinned_box.get_style_context()->add_provider(css_provider, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }

    // remove padding/margins from FlowBoxChild widgets, so previews can be adjacent to each other
    {
        auto css_provider = Gtk::CssProvider::create();
        css_provider->load_from_data(
        ".color-palette-main-box flowboxchild {"
        " padding: 0;"
        " border: 0;"
        " margin: 0;"
        " min-width: 1px;"
        " min-height: 1px;"
        "}");
        get_style_context()->add_provider_for_screen(this->get_screen(), css_provider, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }

    set_vexpand_set(true);
    set_up_scrolling();

    signal_size_allocate().connect([=](Gtk::Allocation& a){ set_up_scrolling(); });
}

ColorPalette::~ColorPalette() {
    if (_active_timeout) {
        g_source_remove(_active_timeout);
    }
}

void ColorPalette::do_scroll(int dx, int dy) {
    if (auto vert = _scroll.get_vscrollbar()) {
        vert->set_value(vert->get_value() + dy);
    }
    if (auto horz = _scroll.get_hscrollbar()) {
        horz->set_value(horz->get_value() + dx);
    }
}

std::pair<double, double> get_range(Gtk::Scrollbar& sb) {
    auto adj = sb.get_adjustment();
    return std::make_pair(adj->get_lower(), adj->get_upper() - adj->get_page_size());
}

gboolean ColorPalette::scroll_cb(gpointer self) {
    auto ptr = static_cast<ColorPalette*>(self);
    bool fire_again = false;

    if (auto vert = ptr->_scroll.get_vscrollbar()) {
        auto value = vert->get_value();
        // is this the final adjustment step?
        if (fabs(ptr->_scroll_final - value) < fabs(ptr->_scroll_step)) {
            vert->set_value(ptr->_scroll_final);
            fire_again = false; // cancel timer
        }
        else {
            auto pos = value + ptr->_scroll_step;
            vert->set_value(pos);
            auto range = get_range(*vert);
            if (pos > range.first && pos < range.second) {
                // not yet done
                fire_again = true; // fire this callback again
            }
        }
    }

    if (!fire_again) {
        ptr->_active_timeout = 0;
    }

    return fire_again;
}

void ColorPalette::scroll(int dx, int dy, double snap, bool smooth) {
    if (auto vert = _scroll.get_vscrollbar()) {
        if (smooth && dy != 0.0) {
            _scroll_final = vert->get_value() + dy;
            if (snap > 0) {
                // round it to whole 'dy' increments
                _scroll_final -= fmod(_scroll_final, snap);
            }
            auto range = get_range(*vert);
            if (_scroll_final < range.first) {
                _scroll_final = range.first;
            }
            else if (_scroll_final > range.second) {
                _scroll_final = range.second;
            }
            _scroll_step = dy / 4.0;
            if (!_active_timeout && vert->get_value() != _scroll_final) {
                // limit refresh to 60 fps, in practice it will be slower
                _active_timeout = g_timeout_add(1000 / 60, &ColorPalette::scroll_cb, this);
            }
        }
        else {
            vert->set_value(vert->get_value() + dy);
        }
    }
    if (auto horz = _scroll.get_hscrollbar()) {
        horz->set_value(horz->get_value() + dx);
    }
}

int ColorPalette::get_tile_size() const {
    return _size;
}

int ColorPalette::get_tile_border() const {
    return _border;
}

int ColorPalette::get_rows() const {
    return _rows;
}

double ColorPalette::get_aspect() const {
    return _aspect;
}

void ColorPalette::set_tile_border(int border) {
    _set_tile_border(border);
    auto& slider = get_widget<Gtk::Scale>(_builder, "border-slider");
    slider.set_value(border);
}

void ColorPalette::_set_tile_border(int border) {
    if (border == _border) return;

    if (border < 0 || border > 100) {
        g_warning("Unexpected tile border size of color palette: %d", border);
        return;
    }

    _border = border;
    set_up_scrolling();
}

void ColorPalette::set_tile_size(int size) {
    _set_tile_size(size);
    auto& slider = get_widget<Gtk::Scale>(_builder, "size-slider");
    slider.set_value(size);
}

void ColorPalette::_set_tile_size(int size) {
    if (size == _size) return;

    if (size < 1 || size > 1000) {
        g_warning("Unexpected tile size for color palette: %d", size);
        return;
    }

    _size = size;
    set_up_scrolling();
}

void ColorPalette::set_aspect(double aspect) {
    _set_aspect(aspect);
    auto& slider = get_widget<Gtk::Scale>(_builder, "aspect-slider");
    slider.set_value(aspect);
}

void ColorPalette::_set_aspect(double aspect) {
    if (aspect == _aspect) return;

    if (aspect < -2.0 || aspect > 2.0) {
        g_warning("Unexpected aspect ratio for color palette: %f", aspect);
        return;
    }

    _aspect = aspect;
    set_up_scrolling();
}

void ColorPalette::set_rows(int rows) {
    _set_rows(rows);
    auto& slider = get_widget<Gtk::Scale>(_builder, "row-slider");
    slider.set_value(rows);
}

void ColorPalette::_set_rows(int rows) {
    if (rows == _rows) return;

    if (rows < 1 || rows > 1000) {
        g_warning("Unexpected number of rows for color palette: %d", rows);
        return;
    }

    _rows = rows;
    update_checkbox();
    set_up_scrolling();
}

void ColorPalette::update_checkbox() {
    auto& sb = get_widget<Gtk::CheckButton>(_builder, "use-sb");
    // scrollbar can only be applied to single-row layouts
    sb.set_sensitive(_rows == 1);
}

void ColorPalette::set_compact(bool compact) {
    if (_compact != compact) {
        _compact = compact;
        set_up_scrolling();

        get_widget<Gtk::Scale>(_builder, "row-slider").set_visible(compact);
        get_widget<Gtk::Label>(_builder, "row-label").set_visible(compact);
        get_widget<Gtk::CheckButton>(_builder, "enlarge").set_visible(compact);
        get_widget<Gtk::CheckButton>(_builder, "show-labels").set_visible(!compact);
    }
}

bool ColorPalette::is_scrollbar_enabled() const {
    return _force_scrollbar;
}

bool ColorPalette::is_stretch_enabled() const {
    return _stretch_tiles;
}

void ColorPalette::enable_stretch(bool enable) {
    auto& stretch = get_widget<Gtk::CheckButton>(_builder, "stretch");
    stretch.set_active(enable);
    _enable_stretch(enable);
}

void ColorPalette::_enable_stretch(bool enable) {
    if (_stretch_tiles == enable) return;

    _stretch_tiles = enable;
    _normal_box.set_halign(enable ? Gtk::ALIGN_FILL : Gtk::ALIGN_START);
    update_stretch();
    set_up_scrolling();
}

void ColorPalette::enable_labels(bool labels) {
    auto& sl = get_widget<Gtk::CheckButton>(_builder, "show-labels");
    sl.set_active(labels);
    _show_labels = labels;
}

void ColorPalette::update_stretch() {
    auto& aspect = get_widget<Gtk::Scale>(_builder, "aspect-slider");
    aspect.set_sensitive(!_stretch_tiles);
    auto& label = get_widget<Gtk::Label>(_builder, "aspect-label");
    label.set_sensitive(!_stretch_tiles);
}

void ColorPalette::enable_scrollbar(bool show) {
    auto& sb = get_widget<Gtk::CheckButton>(_builder, "use-sb");
    sb.set_active(show);
    _enable_scrollbar(show);
}

void ColorPalette::_enable_scrollbar(bool show) {
    if (_force_scrollbar == show) return;

    _force_scrollbar = show;
    set_up_scrolling();
}

void ColorPalette::set_up_scrolling() {
    auto& box = get_widget<Gtk::Box>(_builder, "palette-box");
    auto& btn_menu = get_widget<Gtk::MenuButton>(_builder, "btn-menu");
    auto normal_count = std::max(1, static_cast<int>(_normal_box.get_children().size()));
    auto pinned_count = std::max(1, static_cast<int>(_pinned_box.get_children().size()));

    _normal_box.set_max_children_per_line(_show_labels ? 1 : normal_count);
    _normal_box.set_min_children_per_line(1);
    _pinned_box.set_max_children_per_line(_show_labels ? 1 : pinned_count);
    _pinned_box.set_min_children_per_line(1);

    if (_compact) {
        box.set_orientation(Gtk::ORIENTATION_HORIZONTAL);
        btn_menu.set_margin_bottom(0);
        btn_menu.set_margin_end(0);
        // in compact mode scrollbars are hidden; they take up too much space
        set_valign(Gtk::ALIGN_START);
        set_vexpand(false);

        _scroll.set_valign(Gtk::ALIGN_END);
        _normal_box.set_valign(Gtk::ALIGN_END);

        if (_rows == 1 && _force_scrollbar) {
            // horizontal scrolling with single row
            _normal_box.set_min_children_per_line(normal_count);

            _scroll_btn.set_visible(false);

            if (_force_scrollbar) {
                _scroll_left.set_visible(false);
                _scroll_right.set_visible(false);
            }
            else {
                _scroll_left.set_visible(true);
                _scroll_right.set_visible(true);
            }

            // ideally we should be able to use POLICY_AUTOMATIC, but on some themes this leads to a scrollbar
            // that obscures color tiles (it overlaps them); thus resorting to manual scrollbar selection
            _scroll.set_policy(_force_scrollbar ? Gtk::POLICY_ALWAYS : Gtk::POLICY_EXTERNAL, Gtk::POLICY_NEVER);
        }
        else {
            // vertical scrolling with multiple rows
            // 'external' allows scrollbar to shrink vertically
            _scroll.set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_EXTERNAL);
            _scroll_left.set_visible(false);
            _scroll_right.set_visible(false);
            _scroll_btn.set_visible(true);
        }

        int div = _large_pinned_panel ? (_rows > 2 ? 2 : 1) : _rows;
        _pinned_box.set_max_children_per_line(std::max((pinned_count + div - 1) / div, 1));
        _pinned_box.set_margin_end(_border);
    }
    else {
        box.set_orientation(Gtk::ORIENTATION_VERTICAL);
        btn_menu.set_margin_bottom(2);
        btn_menu.set_margin_end(2);
        // in normal mode use regular full-size scrollbars
        set_valign(Gtk::ALIGN_FILL);
        set_vexpand(true);

        _scroll_left.set_visible(false);
        _scroll_right.set_visible(false);
        _scroll_btn.set_visible(false);

        _normal_box.set_valign(Gtk::ALIGN_START);
        _scroll.set_valign(Gtk::ALIGN_FILL);
        // 'always' allocates space for scrollbar
        _scroll.set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_ALWAYS);
    }

    resize();
}

int ColorPalette::get_tile_size(bool horz) const {
    if (_stretch_tiles) return _size;

    double aspect = horz ? _aspect : -_aspect;

    if (aspect > 0) {
        return static_cast<int>(round((1.0 + aspect) * _size));
    }
    else if (aspect < 0) {
        return static_cast<int>(round((1.0 / (1.0 - aspect)) * _size));
    }
    else {
        return _size;
    }
}

int ColorPalette::get_tile_width() const {
    return get_tile_size(true);
}

int ColorPalette::get_tile_height() const {
    return get_tile_size(false);
}

int ColorPalette::get_palette_height() const {
    return (get_tile_height() + _border) * _rows;
}

void ColorPalette::set_large_pinned_panel(bool large) {
    auto& checkbox = get_widget<Gtk::CheckButton>(_builder, "enlarge");
    checkbox.set_active(large);
    _set_large_pinned_panel(large);
}

void ColorPalette::_set_large_pinned_panel(bool large) {
    if (_large_pinned_panel == large) return;

    _large_pinned_panel = large;
    set_up_scrolling();
}

bool ColorPalette::is_pinned_panel_large() const {
    return _large_pinned_panel;
}

bool ColorPalette::are_labels_enabled() const {
    return _show_labels;
}

void ColorPalette::resize() {
    if ((_rows == 1 && _force_scrollbar) || !_compact) {
        // auto size for single row to allocate space for scrollbar
        _scroll.set_size_request(-1, -1);
    }
    else {
        // exact size for multiple rows
        int height = get_palette_height() - _border;
        _scroll.set_size_request(1, height);
    }

    _normal_box.set_column_spacing(_border);
    _normal_box.set_row_spacing(_border);
    _pinned_box.set_column_spacing(_border);
    _pinned_box.set_row_spacing(_border);

    double scale = _show_labels ? 2.0 : 1.0;

    int width = get_tile_width() * scale;
    int height = get_tile_height() * scale;
    for (auto item : _normal_items) {
        item->set_size_request(width, height);
    }

    int pinned_width = width;
    int pinned_height = height;
    if (_large_pinned_panel) {
        double mult = _rows > 2 ? _rows / 2.0 : 2.0;
        pinned_width = pinned_height = static_cast<int>((height + _border) * mult - _border);
    }
    for (auto item : _pinned_items) {
        item->set_size_request(pinned_width, pinned_height);
    }
}

void free_colors(Gtk::FlowBox& flowbox) {
    for (auto widget : flowbox.get_children()) {
        if (widget) {
            flowbox.remove(*widget);
        }
    }
}

void ColorPalette::set_colors(std::vector<Dialog::ColorItem*> const &swatches)
{
    _normal_items.clear();
    _pinned_items.clear();
    
    for (auto item : swatches) {
        if (item->is_pinned()) {
            _pinned_items.emplace_back(item);
        } else {
            _normal_items.emplace_back(item);
        }
        item->signal_modified().connect([=] {
            item->get_parent()->foreach([=](Gtk::Widget& w) {
                if (auto label = dynamic_cast<Gtk::Label *>(&w)) {
                    label->set_text(item->get_description());
                }
            });
        });
    }
    rebuild_widgets();
}

Gtk::Widget *ColorPalette::_get_widget(Dialog::ColorItem *item) {
    if (auto parent = item->get_parent()) {
        parent->remove(*item);
    }
    if (_show_labels) {
        item->set_valign(Gtk::ALIGN_CENTER);
        auto const box = Gtk::make_managed<Gtk::Box>();
        auto const label = Gtk::make_managed<Gtk::Label>(item->get_description());
        box->add(*item);
        box->add(*label);
        return box;
    }
    return Gtk::manage(item);
}

void ColorPalette::rebuild_widgets()
{
    _normal_box.freeze_notify();
    _normal_box.freeze_child_notify();
    _pinned_box.freeze_notify();
    _pinned_box.freeze_child_notify();

    free_colors(_normal_box);
    free_colors(_pinned_box);

    for (auto item : _normal_items) {
        _normal_box.add(*_get_widget(item));
    }
    for (auto item : _pinned_items) {
        _pinned_box.add(*_get_widget(item));
    }

    _normal_box.show_all();
    _pinned_box.show_all();

    set_up_scrolling();

    _normal_box.thaw_child_notify();
    _normal_box.thaw_notify();
    _pinned_box.thaw_child_notify();
    _pinned_box.thaw_notify();
}

class CustomMenuItem : public Gtk::RadioMenuItem {
public:
    CustomMenuItem(Gtk::RadioMenuItem::Group& group, const Glib::ustring& label, std::vector<ColorPalette::rgb_t> colors):
        Gtk::RadioMenuItem(group, label), _colors(std::move(colors)) {

        set_margin_bottom(2);
    }
private:
    bool on_draw(const Cairo::RefPtr<Cairo::Context>& cr) override;
    std::vector<ColorPalette::rgb_t> _colors;
};

bool CustomMenuItem::on_draw(const Cairo::RefPtr<Cairo::Context>& cr) {
    RadioMenuItem::on_draw(cr);
    if (_colors.empty()) return false;

    auto allocation = get_allocation();
    auto x = 0;
    auto y = 0;
    auto width = allocation.get_width();
    auto height = allocation.get_height();
    auto left = x + height;
    auto right = x + width - height;
    auto dx = 1;
    auto dy = 2;
    auto px = left;
    auto py = y + height - dy;
    auto w = right - left;
    if (w <= 0) return false;

    for (int i = 0; i < w; ++i) {
        if (px >= right) break;

        int index = i * _colors.size() / w;
        auto& color = _colors.at(index);

        cr->set_source_rgb(color.r, color.g, color.b);
        cr->rectangle(px, py, dx, dy);
        cr->fill();

        px += dx;
    }

    return false;
}

void ColorPalette::set_palettes(const std::vector<ColorPalette::palette_t>& palettes) {
    auto items = _menu.get_children();
    auto count = items.size();

    int index = 0;
    while (count > 2) {
        if (auto item = items[index++]) {
            _menu.remove(*item);
            delete item;
        }
        count--;
    }

    Gtk::RadioMenuItem::Group group;
    for (auto it = palettes.rbegin(); it != palettes.rend(); ++it) {
        auto& name = it->name;
        auto const item = Gtk::make_managed<CustomMenuItem>(group, name, it->colors);
        item->signal_activate().connect([=](){
            if (!_in_update) {
                _in_update = true;
                _signal_palette_selected.emit(name);
                _in_update = false;
            }
        });
        item->set_visible(true);
        _menu.prepend(*item);
    }
}

sigc::signal<void (Glib::ustring)>& ColorPalette::get_palette_selected_signal() {
    return _signal_palette_selected;
}

sigc::signal<void ()>& ColorPalette::get_settings_changed_signal() {
    return _signal_settings_changed;
}

void ColorPalette::set_selected(const Glib::ustring& name) {
    auto items = _menu.get_children();
    _in_update = true;
    for (auto item : items) {
        if (auto radio = dynamic_cast<Gtk::RadioMenuItem*>(item)) {
            radio->set_active(radio->get_label() == name);
        }
    }
    _in_update = false;
}

}}} // namespace
