// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <glibmm/main.h>
#include <glibmm/priorities.h>
#include <glibmm/ustring.h>
#include <gtkmm/label.h>
#include <iterator>
#include <pangomm/fontdescription.h>
#include <pangomm/fontfamily.h>
#include <set>
#include <glibmm/markup.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/menubutton.h>
#include <gtkmm/menuitem.h>
#include <gtkmm/radiobutton.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/searchentry.h>
#include <gtkmm/scale.h>
#include <glibmm/i18n.h>
#include <pangomm.h>
#include <libnrtype/font-factory.h>
#include <libnrtype/font-instance.h>

#include "font-list.h"
#include "iconrenderer.h"
#include "ui/builder-utils.h"
#include "ui/icon-loader.h"
#include "svg/css-ostringstream.h"

using Inkscape::UI::create_builder;

namespace Inkscape {
namespace UI {
namespace Widget {

struct FontListColumnModel : public Gtk::TreeModelColumnRecord {
    Gtk::TreeModelColumn<Inkscape::FontInfo> font;
    Gtk::TreeModelColumn<bool> missing;
    Gtk::TreeModelColumn<bool> injected;
    Gtk::TreeModelColumn<Glib::ustring> missing_name;
    Gtk::TreeModelColumn<int> icon;

    FontListColumnModel() {
        add(missing_name);
        add(missing);
        add(injected);
        add(icon);
        add(font);
    }
};

FontListColumnModel g_column_model; // model for font list

// list of font sizes for a slider; combo box has its own list
static std::array<int, 38> g_font_sizes = {
    4, 5, 6, 7, 8, 9, 10, 12, 14, 16, 18, 20, 24, 28, 32, 36,
    44, 56, 64, 72, 80, 96, 112, 128, 144, 160, 192, 224, 256,
    300, 350, 400, 450, 500, 550, 600, 700, 800
};

static int index_to_font_size(int index) {
    if (index < 0) {
        return g_font_sizes.front();
    }
    else if (index >= g_font_sizes.size()) {
        return g_font_sizes.back();
    }
    else {
        return g_font_sizes[index];
    }
}

static int font_size_to_index(double size) {
    auto it = std::lower_bound(begin(g_font_sizes), end(g_font_sizes), static_cast<int>(size));
    return std::distance(begin(g_font_sizes), it);
}

// construct font name from Pango face and family; if real font name is true, return
// font name as it is recorded in the font itself, as far as Pango allows it;
// otherwise return font name with CSS style string
Glib::ustring get_full_name(const Inkscape::FontInfo& font_info, bool real_font_name) {
    if (!font_info.ff) return "";

    if (real_font_name) {
        auto family = font_info.ff->get_name();
        auto face = font_info.face ? font_info.face->get_name() : Glib::ustring();
        auto name = face.empty() ? family : family + ' ' + face;
// g_message("fname: '%s'", name.c_str());
        return name;
    }

    if (!font_info.face || !font_info.ff) return "";
    // return name with CSS style string
    auto name = font_info.face->describe().to_string();

    // TODO: sanitize name, remove some stray characters; delegate this to common fn
    auto& raw = name.raw();
    if (raw.size() > 0 && raw[raw.size() - 1] == ',') {
        auto copy = name.raw();
        copy.pop_back();
        name = copy;
    }
    // name.replace()
    return name;
}

class CellFontRenderer : public Gtk::CellRendererText {
public:
    CellFontRenderer() {
       _face = Cairo::ToyFontFace::create("Noto", Cairo::FONT_SLANT_NORMAL, Cairo::FONT_WEIGHT_NORMAL);
    }

    ~CellFontRenderer() override = default;

    Gtk::Widget* _tree = nullptr;
    Inkscape::FontInfo _font;
    bool _show_font_name = true;
    int _font_size = 200; // size in %, where 100 is normal UI font size
    Glib::ustring _sample_text; // text to render (font preview)
    bool _icon_margin = false;
    // int _width = 0;
    // int _height = 0;
    Glib::ustring _name;
    Cairo::RefPtr<const Cairo::ToyFontFace> _face;

    void render_vfunc(const ::Cairo::RefPtr< ::Cairo::Context>& cr, Gtk::Widget& widget, const Gdk::Rectangle& background_area, const Gdk::Rectangle& cell_area, Gtk::CellRendererState flags) override;
/*
    void get_preferred_width_vfunc(Gtk::Widget& widget, int& min_w, int& nat_w) const override {
        if (_width) {
            min_w = nat_w = _width;
        }
        else {
            Gtk::CellRendererText::get_preferred_width_vfunc(widget, min_w, nat_w);
        }
    }

    void get_preferred_height_vfunc(Gtk::Widget& widget, int& min_h, int& nat_h) const override {
        if (_height) {
            min_h = nat_h = _height;
        }
        else {
            Gtk::CellRendererText::get_preferred_height_vfunc(widget, min_h, nat_h);
        }
    }
*/
};

CellFontRenderer& get_renderer(Gtk::CellRenderer& renderer) {
    return dynamic_cast<CellFontRenderer&>(renderer);
}

void get_cell_data_func(Gtk::CellRenderer* cell_renderer, Gtk::TreeModel::Row row) { // const Gtk::TreeIter& iter) {
    auto& renderer = get_renderer(*cell_renderer);

    // Gtk::TreeModel::Row row = *iter;
    const Inkscape::FontInfo& font = row[g_column_model.font];
    bool missing = row[g_column_model.missing];
    int icon = row[g_column_model.icon];
    Glib::ustring&& missing_name = row[g_column_model.missing_name];
// if (missing){
    // g_message("missing font: %s", missing_name.c_str());
// }
    auto name = missing ? missing_name : get_full_name(font, true);
    auto fname = Glib::Markup::escape_text(name);
    // if no sample text given, then render font name
    auto text = renderer._sample_text.empty() ? fname : renderer._sample_text;
// if (font.face) {
//     text += ':' + std::to_string(font.weight); // font.face->describe().get_weight());
// }
    auto font_desc = Glib::Markup::escape_text(
        missing ? "Sans" : Inkscape::get_font_description(font.ff, font.face).to_string());
    auto markup = Glib::ustring::format("<span allow_breaks='false' size='", renderer._font_size, "%' font='", font_desc, "'>", text, "</span>");
    if (renderer._show_font_name) {
        // if (!missing) {
        //     // get font name with CSS style
        //     fname = Glib::Markup::escape_text(get_full_name(font, false));
        // }
    renderer._name = fname; // Glib::ustring::format("\n<span allow_breaks='false' size='small' font='Noto Sans'>", fname, "</span>");
        // markup += Glib::ustring::format("\n<span allow_breaks='false' size='small' font='Noto Sans'>", fname, "</span>");
    }
    renderer.set_property("markup", markup);
    renderer._font = font;
    renderer._icon_margin = icon > 0;
}

void CellFontRenderer::render_vfunc(const ::Cairo::RefPtr<::Cairo::Context>& cr, Gtk::Widget& widget, const Gdk::Rectangle& background_area, const Gdk::Rectangle& cell_area, Gtk::CellRendererState flags) {
    Gdk::Rectangle bgnd(background_area);
    Gdk::Rectangle area(cell_area);
    auto margin = _icon_margin ? 5 : 0; // extra space for icon
    bgnd.set_x(bgnd.get_x() + margin);
    area.set_x(area.get_x() + margin);
    const auto name_font_size = 10; // attempt to select <small> text size
    const auto bottom = area.get_y() + area.get_height(); // bottom where the info font name will be placed
    Glib::RefPtr<Pango::Layout> layout;
    int text_height = 0;

    if (_show_font_name) {
        layout = _tree->create_pango_layout(_name);
        Pango::FontDescription font("Noto"); // wide range of character support
        font.set_weight(Pango::WEIGHT_NORMAL);
        font.set_size(name_font_size * PANGO_SCALE);
        layout->set_font_description(font);
        int text_width = 0;
        // get the text dimensions
        layout->get_pixel_size(text_width, text_height);
        // shrink area to prevent overlap
        area.set_height(area.get_height() - text_height);
    }

    CellRendererText::render_vfunc(cr, widget, bgnd, area, flags);

    if (_icon_margin) {

    }

    if (_show_font_name) {
        auto context = _tree->get_style_context();
        Gtk::StateFlags sflags = _tree->get_state_flags();
        if (flags & Gtk::CELL_RENDERER_SELECTED) {
            sflags |= Gtk::STATE_FLAG_SELECTED;
        }
        Gdk::RGBA fg = context->get_color(sflags);
        cr->save();
        cr->set_source_rgba(fg.get_red(), fg.get_green(), fg.get_blue(), 0.6);
        cr->move_to(area.get_x() + 2, bottom - text_height);
        layout->show_in_cairo_context(cr);

        /* 
        cr->set_source_rgba(fg.get_red(), fg.get_green(), fg.get_blue(), 0.12);

        auto y = background_area.get_y() + background_area.get_height() - 1;
        cr->move_to(background_area.get_x(), y);
        cr->line_to(background_area.get_x() + background_area.get_width(), y);
        cr->set_line_width(1);
        cr->stroke();
        */
        cr->restore();
    }

}

static void set_icon(Gtk::Button& btn, gchar const* pixmap) {
    if (Gtk::Image* img = sp_get_icon_image(pixmap, Gtk::ICON_SIZE_BUTTON)) {
        btn.set_image(*img);
    }
    else {
        g_warning("No icon found: %s", pixmap);
    }
}

const char* get_sort_icon(Inkscape::FontOrder order) {
    const char* icon = nullptr;

    switch (order) {
        case Inkscape::FontOrder::by_name:
            icon = "sort-alphabetically-symbolic";
            break;

        case Inkscape::FontOrder::by_weight:
            icon = "sort-by-weight-symbolic";
            break;

        case Inkscape::FontOrder::by_width:
            icon = "sort-by-width-symbolic";
            break;

        default:
            g_warning("Missing case in get_sort_icon");
            break;
    }

    return icon;
}

FontList::FontList() :
    _builder(create_builder("font-list.glade")),
    _main_grid(get_widget<Gtk::Grid>(_builder, "main-grid")),
    _tag_list(get_widget<Gtk::ListBox>(_builder, "categories")),
    _font_list(get_widget<Gtk::TreeView>(_builder, "font-list")),
    _font_grid(get_widget<Gtk::IconView>(_builder, "font-grid")),
    _font_size(get_widget<Gtk::ComboBoxText>(_builder, "font-size")),
    _font_size_scale(get_widget<Gtk::Scale>(_builder, "font-size-scale")),
    _tag_box(get_widget<Gtk::Box>(_builder, "tag-box")),
    _font_tags(Inkscape::FontTags::get())
{
    _cell_renderer = std::make_unique<CellFontRenderer>();
    auto font_renderer = static_cast<CellFontRenderer*>(_cell_renderer.get());
    font_renderer->_tree = &_font_list;

    _cell_icon_renderer = std::make_unique<IconRenderer>();
    auto ico = static_cast<IconRenderer*>(_cell_icon_renderer.get());
    ico->add_icon("empty-icon-symbolic");
    ico->add_icon("missing-element-symbolic");
    ico->add_icon("settings-symbolic");
    ico->set_fixed_size(16, 16);

    _grid_renderer = std::make_unique<CellFontRenderer>();
    auto renderer = static_cast<CellFontRenderer*>(_grid_renderer.get());
    renderer->_show_font_name = false;

    _font_list_store = Gtk::ListStore::create(g_column_model);

    get_widget<Gtk::Box>(_builder, "variants").pack_start(_font_variations);
    _font_variations.get_size_group(0)->add_widget(get_widget<Gtk::Label>(_builder, "font-size-label"));
    _font_variations.get_size_group(1)->add_widget(_font_size);
    _font_variations.connectChanged([=](){
        if (_update.pending()) return;
        _signal_changed.emit();
    });

    set_hexpand();
    set_vexpand();
    pack_start(_main_grid, true, true);
    set_margin_start(0);
    set_margin_end(0);
    set_margin_top(5);
    set_margin_bottom(0);
    show_all();

    auto options = &get_widget<Gtk::ToggleButton>(_builder, "btn-options");
    auto options_grid = &get_widget<Gtk::Grid>(_builder, "options-grid");
    options->signal_toggled().connect([=](){
        if (options->get_active()) options_grid->show(); else options_grid->hide();
    });

    std::pair<const char*, Inkscape::FontOrder> sorting[3] = {
        {"sort-by-name", Inkscape::FontOrder::by_name},
        {"sort-by-weight", Inkscape::FontOrder::by_weight},
        {"sort-by-width", Inkscape::FontOrder::by_width}
    };
    for (auto&& el : sorting) {
        auto& item = get_widget<Gtk::MenuItem>(_builder, el.first);
        item.signal_activate().connect([=](){ sort_fonts(el.second); });
        // pack icon and text into MenuItem, since MenuImageItem is deprecated
        auto text = item.get_label();
        auto hbox = Gtk::manage(new Gtk::Box);
        Gtk::Image* img = sp_get_icon_image(get_sort_icon(el.second), Gtk::ICON_SIZE_BUTTON);
        hbox->pack_start(*img, false, true, 4);
        auto label = Gtk::manage(new Gtk::Label);
        label->set_label(text);
        hbox->pack_start(*label, false, true, 4);
        hbox->show_all();
        item.remove();
        item.add(*hbox);
    }
/*
    static const char* filter_boxes[] = {"id-monospaced", "id-oblique", "id-other"};

    for (auto&& id : filter_boxes) {
        auto& checkbtn = get_widget<Gtk::CheckButton>(_builder, id);
        checkbtn.signal_toggled().connect([=](){ filter(); });
    }
*/
    get_widget<Gtk::Button>(_builder, "id-reset-filter").signal_clicked().connect([=](){
/*
        for (auto&& id : filter_boxes) {
            auto& checkbtn = get_widget<Gtk::CheckButton>(_builder, id);
            checkbtn.set_active();
        } */
        if (_font_tags.deselect_all()) {
            add_categories(_font_tags.get_tags());
            filter();
        }
    });

    auto search = &get_widget<Gtk::SearchEntry>(_builder, "font-search");
    search->signal_changed().connect([=](){ filter(); });

    auto set_row_height = [=](int font_size_percent) {
        font_renderer->_font_size = font_size_percent;
        // TODO: use pango layout to calc sizes
        int hh = (font_renderer->_show_font_name ? 10 : 0) + 18 * font_renderer->_font_size / 100;
        font_renderer->set_fixed_size(-1, hh);
        // resize rows
        _font_list.set_fixed_height_mode(false);
        _font_list.set_fixed_height_mode();
    };
    auto set_grid_size = [=](int font_size_percent) {
        renderer->_font_size = font_size_percent;
        // TODO: use pango layout to calc sizes
        int size = 20 * font_size_percent / 100;
        renderer->set_fixed_size(size * 4 / 3, size);
    };

    auto size = &get_widget<Gtk::Scale>(_builder, "preview-font-size");
    size->set_value(font_renderer->_font_size);
    size->signal_value_changed().connect([=](){
        auto font_size = size->get_value();
        set_row_height(font_size);
        set_grid_size(font_size);
        // _font_list.queue_draw();
        // resize
        filter();
    });

    auto show_names = &get_widget<Gtk::CheckButton>(_builder, "show-font-name");
    show_names->signal_toggled().connect([=](){
        bool show = show_names->get_active();
        //TODO: refactor to fn
        font_renderer->_show_font_name = show;
        set_row_height(font_renderer->_font_size);
        _font_list.set_grid_lines(show ? Gtk::TREE_VIEW_GRID_LINES_HORIZONTAL : Gtk::TREE_VIEW_GRID_LINES_NONE);
        // resize
        filter();
    });

    auto sample = &get_widget<Gtk::Entry>(_builder, "sample-text");
    sample->signal_changed().connect([=](){
        font_renderer->_sample_text = sample->get_text();
        renderer->_sample_text = sample->get_text();
        filter();
    });

    get_widget<Gtk::MenuItem>(_builder, "id-font-names").signal_activate().connect([=](){
        sample->set_text("");
        filter();
    });

    std::pair<const char*, const char*> samples[5] = {
        {"id-alphanum", "AbcdEfgh1234"},
        {"id-digits", "1234567890"},
        {"id-lowercase", "abcdefghijklmnopqrstuvwxyz"},
        {"id-uppercase", "ABCDEFGHIJKLMNOPQRSTUVWXYZ"},
        {"id-fox", "The quick brown fox jumps over the lazy dog."}
    };
    for (auto&& el : samples) {
        auto& item = get_widget<Gtk::MenuItem>(_builder, el.first);
        item.signal_activate().connect([=](){
            sample->set_text(el.second);
            filter();
        });
    }

    _text_column.set_sizing(Gtk::TREE_VIEW_COLUMN_FIXED);
    _text_column.pack_start(*_cell_icon_renderer, false);
    _cell_renderer->property_ellipsize() = Pango::ELLIPSIZE_END;
    _text_column.pack_start(*_cell_renderer, true);
    _text_column.set_fixed_width(100); // limit minimal width to keep entire dialog narrow; column can still grow
    _text_column.set_cell_data_func(*_cell_renderer, [=](Gtk::CellRenderer* r, const Gtk::TreeModel::iterator& it) {
        Gtk::TreeModel::Row row = *it;
        get_cell_data_func(r, row);
    });
    _text_column.set_expand();
    _text_column.add_attribute(ico->property_icon(), g_column_model.icon);
    _font_list.append_column(_text_column);

    _font_list.set_fixed_height_mode();
    set_row_height(font_renderer->_font_size);
    _font_list.set_search_column(-1); // disable, we have a separate search/filter
    _font_list.set_enable_search(false);
    _font_list.set_model(_font_list_store);

    _font_grid.pack_start(*_grid_renderer);
    renderer->set_fixed_height_from_font(1);
    set_grid_size(renderer->_font_size);
    renderer->_sample_text = "Aa";
    _font_grid.set_cell_data_func(*renderer, [=](const Gtk::TreeModel::const_iterator& it) {
        Gtk::TreeModel::Row row = *it;
        get_cell_data_func(renderer, row);
    });

    auto show_grid = &get_widget<Gtk::RadioButton>(_builder, "view-grid");
    auto show_list = &get_widget<Gtk::RadioButton>(_builder, "view-list");
    auto set_list_view_mode = [=](bool show_list) {
        auto& list = get_widget<Gtk::ScrolledWindow>(_builder, "list");
        auto& grid = get_widget<Gtk::ScrolledWindow>(_builder, "grid");
        list.set_no_show_all();
        grid.set_no_show_all();
        if (show_list) {
            grid.hide();
            _font_grid.unset_model();
            list.show();
        }
        else {
            list.hide();
            _font_grid.set_model(_font_list_store);
            grid.show();
        }
    };
    show_list->set_active();
    set_list_view_mode(true);
    show_list->signal_toggled().connect([=]() { set_list_view_mode(true); });
    show_grid->signal_toggled().connect([=]() { set_list_view_mode(false); });

// g_message("get font start");
    _fonts = Inkscape::get_all_fonts();
// g_message("get font done");

// fake some tags
for (auto&& f : _fonts) {
    if (f.ff->get_name().find("Helvetica") != Glib::ustring::npos) {
        _font_tags.tag_font(f.face, "favorites");
    }
// if (f.family_kind) g_message("%04x - %s", f.family_kind, get_full_name(f, true).c_str());
    auto kind = f.family_kind >> 8;
    if (kind == 10) {
        _font_tags.tag_font(f.face, "script");
    }
    else if (kind >= 1 && kind <= 5) {
        _font_tags.tag_font(f.face, "serif");
    }
    else if (kind == 8) {
        _font_tags.tag_font(f.face, "sans");
    }
    else if (kind == 12) {
        _font_tags.tag_font(f.face, "symbols");
    }

    if (f.monospaced) {
        _font_tags.tag_font(f.face, "monospace");
    }
    if (f.variable_font) {
        _font_tags.tag_font(f.face, "variable");
    }
}

    _font_size_scale.get_adjustment()->set_lower(0);
    _font_size_scale.get_adjustment()->set_upper(g_font_sizes.size() - 1);
    _font_size_scale.signal_value_changed().connect([=](){
        if (_update.pending()) return;

        auto scoped = _update.block();
        auto size = index_to_font_size(_font_size_scale.get_value());
        _font_size.get_entry()->set_text(std::to_string(size));
        _signal_changed.emit();
    });

    _font_size.signal_changed().connect([=](){
        if (_update.pending()) return;

        auto scoped = _update.block();
        auto text = _font_size.get_active_text();
        if (!text.empty()) {
            auto size = ::atof(text.c_str());
            if (size > 0) {
                _font_size_scale.set_value(font_size_to_index(size));
                _signal_changed.emit();
            }
        }
    });
    _font_size.set_active_text("10");

    sort_fonts(Inkscape::FontOrder::by_name);

    _font_list.get_selection()->signal_changed().connect([=](){
        if (!_update.pending()) {
            auto scoped = _update.block();
            Glib::ustring vars;
            if (auto iter = _font_list.get_selection()->get_selected()) {
                const Inkscape::FontInfo& font = (*iter)[g_column_model.font];
                vars = font.variations;
                if (vars.empty() && font.variable_font) {
                    vars = Inkscape::get_inkscape_fontspec(font.ff, font.face, font.variations);
                }
            }
            _font_variations.update(vars);
            _signal_changed.emit();
        }
    }, false);

    // double-click to apply:
    _font_list.signal_row_activated().connect([=](const Gtk::TreeModel::Path& path, Gtk::TreeViewColumn*){
        if (!_update.pending()) {
            auto scoped = _update.block();
            _signal_apply.emit();
        }
    });

    _font_tags.get_signal_tag_changed().connect([=](const FontTag* ftag, bool selected){
        sync_font_tag(ftag, selected);
    });

    auto& filter_popover = get_widget<Gtk::Popover>(_builder, "filter-popover");
    filter_popover.signal_show().connect([=](){
        // update tag checkboxes
        add_categories(_font_tags.get_tags());
    }, false);
}

void FontList::sort_fonts(Inkscape::FontOrder order) {
    Inkscape::sort_fonts(_fonts, order);

    if (const char* icon = get_sort_icon(order)) {
        auto& sort = get_widget<Gtk::Image>(_builder, "sort-icon");
        sort.set_from_icon_name(icon, Gtk::ICON_SIZE_BUTTON);
    }

    filter();
}

bool FontList::select_font(const Glib::ustring& fontspec) {
    bool found = false;

    _font_list_store->foreach([&](const Gtk::TreeModel::Path& path, const Gtk::TreeModel::iterator& iter){
        const auto& row = *iter;
        auto missing = row.get_value(g_column_model.missing);
        if (missing) {
            auto spec = row.get_value(g_column_model.missing_name);
            if (spec == fontspec) {
                _font_list.get_selection()->select(row);
                scroll_to_row(path);
                found = true;
                return true; // stop
            }
        }
        else {
            const auto& font = row.get_value(g_column_model.font);
            auto spec = Inkscape::get_inkscape_fontspec(font.ff, font.face, font.variations);
            if (spec == fontspec) {
                _font_list.get_selection()->select(row);
                scroll_to_row(path);
                found = true;
                return true; // stop
            }
        }
        return false; // continue
    });

    return found;
}

void FontList::filter() {
    auto scoped = _update.block();
    // todo: save selection
    Inkscape::FontInfo selected;
    auto it = _font_list.get_selection()->get_selected();
    if (it) {
        selected = it->get_value(g_column_model.font);
    }

    auto& search = get_widget<Gtk::SearchEntry>(_builder, "font-search");
    // auto& oblique = get_widget<Gtk::CheckButton>(_builder, "id-oblique");
    // auto& monospaced = get_widget<Gtk::CheckButton>(_builder, "id-monospaced");
    // auto& others = get_widget<Gtk::CheckButton>(_builder, "id-other");
    Show params;
    // params.monospaced = monospaced.get_active();
    // params.oblique = oblique.get_active();
    // params.others = others.get_active();
    filter(search.get_text(), params);

    if (!_current_fspec.empty()) {
        add_font(_current_fspec, false);
    }

    if (it) {
        // reselect
        //TODO
    }
}

void FontList::filter(Glib::ustring text, const Show& params) {
    auto filter = text.lowercase();

    auto active_categories = _font_tags.get_selected_tags();
    auto apply_categories = !active_categories.empty();

    _font_list_store->freeze_notify();
    _font_list_store->clear();
    _extra_fonts = 0;
    // _fspec_to_row.clear();

    for (auto&& f : _fonts) {
        bool filter_in = false;
        /*
        bool filter_in =
            params.oblique && f.oblique ||
            params.monospaced && f.monospaced ||
            params.others && (!f.oblique && !f.monospaced);

        if (!filter_in) continue;
        */

        if (!text.empty()) {
            auto name1 = get_full_name(f, true);
            // auto name2 = get_full_name(f, false);
            if (name1.lowercase().find(filter) == Glib::ustring::npos /* &&
                name2.lowercase().find(filter) == Glib::ustring::npos */) continue;
        }

        if (apply_categories) {
            filter_in = false;
            auto&& set = _font_tags.get_font_tags(f.face);
            for (auto&& ftag : active_categories) {
                if (set.count(ftag.tag) > 0) {
                    filter_in = true;
                    break;
                }
            }

            if (!filter_in) continue;
        }

        Gtk::TreeModel::iterator treeModelIter = _font_list_store->append();
        auto& row = *treeModelIter;
        row[g_column_model.font] = f;
        row[g_column_model.missing] = false;
        row[g_column_model.missing_name] = Glib::ustring();
        row[g_column_model.icon] = 0;
    }

    // for (auto&& row : _font_list_store->children()) {
    //     const auto& font = row.get_value(g_column_model.font);
    //     auto key = Inkscape::get_fontspec(font.ff, font.face).raw();
    //     _fspec_to_row[key] = &row;
    // }

    _font_list_store->thaw_notify();

    update_font_count();
}

void FontList::update_font_count() {
    auto& font_count = get_widget<Gtk::Label>(_builder, "font-count");
    auto count = _font_list_store->children().size();
    auto total = _fonts.size();
    // count could be larger than total if we insert "missing" font(s)
    auto label = count >= total ? _("All fonts") : Glib::ustring::format(count, ' ', _("of"), ' ', total, ' ', _("fonts"));
    // if (_extra_fonts > 0) {
    //     // indicate that extra rows were inserted to avoid surprises of incorrect count
    //     label += Glib::ustring::format(" +", _extra_fonts);
    // }
    font_count.set_text(label);
}

double FontList::get_fontsize() const {
    auto text = _font_size.get_entry()->get_text();
    if (!text.empty()) {
        auto size = ::atof(text.c_str());
        if (size > 0) return size;
    }
    return _current_fsize;
}

Glib::ustring FontList::get_fontspec() const {
    if (auto iter = _font_list.get_selection()->get_selected()) {
        auto missing = iter->get_value(g_column_model.missing);
        if (missing) {
            auto&& name = iter->get_value(g_column_model.missing_name);
            auto variations = _font_variations.get_pango_string(true);
            auto fspec = Inkscape::get_inkscape_fontspec_from_string(name, variations);
    // g_message("get missing fspec: %s", fspec.c_str());
            return fspec;
        }
        else {
            const auto& font = iter->get_value(g_column_model.font);
            auto variations = _font_variations.get_pango_string(true);
            auto fspec = Inkscape::get_inkscape_fontspec(font.ff, font.face, variations);
    // g_message("get fspec: %s - face: %s", fspec.c_str(), font.face ? font.face->describe().to_string().c_str() : "-");

            return fspec;
        }
    }

    return "Sans"; // no selection
}

void FontList::set_current_font(const Glib::ustring& family, const Glib::ustring& face) {
    if (_update.pending()) return;
// g_message("setcur: %s - %s", family.c_str(), face.c_str());
    auto scoped = _update.block();

    auto fontspec = Inkscape::get_fontspec(family, face);
    if (fontspec == _current_fspec) {
        auto fspec = get_fontspec_without_variants(fontspec);
        select_font(fspec);
        return;
    }
// {
//     auto desc = Pango::FontDescription(fontspec);
//     g_message("set cur font: fspec: '%s', desc: '%s'", fontspec.c_str(), desc.to_string().c_str());
// }
    _current_fspec = fontspec;

    if (!fontspec.empty()) {
        _font_variations.update(fontspec);
        add_font(fontspec, true);
    }
}

void FontList::set_current_size(double size) {
    _current_fsize = size;
    if (_update.pending()) return;
    //todo
// g_message("set fsize: %f", size);

    CSSOStringStream os;
    os.precision(3);os << size;
    _font_size.get_entry()->set_text(os.str());
}

void FontList::add_font(const Glib::ustring& fontspec, bool select) {
// g_message("addfnt: %s", fontspec.c_str());
    // auto it = std::find_if(begin(_fonts), end(_fonts), [&](const FontInfo& f){
    //     return Inkscape::get_fontspec(f.ff, f.face) == fontspec;
    // });
    // if (it != end(_fonts)) {
// g_message("addfnt2: %s", fontspec.c_str());
        // found matching font; check if it is currently in the tree list

    bool found = select_font(fontspec); // found in the tree view?
    if (found) return;

    auto it = std::find_if(begin(_fonts), end(_fonts), [&](const FontInfo& f){
        return Inkscape::get_inkscape_fontspec(f.ff, f.face, f.variations) == fontspec;
    });

    // fonts with variations will not be found, we need to remove " @ axis=value" part
    auto fspec = get_fontspec_without_variants(fontspec);
    // auto at = fontspec.rfind('@');
    if (it == end(_fonts) && fspec != fontspec) {
        // remove variations and try to match existing font
// g_message("try match: '%s'", fspec.c_str());
        it = std::find_if(begin(_fonts), end(_fonts), [&](const FontInfo& f){
            return Inkscape::get_inkscape_fontspec(f.ff, f.face, f.variations) == fspec;
        });
        if (it != end(_fonts)) {
            bool found = select_font(fspec); // found in the tree view?
            if (found) return;
        }
    }

    if (it != end(_fonts)) {
// g_message("found match for '%s'", fontspec.c_str());
        // font found in the "all fonts" vector, but
        // this font is filtered out; add it temporarily to the tree list
        Gtk::TreeModel::iterator iter = _font_list_store->prepend();
        auto& row = *iter;
        row[g_column_model.font] = *it;
        row[g_column_model.missing] = false;
        row[g_column_model.injected] = true;
        row[g_column_model.missing_name] = Glib::ustring();
        row[g_column_model.icon] = 0;
        // _fspec_to_row[fontspec.raw()] = &row;

        if (select) {
            _font_list.get_selection()->select(row);
            auto path = _font_list_store->get_path(iter);
            scroll_to_row(path);
        }

        ++_extra_fonts; // extra font entry inserted
    }
    else {
// g_message("no match for '%s'", fontspec.c_str());
        bool missing_font = true;
        Inkscape::FontInfo subst;

        auto desc = Pango::FontDescription(fontspec);
        auto vars = desc.get_variations();
// g_message("vars: '%s'", vars.c_str());
        if (!vars.empty()) {
            // font with variantions; check if we have matching family
            subst.variations = vars;

            auto family = desc.get_family();
// g_message("fam: '%s'", family.c_str());
            it = std::find_if(begin(_fonts), end(_fonts), [&](const FontInfo& f){
                return f.ff->get_name() == family;
            });
            if (it != end(_fonts)) {
                missing_font = false;
                subst.ff = it->ff;
            }
            // missing_font = it == end(_fonts);
        }

        auto&& children = _font_list_store->children();
        Gtk::TreeModel::iterator iter;
        if (!children.empty() && children[0][g_column_model.injected]) {
            // reuse "injected font" entry
            iter = children[0];
        }
        else {
            // font not found; insert a placeholder to show injected font: font is used in a document,
            // but either not available in the system (it's missing) or it is a variant of the variable font
            iter = _font_list_store->prepend();
        }
        auto& row = *iter;
        row[g_column_model.font] = subst;
        row[g_column_model.missing] = missing_font;
        row[g_column_model.injected] = true;
        row[g_column_model.missing_name] = fontspec;// TODO fname?
        row[g_column_model.icon] = missing_font ? 1 : (vars.empty() ? 0 : 2);
// g_message("addfnt5: %s", fontspec.c_str());
        // auto& top_row = *treeModelIter;
        // _fspec_to_row[fontspec.raw()] = &top_row;

        if (select) {
            _font_list.get_selection()->select(*iter);
            auto path = _font_list_store->get_path(iter);
            scroll_to_row(path);
        }

        ++_extra_fonts; // extra font entry for a missing font added
    }
// g_message("addfnt6: %s", fontspec.c_str());
    update_font_count();
}

Gtk::Box* FontList::create_pill_box(const FontTag& ftag) {
    auto box = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL);
    auto text = Gtk::make_managed<Gtk::Label>(ftag.display_name);
    auto close = Gtk::make_managed<Gtk::Button>();
    close->set_relief(Gtk::RELIEF_NONE);
    close->set_image_from_icon_name("close-button-symbolic");
    close->signal_clicked().connect([=](){
        // remove category from current filter
        update_categories(ftag.tag, false);
    });
    box->get_style_context()->add_class("tag-box");
    box->pack_start(*text, true, false);
    box->pack_end(*close, true, false);
    box->show_all();
    return box;
}

// show selected font categories in the filter bar
void FontList::update_filterbar() {
    // brute force approach at first
    for (auto&& btn : _tag_box.get_children()) {
        _tag_box.remove(*btn);
    }

    for (auto&& ftag : _font_tags.get_selected_tags()) {
        auto pill = create_pill_box(ftag);
        _tag_box.add(*pill);
    }
}

void FontList::update_categories(const std::string& tag, bool select) {
    if (_update.pending()) return;

    auto scoped = _update.block();

    if (!_font_tags.select_tag(tag, select)) return;

    // update UI
    update_filterbar();

    // apply filter
    filter();
}

void FontList::add_categories(const std::vector<FontTag>& tags) {
    for (auto row : _tag_list.get_children()) {
        if (row) _tag_list.remove(*row);
    }

    for (auto& tag : tags) {
        auto btn = Gtk::make_managed<Gtk::CheckButton>(tag.display_name);
        btn->set_active(_font_tags.is_tag_selected(tag.tag));
        btn->signal_toggled().connect([=](){
            // toggle font category
            update_categories(tag.tag, btn->get_active());
        });
// g_message("tag: %s", tag.display_name.c_str());
        auto row = Gtk::make_managed<Gtk::ListBoxRow>();
        row->set_can_focus(false);
        row->add(*btn);
        row->show_all();
        _tag_list.append(*row);
    }
}

void FontList::sync_font_tag(const FontTag* ftag, bool selected) {
    if (!ftag) {
        // many/all tags changed
        add_categories(_font_tags.get_tags());
        update_filterbar();
    }
    //todo as needed
}

void FontList::scroll_to_row(Gtk::TreePath path) {
// g_message("scrl to: %s", path.to_string().c_str());
    // delay scroll request to let widget layout complete (due to hiding or showing variable font widgets);
    // keep track of connection so we can disconnect in a destructor if it is still pending at that point
    _scroll = Glib::signal_timeout().connect([=](){
        _font_list.scroll_to_row(path);
        return false; // <- false means disconnect
    }, 50, Glib::PRIORITY_LOW);
    // fudge factor of 50ms; ideally wait for layout pass to complete before scrolling to the row
}

}}} // namespaces