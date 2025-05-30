// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * Inkscape Preferences dialog - implementation.
 */
/* Authors:
 *   Carl Hetherington
 *   Marco Scholten
 *   Johan Engelen <j.b.c.engelen@ewi.utwente.nl>
 *   Bruno Dilly <bruno.dilly@gmail.com>
 *
 * Copyright (C) 2004-2013 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "inkscape-preferences.h"

#ifdef HAVE_CONFIG_H
# include "config.h"  // only include where actually required!
#endif

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <string>
#include <gio/gio.h>
#include <glibmm/error.h>
#include <glibmm/i18n.h>
#include <glibmm/markup.h>
#include <glibmm/miscutils.h>
#include <glibmm/convert.h>
#include <glibmm/regex.h>
#include <glibmm/ustring.h>
#include <giomm/themedicon.h>
#include <gtkmm/binlayout.h>
#include <gdkmm/display.h>
#include <gtkmm/accelerator.h>
#include <gtkmm/box.h>
#include <gtkmm/cssprovider.h>
#include <gtkmm/dialog.h>
#include <gtkmm/entry.h>
#include <gtkmm/enums.h>
#include <gtkmm/eventcontrollerkey.h>
#include <gtkmm/fontchooserdialog.h>
#include <gtkmm/icontheme.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/messagedialog.h>
#include <gtkmm/picture.h>
#include <gtkmm/recentmanager.h>
#include <gtkmm/revealer.h>
#include <gtkmm/scale.h>
#include <gtkmm/settings.h>
#include <gtkmm/styleprovider.h>
#include <gtkmm/togglebutton.h>
#include <sigc++/adaptors/bind.h>
#include <sigc++/functors/mem_fun.h>
#include <cairomm/matrix.h>
#include <cairomm/surface.h>
#include <glibmm/refptr.h>
#include <gtkmm/comboboxtext.h>
#include <gtkmm/object.h>
#include "display/control/ctrl-handle-manager.h"
#include "ui/widget/icon-combobox.h"
#include "ui/widget/handle-preview.h"

#if WITH_GSOURCEVIEW
#   include <gtksourceview/gtksource.h>
#endif

#include "auto-save.h"
#include "document.h"
#include "enums.h"
#include "inkscape-window.h"
#include "inkscape.h"
#include "message-stack.h"
#include "path-prefix.h"
#include "preferences.h"
#include "selcue.h"
#include "selection-chemistry.h"
#include "selection.h"
#include "style.h"

#include "colors/cms/system.h"
#include "colors/cms/profile.h"
#include "colors/manager.h"
#include "colors/spaces/base.h"
#include "display/nr-filter-gaussian.h"
#include <sigc++/scoped_connection.h>
#include "io/resource.h"
#include "ui/builder-utils.h"
#include "ui/dialog-run.h"
#include "ui/interface.h"
#include "ui/modifiers.h"
#include "ui/pack.h"
#include "ui/shortcuts.h"
#include "ui/themes.h"
#include "ui/tool/path-manipulator.h"
#include "ui/toolbar/tool-toolbar.h"
#include "ui/toolbar/toolbar-constants.h"
#include "ui/util.h"
#include "ui/widget/preferences-widget.h"
#include "ui/widget/style-swatch.h"
#include "util/recently-used-fonts.h"
#include "util/trim.h"
#include "util-string/ustring-format.h"
#include "widgets/spw-utilities.h"

namespace Inkscape::UI::Dialog {

using Inkscape::UI::Widget::DialogPage;
using Inkscape::UI::Widget::PrefCheckButton;
using Inkscape::UI::Widget::PrefRadioButton;
using Inkscape::UI::Widget::PrefItem;
using Inkscape::UI::Widget::PrefRadioButtons;
using Inkscape::UI::Widget::PrefSpinButton;
using Inkscape::UI::Widget::StyleSwatch;
using Inkscape::IO::Resource::get_filename;
using Inkscape::IO::Resource::UIS;

[[nodiscard]] static auto reset_icon()
{
    auto const image = Gtk::make_managed<Gtk::Image>();
    image->set_from_icon_name("reset");
    image->set_opacity(0.6);
    image->set_tooltip_text(_("Requires restart to take effect"));
    return image;
}

/**
 * Case-insensitive and unicode normalized search of `pattern` in `string`.
 *
 * @param pattern Text to find
 * @param string Text to search in
 * @param[out] score Match score between 0.0 (not found) and 1.0 (pattern == string)
 * @return True if `pattern` is a substring of `string`.
 */
static bool fuzzy_search(Glib::ustring const &pattern, Glib::ustring const &string, float &score)
{
    auto const norm_patt = pattern.lowercase().normalize();
    auto const norm_str  = string .lowercase().normalize();
    bool const found = norm_str.find(norm_patt) != norm_str.npos;
    score = found ? static_cast<float>(pattern.size()) / string.size() : 0;
    return score > 0.0 ? true : false;
}

/**
 * Case-insensitive and unicode normalized search of `pattern` in `string`.
 *
 * @param pattern Text to find
 * @param string Text to search in
 * @return True if `pattern` is a substring of `string`.
 */
static bool fuzzy_search(Glib::ustring const &pattern, Glib::ustring const &string)
{
    float score{};
    return fuzzy_search(pattern, string, score);
}

[[nodiscard]] static auto get_children_or_mnemonic_labels(Gtk::Widget &widget)
{
    if (dynamic_cast<Gtk::DropDown *>(&widget)) {
        std::vector<Gtk::Widget *> children;
        return children;
    }
    auto children = UI::get_children(widget);
    if (children.empty()) {
        children = widget.list_mnemonic_labels();
    }
    return children;
}

/**
 * Get number of child Labels that match a key in a widget
 *
 * @param key Text to find
 * @param widget Gtk::Widget to search in
 * @return Number of matches found
 */
static int get_num_matches(Glib::ustring const &key, Gtk::Widget *widget)
{
    g_assert(widget);

    int matches = 0;

    if (auto const label = dynamic_cast<Gtk::Label *>(widget)) {
        if (fuzzy_search(key, label->get_text().lowercase())) {
            ++matches;
        }
    }

    for (auto const child : get_children_or_mnemonic_labels(*widget)) {
        matches += get_num_matches(key, child);
    }

    return matches;
}

// Shortcuts model =============

class ModelColumns final : public Gtk::TreeModel::ColumnRecord {
public:
    ModelColumns() {
        add(name);
        add(id);
        add(shortcut);
        add(description);
        add(shortcutkey);
        add(user_set);
    }

    Gtk::TreeModelColumn<Glib::ustring> name;
    Gtk::TreeModelColumn<Glib::ustring> id;
    Gtk::TreeModelColumn<Glib::ustring> shortcut;
    Gtk::TreeModelColumn<Glib::ustring> description;
    Gtk::TreeModelColumn<Gtk::AccelKey> shortcutkey;
    Gtk::TreeModelColumn<unsigned int> user_set;
};
ModelColumns _kb_columns;
static ModelColumns& onKBGetCols() {
    return _kb_columns;
}

/**
 * Add CSS-based highlight-class and pango highlight to a Gtk::Label
 *
 * @param label Label to add highlight to
 * @param key Text to add pango highlight
 */
void InkscapePreferences::add_highlight(Gtk::Label *label, Glib::ustring const &key)
{
    Glib::ustring text = label->get_text();
    Glib::ustring const n_text = text.lowercase().normalize();
    Glib::ustring const n_key = key.lowercase().normalize();
    label->add_css_class("highlight");
    auto const pos = n_text.find(n_key);
    auto const len = n_key.size();
    text = Glib::Markup::escape_text(text.substr(0, pos)) + "<span weight=\"bold\" underline=\"single\">" +
           Glib::Markup::escape_text(text.substr(pos, len)) + "</span>" +
           Glib::Markup::escape_text(text.substr(pos + len));
    label->set_markup(text);
}

/**
 * Remove CSS-based highlight-class and pango highlight from a Gtk::Label
 *
 * @param label Label to remove highlight from
 * @param key Text to remove pango highlight from
 */
void InkscapePreferences::remove_highlight(Gtk::Label *label)
{
    if (label->get_use_markup()) {
        Glib::ustring text = label->get_text();
        label->set_text(text);
        label->remove_css_class("highlight");
    }
}

InkscapePreferences::InkscapePreferences()
    : DialogBase("/dialogs/preferences", "Preferences"),
      _minimum_width(0),
      _minimum_height(0),
      _natural_width(900),
      _natural_height(700),
      _current_page(nullptr),
      _init(true)
{
    //get the width of a spinbutton
    {
        UI::Widget::SpinButton sb;
        sb.set_width_chars(6);
        append(sb);
        int ignore{};
        sb.measure(Gtk::Orientation::HORIZONTAL, -1, ignore, _sb_width, ignore, ignore);
        remove(sb);
    }

    //Main HBox
    auto const hbox_list_page = Gtk::make_managed<Gtk::Box>();
    hbox_list_page->set_margin(12);
    hbox_list_page->set_spacing(12);
    append(*hbox_list_page);

    //Pagelist
    auto const list_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 3);
    auto const scrolled_window = Gtk::make_managed<Gtk::ScrolledWindow>();
    _search.set_valign(Gtk::Align::START);
    UI::pack_start(*list_box, _search, false, true);
    UI::pack_start(*list_box, *scrolled_window, false, true);
    UI::pack_start(*hbox_list_page, *list_box, false, true);
    _page_list.set_headers_visible(false);
    scrolled_window->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    scrolled_window->set_valign(Gtk::Align::FILL);
    scrolled_window->set_propagate_natural_width();
    scrolled_window->set_propagate_natural_height();
    scrolled_window->set_child(_page_list);
    scrolled_window->set_vexpand_set(true);
    scrolled_window->set_vexpand(true);
    scrolled_window->set_has_frame(true);
    _page_list_model = Gtk::TreeStore::create(_page_list_columns);
    _page_list_model_filter = Gtk::TreeModelFilter::create(_page_list_model);
    _page_list_model_sort = Gtk::TreeModelSort::create(_page_list_model_filter);
    _page_list_model_sort->set_sort_column(_page_list_columns._col_name, Gtk::SortType::ASCENDING);
    _page_list.set_enable_search(false);
    _page_list.set_model(_page_list_model_sort);
    _page_list.append_column("name",_page_list_columns._col_name);
    Glib::RefPtr<Gtk::TreeSelection> page_list_selection = _page_list.get_selection();
    page_list_selection->signal_changed().connect(sigc::mem_fun(*this, &InkscapePreferences::on_pagelist_selection_changed));
    page_list_selection->set_mode(Gtk::SelectionMode::BROWSE);

    // Search
    _page_list.set_search_column(-1); // this disables pop-up search!
    _search.signal_search_changed().connect(sigc::mem_fun(*this, &InkscapePreferences::on_search_changed));
    _search.set_tooltip_text("Search");
    _page_list_model_sort->set_sort_func(
        _page_list_columns._col_name, [this](Gtk::TreeModel::const_iterator const &a,
                                             Gtk::TreeModel::const_iterator const &b) -> int
        {
            float score_a, score_b;
            Glib::ustring key = _search.get_text().lowercase();
            if (key == "") {
                return -1;
            }
            Glib::ustring label_a = a->get_value(_page_list_columns._col_name).lowercase();
            Glib::ustring label_b = b->get_value(_page_list_columns._col_name).lowercase();
            auto *grid_a = a->get_value(_page_list_columns._col_page);
            auto *grid_b = b->get_value(_page_list_columns._col_page);
            int num_res_a = num_widgets_in_grid(key, grid_a);
            int num_res_b = num_widgets_in_grid(key, grid_b);
            fuzzy_search(key, label_a, score_a);
            fuzzy_search(key, label_b, score_b);
            if (score_a > score_b) {
                return -1;
            } else if (score_a < score_b) {
                return 1;
            } else if (num_res_a >= num_res_b) {
                return -1;
            } else if (num_res_a < num_res_b) {
                return 1;
            } else {
                return a->get_value(_page_list_columns._col_id) > b->get_value(_page_list_columns._col_id) ? -1 : 1;
            }
        });

    _search.signal_next_match().connect([this]{
        if (_search_results.size() > 0) {
            Gtk::TreeModel::iterator curr = _page_list.get_selection()->get_selected();
            auto _page_list_selection = _page_list.get_selection();
            auto next = get_next_result(curr);
            if (next) {
                _page_list.get_model()->get_iter(next);
                _page_list.scroll_to_cell(next, *_page_list.get_column(0));
                _page_list.set_cursor(next);
            }
        }
    });

    _search.signal_previous_match().connect([this]{
        if (_search_results.size() > 0) {
            Gtk::TreeModel::iterator curr = _page_list.get_selection()->get_selected();
            auto _page_list_selection = _page_list.get_selection();
            auto prev = get_prev_result(curr);
            if (prev) {
                _page_list.get_model()->get_iter(prev);
                _page_list.scroll_to_cell(prev, *_page_list.get_column(0));
                _page_list.set_cursor(prev);
            }
        }
    });

    auto const key = Gtk::EventControllerKey::create();
    key->signal_key_pressed().connect([this](auto &&...args) { return on_navigate_key_pressed(args...); }, true);
    _search.add_controller(key);

    _page_list_model_filter->set_visible_func([this](Gtk::TreeModel::const_iterator const &row){
        auto key_lower = _search.get_text().lowercase();
        return recursive_filter(key_lower, row);
    });

    //Pages
    auto const vbox_page = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL);
    auto const title_frame = Gtk::make_managed<Gtk::Frame>();

    auto const pageScroller = Gtk::make_managed<Gtk::ScrolledWindow>();
    pageScroller->set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
    pageScroller->set_propagate_natural_width();
    pageScroller->set_propagate_natural_height();
    pageScroller->set_child(*vbox_page);
    UI::pack_start(*hbox_list_page, *pageScroller, true, true);

    title_frame->set_child(_page_title);
    UI::pack_start(*vbox_page, *title_frame, false, false);
    UI::pack_start(*vbox_page, _page_frame, true, true);
    _page_frame.add_css_class("flat");
    title_frame->add_css_class("flat");

    initPageTools();
    initPageUI();
    initPageBehavior();
    initPageIO();

    initPageSystem();
    initPageBitmaps();
    initPageRendering();
    initPageSpellcheck();

    signal_map().connect(sigc::mem_fun(*this, &InkscapePreferences::showPage));

    //calculate the size request for this dialog
    _page_list.expand_all();
    _page_list_model->foreach_iter(sigc::mem_fun(*this, &InkscapePreferences::GetSizeRequest));
    _page_list.collapse_all();

    // Set Custom theme
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    _theme_oberver = prefs->createObserver("/theme/", [=]() {
        prefs->setString("/options/boot/theme", "custom");
    });
}

InkscapePreferences::~InkscapePreferences() = default;

/**
 * Get child Labels that match a key in a widget grid
 * and add the results to global _search_results vector
 *
 * @param key Text to find
 * @param widget Gtk::Widget to search in
 */
void InkscapePreferences::get_widgets_in_grid(Glib::ustring const &key, Gtk::Widget *widget)
{
    g_assert(widget);

    if (auto const label = dynamic_cast<Gtk::Label *>(widget)) {
        if (fuzzy_search(key, label->get_text())) {
            _search_results.push_back(label);
        }
    }

    for (auto const child : get_children_or_mnemonic_labels(*widget)) {
        get_widgets_in_grid(key, child);
    }
}

/**
 * Get number of child Labels that match a key in a widget grid
 * and add the results to global _search_results vector
 *
 * @param key Text to find
 * @param widget Gtk::Widget to search in
 * @return Number of results found
 */
int InkscapePreferences::num_widgets_in_grid(Glib::ustring const &key, Gtk::Widget *widget)
{
    g_assert(widget);

    int results = 0;

    if (auto const label = dynamic_cast<Gtk::Label *>(widget)) {
        if (fuzzy_search(key, label->get_text())) {
            ++results;
        }
    }

    for (auto const child : get_children_or_mnemonic_labels(*widget)) {
        results += num_widgets_in_grid(key, child);
    }

    return results;
}

/**
 * Implementation of the search functionality executes each time
 * search entry is changed
 */
void InkscapePreferences::on_search_changed()
{
    _num_results = 0;
    if (_search_results.size() > 0) {
        for (auto *result : _search_results) {
            remove_highlight(result);
        }
        _search_results.clear();
    }
    auto key = _search.get_text();
    _page_list_model_filter->refilter();
    // get first iter
    Gtk::TreeModel::Children children = _page_list.get_model()->children();
    Gtk::TreeModel::iterator iter = children.begin();

    highlight_results(key, iter);
    goto_first_result();
    if (key == "") {
        Gtk::TreeModel::Children children = _page_list.get_model()->children();
        Gtk::TreeModel::iterator iter = children.begin();
        _page_list.scroll_to_cell(Gtk::TreePath(iter), *_page_list.get_column(0));
        _page_list.set_cursor(Gtk::TreePath(iter));
    } else if (_num_results == 0 && key != "") {
        _page_list.set_has_tooltip(false);
        _show_all = true;
        _page_list_model_filter->refilter();
        _show_all = false;
        show_not_found();
    } else {
        _page_list.expand_all();
    }
}

/**
 * Select the first row in the tree that has a result
 */
void InkscapePreferences::goto_first_result()
{
    auto key = _search.get_text();
    if (_num_results > 0) {
        Gtk::TreeModel::iterator curr = _page_list.get_model()->children().begin();
        if (fuzzy_search(key, curr->get_value(_page_list_columns._col_name)) ||
            get_num_matches(key, curr->get_value(_page_list_columns._col_page)) > 0) {
            _page_list.scroll_to_cell(Gtk::TreePath(curr), *_page_list.get_column(0));
            _page_list.set_cursor(Gtk::TreePath(curr));
        } else {
            auto next = get_next_result(curr);
            if (next) {
                _page_list.scroll_to_cell(next, *_page_list.get_column(0));
                _page_list.set_cursor(next);
            }
        }
    }
}

/**
 * Look for the immediate next row in the tree that contains a search result
 *
 * @param iter Current iter the that is selected in the tree
 * @param check_children Bool whether to check if the children of the iter
 * contain search result
 * @return Immediate next row than contains a search result
 */
Gtk::TreePath InkscapePreferences::get_next_result(Gtk::TreeModel::iterator &iter, bool check_children)
{
    auto key = _search.get_text();
    Gtk::TreePath path = Gtk::TreePath(iter);
    if (iter->children().begin() && check_children) { // check for search results in children
        auto child = iter->children().begin();
        _page_list.expand_row(path, false);
        if (fuzzy_search(key, child->get_value(_page_list_columns._col_name)) ||
            get_num_matches(key, child->get_value(_page_list_columns._col_page)) > 0) {
            return Gtk::TreePath(child);
        } else {
            return get_next_result(child);
        }
    } else {
        ++iter;     // go to next row
        if (iter) { // if row exists
            if (fuzzy_search(key, iter->get_value(_page_list_columns._col_name)) ||
                get_num_matches(key, iter->get_value(_page_list_columns._col_page))) {
                path.next();
                return path;
            } else {
                return get_next_result(iter);
            }
        } else if (path.up() && path) {
            path.next();
            iter = _page_list.get_model()->get_iter(path);
            if (iter) {
                if (fuzzy_search(key, iter->get_value(_page_list_columns._col_name)) ||
                    get_num_matches(key, iter->get_value(_page_list_columns._col_page))) {
                    return Gtk::TreePath(iter);
                } else {
                    return get_next_result(iter);
                }
            } else {
                path.up();
                if (path) {
                    iter = _page_list.get_model()->get_iter(path);
                    return get_next_result(iter, false); // dont check for children
                } else {
                    return Gtk::TreePath(_page_list.get_model()->children().begin());
                }
            }
        }
    }
    assert(!iter);
    return Gtk::TreePath();
}

/**
 * Look for the immediate previous row in the tree that contains a search result
 *
 * @param iter Current iter that is selected in the tree
 * @param check_children Bool whether to check if the children of the iter
 * contain search result
 * @return Immediate previous row than contains a search result
 */
Gtk::TreePath InkscapePreferences::get_prev_result(Gtk::TreeModel::iterator &iter, bool iterate)
{
    auto key = _search.get_text();
    Gtk::TreePath path = Gtk::TreePath(iter);
    if (iterate) {
        --iter;
    }
    if (iter) {
        if (iter->children().begin()) {
            auto child = iter->children().end();
            --child;
            Gtk::TreePath path = Gtk::TreePath(iter);
            _page_list.expand_row(path, false);
            return get_prev_result(child, false);
        } else if (fuzzy_search(key, iter->get_value(_page_list_columns._col_name)) ||
                   get_num_matches(key, iter->get_value(_page_list_columns._col_page))) {
            return (Gtk::TreePath(iter));
        } else {
            return get_prev_result(iter);
        }
    } else if (path.up()) {
        if (path) {
            iter = _page_list.get_model()->get_iter(path);
            if (fuzzy_search(key, iter->get_value(_page_list_columns._col_name)) ||
                get_num_matches(key, iter->get_value(_page_list_columns._col_page))) {
                return path;
            } else {
                return get_prev_result(iter);
            }
        } else {
            auto lastIter = _page_list.get_model()->children().end();
            --lastIter;
            return get_prev_result(lastIter, false);
        }
    } else {
        return Gtk::TreePath(iter);
    }
}

/**
 * Handle key F3 and Shift+F3 key press events to navigate to the next search
 * result
 *
 * @param evt event object
 * @return Always returns False to label the key press event as handled, this
 * prevents the search bar from retaining focus for other keyboard event.
 */
bool InkscapePreferences::on_navigate_key_pressed(unsigned keyval, unsigned /*keycode*/,
                                                  Gdk::ModifierType state)
{
    if (keyval != GDK_KEY_F3 || _search_results.size() == 0) {
        return false;
    }

    auto const modmask = Gtk::Accelerator::get_default_mod_mask();
    if ((state & modmask) == Gdk::ModifierType::SHIFT_MASK) {
        Gtk::TreeModel::iterator curr = _page_list.get_selection()->get_selected();
        auto _page_list_selection = _page_list.get_selection();
        auto prev = get_prev_result(curr);
        if (prev) {
            _page_list.scroll_to_cell(prev, *_page_list.get_column(0));
            _page_list.set_cursor(prev);
        }
    } else {
        Gtk::TreeModel::iterator curr = _page_list.get_selection()->get_selected();
        auto _page_list_selection = _page_list.get_selection();
        auto next = get_next_result(curr);
        if (next) {
            _page_list.scroll_to_cell(next, *_page_list.get_column(0));
            _page_list.set_cursor(next);
        }
    }
    return false;
}

/**
 * Add highlight to all the search results
 *
 * @param key Text to search
 * @param iter pointing to the first row of the tree
 */
void InkscapePreferences::highlight_results(Glib::ustring const &key, Gtk::TreeModel::iterator &iter)
{
    Gtk::TreeModel::Path path;
    Glib::ustring Txt;
    while (iter) {
        auto const grid = iter->get_value(_page_list_columns._col_page);
        get_widgets_in_grid(key, grid);
        if (_search_results.size() > 0) {
            for (auto *result : _search_results) {
                // underline and highlight
                add_highlight(static_cast<Gtk::Label *>(result), key);
            }
        }
        if (iter->children()) {
            auto children = iter->children();
            auto child_iter = children.begin();
            highlight_results(key, child_iter);
        }
        iter++;
    }
}

/**
 * Filter function for the search functionality to show only matching
 * rows or rows with matching search resuls in the tree view
 *
 * @param key Text to search for
 * @param iter iter pointing to the first row of the tree view
 * @return True if the row is to be shown else return False
 */
bool InkscapePreferences::recursive_filter(Glib::ustring &key, Gtk::TreeModel::const_iterator const &iter)
{
    if(_show_all)
        return true;
    auto row_label = iter->get_value(_page_list_columns._col_name).lowercase();
    if (key == "") {
        return true;
    }
    if (fuzzy_search(key, row_label)) {
        ++_num_results;
        return true;
    }
    auto *grid = iter->get_value(_page_list_columns._col_page);
    int matches = get_num_matches(key, grid);
    _num_results += matches;
    if (matches) {
        return true;
    }
    auto child = iter->children().begin();
    if (child) {
        for (auto inner = child; inner; ++inner) {
            if (recursive_filter(key, inner)) {
                return true;
            }
        }
    }
    return false;
}

Gtk::TreeModel::iterator InkscapePreferences::AddPage(DialogPage& p, Glib::ustring title, int id)
{
    return AddPage(p, title, Gtk::TreeModel::iterator() , id);
}

Gtk::TreeModel::iterator InkscapePreferences::AddPage(DialogPage& p, Glib::ustring title, Gtk::TreeModel::iterator parent, int id)
{
    Gtk::TreeModel::iterator iter;
    if (parent)
       iter = _page_list_model->append((*parent).children());
    else
       iter = _page_list_model->append();
    Gtk::TreeModel::Row row = *iter;
    row[_page_list_columns._col_name] = title;
    row[_page_list_columns._col_id] = id;
    row[_page_list_columns._col_page] = &p;
    return iter;
}

void InkscapePreferences::AddSelcueCheckbox(DialogPage &p, Glib::ustring const &prefs_path, bool def_value)
{
    auto const cb = Gtk::make_managed<PrefCheckButton>();
    cb->init ( _("Show selection cue"), prefs_path + "/selcue", def_value);
    p.add_line( false, "", *cb, "", _("Whether selected objects display a selection cue (the same as in selector)"));
}

void InkscapePreferences::AddGradientCheckbox(DialogPage &p, Glib::ustring const &prefs_path, bool def_value)
{
    auto const cb = Gtk::make_managed<PrefCheckButton>();
    cb->init ( _("Enable gradient editing"), prefs_path + "/gradientdrag", def_value);
    p.add_line( false, "", *cb, "", _("Whether selected objects display gradient editing controls"));
}

void InkscapePreferences::AddLayerChangeCheckbox(DialogPage &p, Glib::ustring const &prefs_path, bool def_value)
{
    auto const cb = Gtk::make_managed<PrefCheckButton>();
    cb->init ( _("Change layer on selection"), prefs_path + "/changelayer", def_value);
    p.add_line( false, "", *cb, "", _("Whether selecting objects in another layer changes the active layer"));
}

void InkscapePreferences::AddPageChangeCheckbox(DialogPage &p, Glib::ustring const &prefs_path, bool def_value)
{
    auto const cb = Gtk::make_managed<PrefCheckButton>();
    cb->init ( _("Change page on selection"), prefs_path + "/changepage", def_value);
    p.add_line( false, "", *cb, "", _("Whether selecting objects on another page changes the current page"));
}

void InkscapePreferences::AddConvertGuidesCheckbox(DialogPage &p, Glib::ustring const &prefs_path, bool def_value) {
    auto const cb = Gtk::make_managed<PrefCheckButton>();
    cb->init ( _("Conversion to guides uses edges instead of bounding box"), prefs_path + "/convertguides", def_value);
    p.add_line( false, "", *cb, "", _("Converting an object to guides places these along the object's true edges (imitating the object's shape), not along the bounding box"));
}

void InkscapePreferences::AddDotSizeSpinbutton(DialogPage &p, Glib::ustring const &prefs_path, double def_value)
{
    auto const sb = Gtk::make_managed<PrefSpinButton>();
    sb->init ( prefs_path + "/dot-size", 0.0, 1000.0, 0.1, 10.0, def_value, false, false);
    p.add_line( false, _("Ctrl+click _dot size:"), *sb, _("times current stroke width"),
                       _("Size of dots created with Ctrl+click (relative to current stroke width)"),
                       false );
}

void InkscapePreferences::AddBaseSimplifySpinbutton(DialogPage &p, Glib::ustring const &prefs_path, double def_value)
{
    auto const sb = Gtk::make_managed<PrefSpinButton>();
    sb->init ( prefs_path + "/base-simplify", 0.0, 100.0, 1.0, 10.0, def_value, false, false);
    p.add_line( false, _("Base simplify:"), *sb, _("on dynamic LPE simplify"),
                       _("Base simplify of dynamic LPE based simplify"),
                       false );
}


static void StyleFromSelectionToTool(Glib::ustring const &prefs_path, StyleSwatch *swatch)
{
    SPDesktop *desktop = SP_ACTIVE_DESKTOP;
    if (desktop == nullptr)
        return;

    Inkscape::Selection *selection = desktop->getSelection();

    if (selection->isEmpty()) {
        desktop->messageStack()->flash(Inkscape::ERROR_MESSAGE,
                                       _("<b>No objects selected</b> to take the style from."));
        return;
    }
    SPItem *item = selection->singleItem();
    if (!item) {
        /* TODO: If each item in the selection has the same style then don't consider it an error.
         * Maybe we should try to handle multiple selections anyway, e.g. the intersection of the
         * style attributes for the selected items. */
        desktop->messageStack()->flash(Inkscape::ERROR_MESSAGE,
                                       _("<b>More than one object selected.</b>  Cannot take style from multiple objects."));
        return;
    }

    SPCSSAttr *css = take_style_from_item (item);

    if (!css) return;

    // remove black-listed properties
    css = sp_css_attr_unset_blacklist (css);

    // only store text style for the text tool
    if (prefs_path != "/tools/text") {
        css = sp_css_attr_unset_text (css);
    }

    // we cannot store properties with uris - they will be invalid in other documents
    css = sp_css_attr_unset_uris (css);

    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    prefs->setStyle(prefs_path + "/style", css);
    sp_repr_css_attr_unref (css);

    // update the swatch
    if (swatch) {
        SPCSSAttr *css = prefs->getInheritedStyle(prefs_path + "/style");
        swatch->setStyle (css);
        sp_repr_css_attr_unref(css);
    }
}

void InkscapePreferences::AddNewObjectsStyle(DialogPage &p, Glib::ustring const &prefs_path, const gchar *banner)
{
    if (banner)
        p.add_group_header(banner);
    else
        p.add_group_header( _("Style of new objects"));
    auto const current = Gtk::make_managed<PrefRadioButton>();
    current->init ( _("Last used style"), prefs_path + "/usecurrent", 1, true, nullptr);
    p.add_line( true, "", *current, "",
                _("Apply the style you last set on an object"));

    auto const own = Gtk::make_managed<PrefRadioButton>();
    auto const hb = Gtk::make_managed<Gtk::Box>();
    own->init ( _("This tool's own style:"), prefs_path + "/usecurrent", 0, false, current);
    own->set_halign(Gtk::Align::START);
    own->set_valign(Gtk::Align::START);
    hb->append(*own);

    p.set_tip( *own, _("Each tool may store its own style to apply to the newly created objects. Use the button below to set it."));
    p.add_line( true, "", *hb, "", "");

    // style swatch
    auto const button = Gtk::make_managed<Gtk::Button>(_("Take from selection"), true);
    StyleSwatch *swatch = nullptr;
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();

    if (prefs->getInt(prefs_path + "/usecurrent")) {
        button->set_sensitive(false);
    }

    SPCSSAttr *css = prefs->getStyle(prefs_path + "/style");
    swatch = Gtk::make_managed<StyleSwatch>(css, _("This tool's style of new objects"));
    hb->append(*swatch);
    sp_repr_css_attr_unref(css);

    button->signal_clicked().connect(sigc::bind(&StyleFromSelectionToTool, prefs_path, swatch));
    own->changed_signal.connect( sigc::mem_fun(*button, &Gtk::Button::set_sensitive) );
    p.add_line( true, "", *button, "",
                _("Remember the style of the (first) selected object as this tool's style"));
}
#define get_tool_action(toolname) ("win.tool-switch('" + toolname + "')")
Glib::ustring get_tool_action_name(Glib::ustring toolname)
{
    auto *iapp = InkscapeApplication::instance();
    if (iapp)
        return iapp->get_action_extra_data().get_label_for_action(get_tool_action(toolname));
    return "";
}
void InkscapePreferences::initPageTools()
{
    Gtk::TreeModel::iterator iter_tools = this->AddPage(_page_tools, _("Tools"), PREFS_PAGE_TOOLS);
    this->AddPage(_page_selector, get_tool_action_name("Select"), iter_tools, PREFS_PAGE_TOOLS_SELECTOR);
    this->AddPage(_page_node, get_tool_action_name("Node"), iter_tools, PREFS_PAGE_TOOLS_NODE);

    // shapes
    Gtk::TreeModel::iterator iter_shapes = this->AddPage(_page_shapes, _("Shapes"), iter_tools, PREFS_PAGE_TOOLS_SHAPES);
    this->AddPage(_page_rectangle, get_tool_action_name("Rect"), iter_shapes, PREFS_PAGE_TOOLS_SHAPES_RECT);
    this->AddPage(_page_ellipse, get_tool_action_name("Arc"), iter_shapes, PREFS_PAGE_TOOLS_SHAPES_ELLIPSE);
    this->AddPage(_page_star, get_tool_action_name("Star"), iter_shapes, PREFS_PAGE_TOOLS_SHAPES_STAR);
    this->AddPage(_page_3dbox, get_tool_action_name("3DBox"), iter_shapes, PREFS_PAGE_TOOLS_SHAPES_3DBOX);
    this->AddPage(_page_spiral, get_tool_action_name("Spiral"), iter_shapes, PREFS_PAGE_TOOLS_SHAPES_SPIRAL);

    this->AddPage(_page_pen, get_tool_action_name("Pen"), iter_tools, PREFS_PAGE_TOOLS_PEN);
    this->AddPage(_page_pencil, get_tool_action_name("Pencil"), iter_tools, PREFS_PAGE_TOOLS_PENCIL);
    this->AddPage(_page_calligraphy, get_tool_action_name("Calligraphic"), iter_tools, PREFS_PAGE_TOOLS_CALLIGRAPHY);
    this->AddPage(_page_text, get_tool_action_name("Text"), iter_tools, PREFS_PAGE_TOOLS_TEXT);

    this->AddPage(_page_gradient, get_tool_action_name("Gradient"), iter_tools, PREFS_PAGE_TOOLS_GRADIENT);
    this->AddPage(_page_dropper, get_tool_action_name("Dropper"), iter_tools, PREFS_PAGE_TOOLS_DROPPER);
    this->AddPage(_page_paintbucket, get_tool_action_name("PaintBucket"), iter_tools, PREFS_PAGE_TOOLS_PAINTBUCKET);

    this->AddPage(_page_tweak, get_tool_action_name("Tweak"), iter_tools, PREFS_PAGE_TOOLS_TWEAK);
    this->AddPage(_page_spray, get_tool_action_name("Spray"), iter_tools, PREFS_PAGE_TOOLS_SPRAY);
    this->AddPage(_page_eraser, get_tool_action_name("Eraser"), iter_tools, PREFS_PAGE_TOOLS_ERASER);
    this->AddPage(_page_connector, get_tool_action_name("Connector"), iter_tools, PREFS_PAGE_TOOLS_CONNECTOR);
#ifdef WITH_LPETOOL
    this->AddPage(_page_lpetool, get_tool_action_name("LPETool"), iter_tools, PREFS_PAGE_TOOLS_LPETOOL);
#endif // WITH_LPETOOL
    this->AddPage(_page_measure, get_tool_action_name("Measure"), iter_tools, PREFS_PAGE_TOOLS_MEASURE);
    this->AddPage(_page_zoom, get_tool_action_name("Zoom"), iter_tools, PREFS_PAGE_TOOLS_ZOOM);
    _page_tools.add_group_header( _("Bounding box to use"));
    _t_bbox_visual.init ( _("Visual bounding box"), "/tools/bounding_box", 0, false, nullptr); // 0 means visual
    _page_tools.add_line( true, "", _t_bbox_visual, "",
                            _("This bounding box includes stroke width, markers, filter margins, etc."));
    _t_bbox_geometric.init ( _("Geometric bounding box"), "/tools/bounding_box", 1, true, &_t_bbox_visual); // 1 means geometric
    _page_tools.add_line( true, "", _t_bbox_geometric, "",
                            _("This bounding box includes only the bare path"));

    _page_tools.add_group_header( _("Conversion to guides"));
    _t_cvg_keep_objects.init ( _("Keep objects after conversion to guides"), "/tools/cvg_keep_objects", false);
    _page_tools.add_line( true, "", _t_cvg_keep_objects, "",
                            _("When converting an object to guides, don't delete the object after the conversion"));
    _t_cvg_convert_whole_groups.init ( _("Treat groups as a single object"), "/tools/cvg_convert_whole_groups", false);
    _page_tools.add_line( true, "", _t_cvg_convert_whole_groups, "",
                            _("Treat groups as a single object during conversion to guides rather than converting each child separately"));

    _pencil_average_all_sketches.init ( _("Average all sketches"), "/tools/freehand/pencil/average_all_sketches", false);
    _calligrapy_keep_selected.init ( _("Select new path"), "/tools/calligraphic/keep_selected", true);
    _connector_ignore_text.init( _("Don't attach connectors to text objects"), "/tools/connector/ignoretext", true);

    //Selector


    AddSelcueCheckbox(_page_selector, "/tools/select", false);
    AddGradientCheckbox(_page_selector, "/tools/select", false);
    AddLayerChangeCheckbox(_page_selector, "/tools/select", true);
    AddPageChangeCheckbox(_page_selector, "/tools/select", true);

    _page_selector.add_group_header( _("When transforming, show"));
    _t_sel_trans_obj.init ( _("Objects"), "/tools/select/show", "content", true, nullptr);
    _page_selector.add_line( true, "", _t_sel_trans_obj, "",
                            _("Show the actual objects when moving or transforming"));
    _t_sel_trans_outl.init ( _("Box outline"), "/tools/select/show", "outline", false, &_t_sel_trans_obj);
    _page_selector.add_line( true, "", _t_sel_trans_outl, "",
                            _("Show only a box outline of the objects when moving or transforming"));
    _page_selector.add_group_header( _("Per-object selection cue"));
    _t_sel_cue_none.init ( C_("Selection cue", "None"), "/options/selcue/value", Inkscape::SelCue::NONE, false, nullptr);
    _page_selector.add_line( true, "", _t_sel_cue_none, "",
                            _("No per-object selection indication"));
    _t_sel_cue_mark.init ( _("Mark"), "/options/selcue/value", Inkscape::SelCue::MARK, true, &_t_sel_cue_none);
    _page_selector.add_line( true, "", _t_sel_cue_mark, "",
                            _("Each selected object has a diamond mark in the top left corner"));
    _t_sel_cue_box.init ( _("Box"), "/options/selcue/value", Inkscape::SelCue::BBOX, false, &_t_sel_cue_none);
    _page_selector.add_line( true, "", _t_sel_cue_box, "",
                            _("Each selected object displays its bounding box"));

    //Node
    AddSelcueCheckbox(_page_node, "/tools/nodes", true);
    AddGradientCheckbox(_page_node, "/tools/nodes", true);
    AddLayerChangeCheckbox(_page_node, "/tools/nodes", false);

    _page_node.add_group_header( _("Path outline"));
    _t_node_pathoutline_color.init(_("Path outline color"), "/tools/nodes/highlight_color", "#ff0000ff");
    _page_node.add_line( false, "", _t_node_pathoutline_color, "", _("Selects the color used for showing the path outline"), false);
    _t_node_show_outline.init(_("Always show outline"), "/tools/nodes/show_outline", false);
    _page_node.add_line( true, "", _t_node_show_outline, "", _("Show outlines for all paths, not only invisible paths"));
    _t_node_live_outline.init(_("Update outline when dragging nodes"), "/tools/nodes/live_outline", false);
    _page_node.add_line( true, "", _t_node_live_outline, "", _("Update the outline when dragging or transforming nodes; if this is off, the outline will only update when completing a drag"));
    _t_node_live_objects.init(_("Update paths when dragging nodes"), "/tools/nodes/live_objects", false);
    _page_node.add_line( true, "", _t_node_live_objects, "", _("Update paths when dragging or transforming nodes; if this is off, paths will only be updated when completing a drag"));
    _t_node_show_path_direction.init(_("Show path direction on outlines"), "/tools/nodes/show_path_direction", false);
    _page_node.add_line( true, "", _t_node_show_path_direction, "", _("Visualize the direction of selected paths by drawing small arrows in the middle of each outline segment"));
    _t_node_pathflash_enabled.init ( _("Show temporary path outline"), "/tools/nodes/pathflash_enabled", false);
    _page_node.add_line( true, "", _t_node_pathflash_enabled, "", _("When hovering over a path, briefly flash its outline"));
    _t_node_pathflash_selected.init ( _("Show temporary outline for selected paths"), "/tools/nodes/pathflash_selected", false);
    _page_node.add_line( true, "", _t_node_pathflash_selected, "", _("Show temporary outline even when a path is selected for editing"));
    _t_node_pathflash_timeout.init("/tools/nodes/pathflash_timeout", 0, 10000.0, 100.0, 100.0, 1000.0, true, false);
    _page_node.add_line( false, _("_Flash time:"), _t_node_pathflash_timeout, "ms", _("Specifies how long the path outline will be visible after a mouse-over (in milliseconds); specify 0 to have the outline shown until mouse leaves the path"), false);
    _page_node.add_group_header(_("Editing preferences"));
    _t_node_single_node_transform_handles.init(_("Show transform handles for single nodes"), "/tools/nodes/single_node_transform_handles", false);
    _page_node.add_line( true, "", _t_node_single_node_transform_handles, "", _("Show transform handles even when only a single node is selected"));
    _t_node_delete_flat_corner.init("/tools/node/flat-cusp-angle", 0, 180, 1, 5, 135, false, false);
    _page_node.add_line(true, _("Cusp considered flat for deletion:"), _t_node_delete_flat_corner, "degrees or more", _("Preserve shape when deleting flat nodes.\nInsert segments for sharp ones."), false);

    _page_node.add_group_header(_("Delete Modes"));

    auto prep_del_combo = [](std::string const &name, UI::Widget::PrefCombo &widget, UI::NodeDeleteMode initial) {
        Glib::ustring const labels[] = {
            _("Preserve curves only"),
            _("Preserve cusps only"),
            _("Preserve both"),
            _("Straight lines"),
            _("Remove nodes and leave a gap"),
            _("Remove lines and leave a gap")
        };
        int const values[] = {
            // See ui/tool/path-manipulator.h
            (int)UI::NodeDeleteMode::automatic,
            (int)UI::NodeDeleteMode::inverse_auto,
            (int)UI::NodeDeleteMode::curve_fit,
            (int)UI::NodeDeleteMode::line_segment,
            (int)UI::NodeDeleteMode::gap_nodes,
            (int)UI::NodeDeleteMode::gap_lines
        };
        widget.init("/tools/node/delete-mode-" + name, labels, values, (int)initial);
    };

    prep_del_combo("default", _t_node_delete_mode, UI::NodeDeleteMode::automatic);
    prep_del_combo("ctrl", _t_node_delete_mode1, UI::NodeDeleteMode::line_segment);
    prep_del_combo("alt", _t_node_delete_mode2, UI::NodeDeleteMode::gap_nodes);
    prep_del_combo("shift", _t_node_delete_mode3, UI::NodeDeleteMode::curve_fit);
    prep_del_combo("cut", _t_node_cut_mode, UI::NodeDeleteMode::gap_lines);

    _page_node.add_line( false, _("_Delete Modes:"), _t_node_delete_mode, "", _("What happens when nodes are deleted."), false);
    _page_node.add_line( false, "+ Ctrl", _t_node_delete_mode1, "", _("What happens when nodes are deleted while ctrl is held."), false);
    _page_node.add_line( false, "+ Alt", _t_node_delete_mode2, "", _("What happens when nodes are deleted while alt is held."), false);
    _page_node.add_line( false, "+ Shift", _t_node_delete_mode3, "", _("What happens when nodes are deleted while shift is held."), false);
    _page_node.add_line( false, _("Cut Mode:"), _t_node_cut_mode, "", _("What happens when nodes are cut."), false);

    //Tweak
    this->AddNewObjectsStyle(_page_tweak, "/tools/tweak", _("Object paint style"));
    AddSelcueCheckbox(_page_tweak, "/tools/tweak", true);
    AddGradientCheckbox(_page_tweak, "/tools/tweak", false);

    //Zoom
    AddSelcueCheckbox(_page_zoom, "/tools/zoom", true);
    AddGradientCheckbox(_page_zoom, "/tools/zoom", false);

    //Measure
    auto const cb = Gtk::make_managed<PrefCheckButton>();
    cb->init ( _("Ignore first and last points"), "/tools/measure/ignore_1st_and_last", true);
    _page_measure.add_line( false, "", *cb, "", _("The start and end of the measurement tool's control line will not be considered for calculating lengths. Only lengths between actual curve intersections will be displayed."));

    //Shapes
    this->AddSelcueCheckbox(_page_shapes, "/tools/shapes", true);
    this->AddGradientCheckbox(_page_shapes, "/tools/shapes", true);

    //Rectangle
    this->AddNewObjectsStyle(_page_rectangle, "/tools/shapes/rect");
    this->AddConvertGuidesCheckbox(_page_rectangle, "/tools/shapes/rect", true);

    //3D box
    this->AddNewObjectsStyle(_page_3dbox, "/tools/shapes/3dbox");
    this->AddConvertGuidesCheckbox(_page_3dbox, "/tools/shapes/3dbox", true);

    //Ellipse
    this->AddNewObjectsStyle(_page_ellipse, "/tools/shapes/arc");

    //Star
    this->AddNewObjectsStyle(_page_star, "/tools/shapes/star");

    //Spiral
    this->AddNewObjectsStyle(_page_spiral, "/tools/shapes/spiral");

    //Pencil
    this->AddSelcueCheckbox(_page_pencil, "/tools/freehand/pencil", true);
    this->AddNewObjectsStyle(_page_pencil, "/tools/freehand/pencil");
    this->AddDotSizeSpinbutton(_page_pencil, "/tools/freehand/pencil", 3.0);
    this->AddBaseSimplifySpinbutton(_page_pencil, "/tools/freehand/pencil", 25.0);
    _page_pencil.add_group_header( _("Sketch mode"));
    _page_pencil.add_line( true, "", _pencil_average_all_sketches, "",
                            _("If on, the sketch result will be the normal average of all sketches made, instead of averaging the old result with the new sketch"));

    //Pen
    this->AddSelcueCheckbox(_page_pen, "/tools/freehand/pen", true);
    this->AddNewObjectsStyle(_page_pen, "/tools/freehand/pen");
    this->AddDotSizeSpinbutton(_page_pen, "/tools/freehand/pen", 3.0);

    //Calligraphy
    this->AddSelcueCheckbox(_page_calligraphy, "/tools/calligraphic", false);
    this->AddNewObjectsStyle(_page_calligraphy, "/tools/calligraphic");
    _page_calligraphy.add_line( false, "", _calligrapy_keep_selected, "",
                            _("If on, each newly created object will be selected (deselecting previous selection)"));

    //Text
    this->AddSelcueCheckbox(_page_text, "/tools/text", true);
    this->AddGradientCheckbox(_page_text, "/tools/text", true);
    {
        auto cb = Gtk::make_managed<PrefCheckButton>();
        cb->init ( _("Show font samples in the drop-down list"), "/tools/text/show_sample_in_list", true);
        _page_text.add_line( false, "", *cb, "", _("Show font samples alongside font names in the drop-down list in Text bar"));

        _font_dialog.init(_("Show font substitution warning dialog"), "/options/font/substitutedlg", false);
        _page_text.add_line( false, "", _font_dialog, "", _("Show font substitution warning dialog when requested fonts are not available on the system"));
        _font_sample.init("/tools/text/font_sample", true);
        _page_text.add_line( false, _("Font sample"), _font_sample, "", _("Change font preview sample text"), true);

        cb = Gtk::make_managed<PrefCheckButton>();
        cb->init ( _("Use SVG2 auto-flowed text"),  "/tools/text/use_svg2", true);
        _page_text.add_line( false, "", *cb, "", _("Use SVG2 auto-flowed text instead of SVG1.2 auto-flowed text. (Recommended)"));

        _recently_used_fonts_size.init("/tools/text/recently_used_fonts_size", 0, 100, 1, 10, 10, true, false);
        _page_text.add_line( false, _("Fonts in 'Recently used' collection:"), _recently_used_fonts_size, "",
                           _("Maximum number of fonts in the 'Recently used' font collection"), false);
        _recently_used_fonts_size.changed_signal.connect([](double new_size) {
            Inkscape::RecentlyUsedFonts* recently_used_fonts = Inkscape::RecentlyUsedFonts::get();
            recently_used_fonts->change_max_list_size(new_size); });
    }

    //_page_text.add_group_header( _("Text units"));
    //_font_output_px.init ( _("Always output text size in pixels (px)"), "/options/font/textOutputPx", true);
    //_page_text.add_line( true, "", _font_output_px, "", _("Always convert the text size units above into pixels (px) before saving to file"));

    _page_text.add_group_header( _("Font directories"));
    _font_fontsdir_system.init( _("Use Inkscape's fonts directory"), "/options/font/use_fontsdir_system", true);
    _page_text.add_line( true, "", _font_fontsdir_system, "", _("Load additional fonts from \"fonts\" directory located in Inkscape's global \"share\" directory"));
    _font_fontsdir_user.init( _("Use user's fonts directory"), "/options/font/use_fontsdir_user", true);
    _page_text.add_line( true, "", _font_fontsdir_user, "", _("Load additional fonts from \"fonts\" directory located in Inkscape's user configuration directory"));
    _font_fontdirs_custom.init("/options/font/custom_fontdirs", 50);
    _page_text.add_line(true, _("Additional font directories"), _font_fontdirs_custom, "", _("Load additional fonts from custom locations (one path per line)"), true);


    this->AddNewObjectsStyle(_page_text, "/tools/text");

    //Spray
    AddSelcueCheckbox(_page_spray, "/tools/spray", true);
    AddGradientCheckbox(_page_spray, "/tools/spray", false);

    //Eraser
    this->AddNewObjectsStyle(_page_eraser, "/tools/eraser");

    //Paint Bucket
    this->AddSelcueCheckbox(_page_paintbucket, "/tools/paintbucket", false);
    this->AddNewObjectsStyle(_page_paintbucket, "/tools/paintbucket");

    //Gradient
    this->AddSelcueCheckbox(_page_gradient, "/tools/gradient", true);
    _misc_forkvectors.init( _("Prevent sharing of gradient definitions"), "/options/forkgradientvectors/value", true);
    _page_gradient.add_line( false, "", _misc_forkvectors, "",
                           _("When on, shared gradient definitions are automatically forked on change; uncheck to allow sharing of gradient definitions so that editing one object may affect other objects using the same gradient"), true);

    _misc_gradientangle.init("/dialogs/gradienteditor/angle", -359, 359, 1, 90, 0, false, false);
    _page_gradient.add_line( false, _("Linear gradient _angle:"), _misc_gradientangle, "",
                           _("Default angle of new linear gradients in degrees (clockwise from horizontal)"), false);

    _misc_gradient_collect.init(_("Auto-delete unused gradients"), "/option/gradient/auto_collect", true);
    _page_gradient.add_line(
        false, "", _misc_gradient_collect, "",
        _("When enabled, gradients that are not used will be deleted (auto-collected) automatically "
          "from the SVG file. When disabled, unused gradients will be preserved in "
          "the file for later use. (Note: This setting only affects new gradients.)"),
        true);

    //Dropper
    this->AddSelcueCheckbox(_page_dropper, "/tools/dropper", true);
    this->AddGradientCheckbox(_page_dropper, "/tools/dropper", true);

    //Connector
    this->AddSelcueCheckbox(_page_connector, "/tools/connector", true);
    _page_connector.add_line(false, "", _connector_ignore_text, "",
            _("If on, connector attachment points will not be shown for text objects"));

#ifdef WITH_LPETOOL
    //LPETool
    //disabled, because the LPETool is not finished yet.
    this->AddNewObjectsStyle(_page_lpetool, "/tools/lpetool");
#endif // WITH_LPETOOL
}

void InkscapePreferences::get_highlight_colors(guint32 &colorsetbase, guint32 &colorsetsuccess,
                                               guint32 &colorsetwarning, guint32 &colorseterror)
{
    using namespace Inkscape::IO::Resource;
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    Glib::ustring themeiconname = prefs->getString("/theme/iconTheme", prefs->getString("/theme/defaultIconTheme", ""));
    Glib::ustring prefix = "";
    if (prefs->getBool("/theme/darkTheme", false)) {
        prefix = ".dark ";
    }
    auto const higlight = get_filename(ICONS, (themeiconname + "/highlights.css").c_str(), false, true);
    if (!higlight.empty()) {
        std::ifstream ifs(higlight);
        std::string content((std::istreambuf_iterator<char>(ifs)), (std::istreambuf_iterator<char>()));
        Glib::ustring result;
        size_t startpos = content.find(prefix + ".base");
        size_t endpos = content.find("}");
        if (startpos != std::string::npos) {
            result = content.substr(startpos, endpos - startpos);
            size_t startposin = result.find("fill:");
            size_t endposin = result.find(";");
            result = result.substr(startposin + 5, endposin - (startposin + 5));
            Util::trim(result);
            colorsetbase = to_guint32(Gdk::RGBA(result));
        }
        content.erase(0, endpos + 1);
        startpos = content.find(prefix + ".success");
        endpos = content.find("}");
        if (startpos != std::string::npos) {
            result = content.substr(startpos, endpos - startpos);
            size_t startposin = result.find("fill:");
            size_t endposin = result.find(";");
            result = result.substr(startposin + 5, endposin - (startposin + 5));
            Util::trim(result);
            colorsetsuccess = to_guint32(Gdk::RGBA(result));
        }
        content.erase(0, endpos + 1);
        startpos = content.find(prefix + ".warning");
        endpos = content.find("}");
        if (startpos != std::string::npos) {
            result = content.substr(startpos, endpos - startpos);
            size_t startposin = result.find("fill:");
            size_t endposin = result.find(";");
            result = result.substr(startposin + 5, endposin - (startposin + 5));
            Util::trim(result);
            colorsetwarning = to_guint32(Gdk::RGBA(result));
        }
        content.erase(0, endpos + 1);
        startpos = content.find(prefix + ".error");
        endpos = content.find("}");
        if (startpos != std::string::npos) {
            result = content.substr(startpos, endpos - startpos);
            size_t startposin = result.find("fill:");
            size_t endposin = result.find(";");
            result = result.substr(startposin + 5, endposin - (startposin + 5));
            Util::trim(result);
            colorseterror = to_guint32(Gdk::RGBA(result));
        }
    }
}

void InkscapePreferences::resetIconsColors(bool themechange)
{
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();

    Glib::ustring themeiconname = prefs->getString("/theme/iconTheme", prefs->getString("/theme/defaultIconTheme", ""));

    if (!prefs->getBool("/theme/symbolicIcons", false)) {
        _symbolic_base_colors.set_sensitive(false);
        _symbolic_highlight_colors.set_sensitive(false);
        _symbolic_base_color.set_sensitive(false);
        _symbolic_success_color.set_sensitive(false);
        _symbolic_warning_color.set_sensitive(false);
        _symbolic_error_color.set_sensitive(false);
        return;
    }

    auto doChangeIconsColors = false;

    if (prefs->getBool("/theme/symbolicDefaultBaseColors", true) ||
        !prefs->getEntry("/theme/" + themeiconname + "/symbolicBaseColor").isValidUInt()) {
        auto const display = Gdk::Display::get_default();
        if (INKSCAPE.themecontext->getColorizeProvider()) {
            Gtk::StyleProvider::remove_provider_for_display(display, INKSCAPE.themecontext->getColorizeProvider());
        }
        auto base_color = _symbolic_base_color.get_color();
        // This is a hack to fix a problematic style which isn't updated fast enough on
        // change from dark to bright themes
        if (themechange) {
            base_color = to_rgba(_symbolic_base_color.get_current_color().toRGBA());
        }
        // This colors are set on style.css of inkscape, we copy highlight to not use
        guint32 colorsetbase = to_guint32(base_color);
        guint32 colorsetsuccess = colorsetbase;
        guint32 colorsetwarning = colorsetbase;
        guint32 colorseterror = colorsetbase;
        get_highlight_colors(colorsetbase, colorsetsuccess, colorsetwarning, colorseterror);
        _symbolic_base_color.setColor(Colors::Color(colorsetbase));
        prefs->setUInt("/theme/" + themeiconname + "/symbolicBaseColor", colorsetbase);
        _symbolic_base_color.set_sensitive(false);
        doChangeIconsColors = true;
    } else {
        _symbolic_base_color.set_sensitive(true);
    }

    if (prefs->getBool("/theme/symbolicDefaultHighColors", true)) {
        auto const display = Gdk::Display::get_default();
        if (INKSCAPE.themecontext->getColorizeProvider()) {
            Gtk::StyleProvider::remove_provider_for_display(display, INKSCAPE.themecontext->getColorizeProvider());
        }
        auto const success_color = _symbolic_success_color.get_color();
        auto const warning_color = _symbolic_warning_color.get_color();
        auto const error_color   = _symbolic_error_color  .get_color();
        //we copy base to not use
        guint32 colorsetbase = to_guint32(success_color);
        guint32 colorsetsuccess = to_guint32(success_color);
        guint32 colorsetwarning = to_guint32(warning_color);
        guint32 colorseterror = to_guint32(error_color);
        get_highlight_colors(colorsetbase, colorsetsuccess, colorsetwarning, colorseterror);
        _symbolic_success_color.setColor(Colors::Color(colorsetsuccess));
        _symbolic_warning_color.setColor(Colors::Color(colorsetwarning));
        _symbolic_error_color.setColor(Colors::Color(colorseterror));
        prefs->setUInt("/theme/" + themeiconname + "/symbolicSuccessColor", colorsetsuccess);
        prefs->setUInt("/theme/" + themeiconname + "/symbolicWarningColor", colorsetwarning);
        prefs->setUInt("/theme/" + themeiconname + "/symbolicErrorColor", colorseterror);
        _symbolic_success_color.set_sensitive(false);
        _symbolic_warning_color.set_sensitive(false);
        _symbolic_error_color.set_sensitive(false);
        doChangeIconsColors = true;
    } else {
        _symbolic_success_color.set_sensitive(true);
        _symbolic_warning_color.set_sensitive(true);
        _symbolic_error_color.set_sensitive(true);
    }

    if (doChangeIconsColors) {
        changeIconsColors();
    }
}

void InkscapePreferences::resetIconsColorsWrapper() { resetIconsColors(false); }

void InkscapePreferences::changeIconsColors()
{
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    Glib::ustring themeiconname = prefs->getString("/theme/iconTheme", prefs->getString("/theme/defaultIconTheme", ""));
    guint32 colorsetbase = prefs->getUInt("/theme/" + themeiconname + "/symbolicBaseColor", 0x2E3436ff);
    guint32 colorsetsuccess = prefs->getUInt("/theme/" + themeiconname + "/symbolicSuccessColor", 0x4AD589ff);
    guint32 colorsetwarning = prefs->getUInt("/theme/" + themeiconname + "/symbolicWarningColor", 0xF57900ff);
    guint32 colorseterror = prefs->getUInt("/theme/" + themeiconname + "/symbolicErrorColor", 0xCC0000ff);
    _symbolic_base_color.setColor(Colors::Color(colorsetbase));
    _symbolic_success_color.setColor(Colors::Color(colorsetsuccess));
    _symbolic_warning_color.setColor(Colors::Color(colorsetwarning));
    _symbolic_error_color.setColor(Colors::Color(colorseterror));

    auto const &colorize_provider = INKSCAPE.themecontext->getColorizeProvider();
    if (!colorize_provider) return;

    auto const display = Gdk::Display::get_default();
    Gtk::StyleProvider::remove_provider_for_display(display, colorize_provider);

    Glib::ustring css_str = "";
    if (prefs->getBool("/theme/symbolicIcons", false)) {
        css_str = INKSCAPE.themecontext->get_symbolic_colors();
    }

    {
        auto const on_error = [&](auto const &section, auto const &error)
        {
            g_critical("CSSProviderError::load_from_data(): failed to load '%s'\n(%s)",
                       css_str.c_str(), error.what());
        };
        sigc::scoped_connection _ = colorize_provider->signal_parsing_error().connect(on_error);
        colorize_provider->load_from_data(css_str);
    }

    Gtk::StyleProvider::add_provider_for_display(display, colorize_provider,
                                                 GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

void InkscapePreferences::toggleSymbolic()
{
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    Gtk::Window *window = SP_ACTIVE_DESKTOP->getInkscapeWindow();
    if (prefs->getBool("/theme/symbolicIcons", false)) {
        if (window ) {
            window->add_css_class("symbolic");
            window->remove_css_class("regular");
        }
        _symbolic_base_colors.set_sensitive(true);
        _symbolic_highlight_colors.set_sensitive(true);
        Glib::ustring themeiconname = prefs->getString("/theme/iconTheme", prefs->getString("/theme/defaultIconTheme", ""));
        if (prefs->getBool("/theme/symbolicDefaultColors", true) ||
            !prefs->getEntry("/theme/" + themeiconname + "/symbolicBaseColor").isValidUInt()) {
            resetIconsColors();
        } else {
            changeIconsColors();
        }
    } else {
        if (window) {
            window->add_css_class("regular");
            window->remove_css_class("symbolic");
        }
        auto const display = Gdk::Display::get_default();
        if (INKSCAPE.themecontext->getColorizeProvider()) {
            Gtk::StyleProvider::remove_provider_for_display(display, INKSCAPE.themecontext->getColorizeProvider());
        }
        _symbolic_base_colors.set_sensitive(false);
        _symbolic_highlight_colors.set_sensitive(false);
    }
    INKSCAPE.themecontext->getChangeThemeSignal().emit();
    INKSCAPE.themecontext->add_gtk_css(true);
}

void InkscapePreferences::comboThemeChange()
{
    //we reset theming on combo change
    _dark_theme.set_active(false);
    _symbolic_base_colors.set_active(true);
    if (_contrast_theme.getSpinButton()->get_value() != 10.0){
        _contrast_theme.getSpinButton()->set_value(10.0);
    } else {
        themeChange();
    }
}
void InkscapePreferences::contrastThemeChange()
{
    //we reset theming on combo change
    themeChange(true);
}

void InkscapePreferences::themeChange(bool contrastslider)
{
    Gtk::Window *window = SP_ACTIVE_DESKTOP->getInkscapeWindow();

    if (window) {
        auto const display = Gdk::Display::get_default();

        if (INKSCAPE.themecontext->getContrastThemeProvider()) {
            Gtk::StyleProvider::remove_provider_for_display(display, INKSCAPE.themecontext->getContrastThemeProvider());
        }

        if (INKSCAPE.themecontext->getThemeProvider() ) {
            Gtk::StyleProvider::remove_provider_for_display(display, INKSCAPE.themecontext->getThemeProvider() );
        }

        Inkscape::Preferences *prefs = Inkscape::Preferences::get();
        Glib::ustring current_theme = prefs->getString("/theme/gtkTheme", prefs->getString("/theme/defaultGtkTheme", ""));

        _dark_theme.get_parent()->set_visible(dark_themes[current_theme]);

        auto settings = Gtk::Settings::get_default();
        settings->property_gtk_theme_name() = current_theme;

        auto const dark = INKSCAPE.themecontext->isCurrentThemeDark(window);
        bool toggled = prefs->getBool("/theme/darkTheme", false) != dark;
        prefs->setBool("/theme/darkTheme", dark);

        INKSCAPE.themecontext->getChangeThemeSignal().emit();
        INKSCAPE.themecontext->add_gtk_css(true, contrastslider);
        resetIconsColors(toggled);
    }
}

void InkscapePreferences::preferDarkThemeChange()
{
    Gtk::Window *window = SP_ACTIVE_DESKTOP->getInkscapeWindow();
    if (window) {
        Inkscape::Preferences *prefs = Inkscape::Preferences::get();
        auto const dark = INKSCAPE.themecontext->isCurrentThemeDark(window);
        bool toggled = prefs->getBool("/theme/darkTheme", false) != dark;
        prefs->setBool("/theme/darkTheme", dark);
        INKSCAPE.themecontext->getChangeThemeSignal().emit();
        INKSCAPE.themecontext->add_gtk_css(true);
        // we avoid switched base colors
        if (!_symbolic_base_colors.get_active()) {
            prefs->setBool("/theme/symbolicDefaultBaseColors", true);
            resetIconsColors(false);
            _symbolic_base_colors.set_sensitive(true);
            prefs->setBool("/theme/symbolicDefaultBaseColors", false);
        } else {
            resetIconsColors(toggled);
        }
    }
}

void InkscapePreferences::symbolicThemeCheck()
{
    using namespace Inkscape::IO::Resource;
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    Glib::ustring themeiconname = prefs->getString("/theme/iconTheme", prefs->getString("/theme/defaultIconTheme", ""));
    bool symbolic = false;
    auto settings = Gtk::Settings::get_default();
    if (settings) {
        if (themeiconname != "") {
            settings->property_gtk_icon_theme_name() = themeiconname;
        }
    }
    // we always show symbolic in default theme (relays in hicolor theme)
    if (themeiconname != prefs->getString("/theme/defaultIconTheme", "")) {
        for (auto &&folder : get_foldernames(ICONS, { "application" })) {
            auto path = folder;
            const size_t last_slash_idx = folder.find_last_of("\\/");
            if (std::string::npos != last_slash_idx) {
                folder.erase(0, last_slash_idx + 1);
            }

            auto const folder_utf8 = Glib::filename_to_utf8(folder);
            if (folder_utf8 == themeiconname) {
#ifdef _WIN32
                path += g_win32_locale_filename_from_utf8("/symbolic/actions");
#else
                path += "/symbolic/actions";
#endif

                symbolic = !get_filenames(path, {".svg"}, {}).empty();
            }
        }
    } else {
        symbolic = true;
    }
    if (_symbolic_icons.get_parent()) {
        if (!symbolic) {
            _symbolic_icons.set_active(false);
            _symbolic_icons.get_parent()->set_visible(false);
            _symbolic_base_colors.get_parent()->set_visible(false);
            _symbolic_highlight_colors.get_parent()->set_visible(false);
            _symbolic_base_color.get_parent()->get_parent()->set_visible(false);
            _symbolic_success_color.get_parent()->get_parent()->set_visible(false);
        } else {
            _symbolic_icons.get_parent()->set_visible(true);
            _symbolic_base_colors.get_parent()->set_visible(true);
            _symbolic_highlight_colors.get_parent()->set_visible(true);
            _symbolic_base_color.get_parent()->get_parent()->set_visible(true);
            _symbolic_success_color.get_parent()->get_parent()->set_visible(true);
        }
    }
    if (symbolic) {
        if (prefs->getBool("/theme/symbolicDefaultHighColors", true) ||
            prefs->getBool("/theme/symbolicDefaultBaseColors", true) ||
            !prefs->getEntry("/theme/" + themeiconname + "/symbolicBaseColor").isValidUInt()) {
            resetIconsColors();
        } else {
            changeIconsColors();
        }
        auto colorsetbase = prefs->getColor("/theme/" + themeiconname + "/symbolicBaseColor", "#2E3436ff");
        auto colorsetsuccess = prefs->getColor("/theme/" + themeiconname + "/symbolicSuccessColor", "#4AD589ff");
        auto colorsetwarning = prefs->getColor("/theme/" + themeiconname + "/symbolicWarningColor", "#F57900ff");
        auto colorseterror = prefs->getColor("/theme/" + themeiconname + "/symbolicErrorColor", "#CC0000ff");
        _symbolic_base_color.init(_("Color for symbolic icons:"), "/theme/" + themeiconname + "/symbolicBaseColor",
                                  colorsetbase.toString());
        _symbolic_success_color.init(_("Color for symbolic success icons:"),
                                     "/theme/" + themeiconname + "/symbolicSuccessColor", colorsetsuccess.toString());
        _symbolic_warning_color.init(_("Color for symbolic warning icons:"),
                                     "/theme/" + themeiconname + "/symbolicWarningColor", colorsetwarning.toString());
        _symbolic_error_color.init(_("Color for symbolic error icons:"),
                                   "/theme/" + themeiconname + "/symbolicErrorColor", colorseterror.toString());
    }
}

static Cairo::RefPtr<Cairo::Surface> draw_color_preview(unsigned int rgb, unsigned int frame_rgb, int device_scale) {
    int size = Widget::IconComboBox::get_image_size();
    auto surface = Cairo::ImageSurface::create(Cairo::Surface::Format::ARGB32, size * device_scale, size * device_scale);
    cairo_surface_set_device_scale(surface->cobj(), device_scale, device_scale);
    auto ctx = Cairo::Context::create(surface);
    ctx->arc(size / 2, size / 2, size / 2, 0.0, 2 * M_PI);
    ctx->set_source_rgb((frame_rgb >> 16 & 0xff) / 255.0, (frame_rgb >> 8 & 0xff) / 255.0, (frame_rgb & 0xff) / 255.0);
    ctx->fill();
    size -= 2;
    ctx->set_matrix(Cairo::translation_matrix(1, 1));
    ctx->arc(size / 2, size / 2, size / 2, 0.0, 2 * M_PI);
    ctx->set_source_rgb((rgb >> 16 & 0xff) / 255.0, (rgb >> 8 & 0xff) / 255.0, (rgb & 0xff) / 255.0);
    ctx->fill();
    return surface;
}

// create a preview of few selected handles
void InkscapePreferences::initPageUI()
{
    Gtk::TreeModel::iterator iter_ui = this->AddPage(_page_ui, _("Interface"), PREFS_PAGE_UI);

    Glib::ustring languages[] = {_("System default"),
        _("Albanian (sq)"), _("Arabic (ar)"), _("Armenian (hy)"), _("Assamese (as)"), _("Azerbaijani (az)"),
        _("Basque (eu)"), _("Belarusian (be)"), _("Bulgarian (bg)"), _("Bengali (bn)"), _("Bengali/Bangladesh (bn_BD)"), _("Bodo (brx)"), _("Breton (br)"),
        _("Catalan (ca)"), _("Valencian Catalan (ca@valencia)"), _("Chinese/China (zh_CN)"),  _("Chinese/Taiwan (zh_TW)"), _("Croatian (hr)"), _("Czech (cs)"),
        _("Danish (da)"), _("Dogri (doi)"), _("Dutch (nl)"), _("Dzongkha (dz)"),
        _("German (de)"), _("Greek (el)"),
        _("English (en)"), _("English/Australia (en_AU)"), _("English/Canada (en_CA)"), _("English/Great Britain (en_GB)"), _("Esperanto (eo)"), _("Estonian (et)"),
        _("Farsi (fa)"), _("Finnish (fi)"), _("French (fr)"),
        _("Galician (gl)"), _("Gujarati (gu)"),
        _("Hebrew (he)"), _("Hindi (hi)"), _("Hungarian (hu)"),
        _("Icelandic (is)"), _("Indonesian (id)"), _("Irish (ga)"), _("Italian (it)"),
        _("Japanese (ja)"),
        _("Kannada (kn)"), _("Kashmiri in Perso-Arabic script (ks@aran)"), _("Kashmiri in Devanagari script (ks@deva)"), _("Khmer (km)"), _("Kinyarwanda (rw)"), _("Konkani (kok)"), _("Konkani in Latin script (kok@latin)"), _("Korean (ko)"),
        _("Latvian (lv)"), _("Lithuanian (lt)"),
        _("Macedonian (mk)"), _("Maithili (mai)"), _("Malayalam (ml)"), _("Manipuri (mni)"), _("Manipuri in Bengali script (mni@beng)"), _("Marathi (mr)"), _("Mongolian (mn)"),
        _("Nepali (ne)"), _("Norwegian Bokmål (nb)"), _("Norwegian Nynorsk (nn)"),
        _("Odia (or)"),
        _("Panjabi (pa)"), _("Polish (pl)"), _("Portuguese (pt)"), _("Portuguese/Brazil (pt_BR)"),
        _("Romanian (ro)"), _("Russian (ru)"),
        _("Sanskrit (sa)"), _("Santali (sat)"), _("Santali in Devanagari script (sat@deva)"), _("Serbian (sr)"), _("Serbian in Latin script (sr@latin)"),
        _("Sindhi (sd)"), _("Sindhi in Devanagari script (sd@deva)"), _("Slovak (sk)"), _("Slovenian (sl)"),  _("Spanish (es)"), _("Spanish/Mexico (es_MX)"), _("Swedish (sv)"),
        _("Tamil (ta)"), _("Telugu (te)"), _("Thai (th)"), _("Turkish (tr)"),
        _("Ukrainian (uk)"), _("Urdu (ur)"),
        _("Vietnamese (vi)")};
    Glib::ustring langValues[] = {"",
        "sq", "ar", "hy", "as", "az",
        "eu", "be", "bg", "bn", "bn_BD", "brx", "br",
        "ca", "ca@valencia", "zh_CN", "zh_TW", "hr", "cs",
        "da", "doi", "nl", "dz",
        "de", "el",
        "en", "en_AU", "en_CA", "en_GB", "eo", "et",
        "fa", "fi", "fr",
        "gl", "gu",
        "he", "hi", "hu",
        "is", "id", "ga", "it",
        "ja",
        "kn", "ks@aran", "ks@deva", "km", "rw", "kok", "kok@latin", "ko",
        "lv", "lt",
        "mk", "mai", "ml", "mni", "mni@beng", "mr", "mn",
        "ne", "nb", "nn",
        "or",
        "pa", "pl", "pt", "pt_BR",
        "ro", "ru",
        "sa", "sat", "sat@deva", "sr", "sr@latin",
        "sd", "sd@deva", "sk", "sl", "es", "es_MX", "sv",
        "ta", "te", "th", "tr",
        "uk", "ur",
        "vi" };

    {
        // sorting languages according to translated name
        int i = 0;
        int j = 0;
        int n = sizeof( languages ) / sizeof( Glib::ustring );
        Glib::ustring key_language;
        Glib::ustring key_langValue;
        for ( j = 1 ; j < n ; j++ ) {
            key_language = languages[j];
            key_langValue = langValues[j];
            i = j-1;
            while ( i >= 0
                    && ( ( languages[i] > key_language
                         && langValues[i] != "" )
                       || key_langValue == "" ) )
            {
                languages[i+1] = languages[i];
                langValues[i+1] = langValues[i];
                i--;
            }
            languages[i+1] = key_language;
            langValues[i+1] = key_langValue;
        }
    }

    _ui_languages.init( "/ui/language", languages, langValues, languages[0]);
    _ui_languages.enable_search();
    _page_ui.add_line( false, _("Language:"), _ui_languages, "",
                              _("Set the language for menus and number formats"), false, reset_icon());

    Inkscape::Preferences *prefs = Inkscape::Preferences::get();

    _misc_recent.init("/options/maxrecentdocuments/value", 0.0, 1000.0, 1.0, 1.0, 1.0, true, false);

    auto const reset_recent = Gtk::make_managed<Gtk::Button>(_("Clear list"));
    reset_recent->signal_clicked().connect(sigc::mem_fun(*this, &InkscapePreferences::on_reset_open_recent_clicked));

    _page_ui.add_line( false, _("Maximum documents\n in Open _Recent:"), _misc_recent, "",
                              _("Set the maximum length of the Open Recent list in the File menu, or clear the list"), false, reset_recent);

    _page_ui.add_group_header(_("_Zoom correction factor (in %)"), 2);
    _page_ui.add_group_note(_("Adjust the slider until the length of the ruler on your screen matches its real length. This information is used when zooming to 1:1, 1:2, etc., to display objects in their true sizes"));
    _ui_zoom_correction.init(300, 30, 0.01, 500.0, 1.0, 10.0, 1.0);
    _page_ui.add_line( true, "", _ui_zoom_correction, "", "", true);

    _ui_realworldzoom.init( _("Show zoom percentage corrected by factor"), "/options/zoomcorrection/shown", true);
    _page_ui.add_line( false, "", _ui_realworldzoom, "", _("Zoom percentage can be either by the physical units or by pixels."));

    _ui_rotationlock.init(_("Lock canvas rotation by default"), "/options/rotationlock", false);
    _page_ui.add_line(false, "", _ui_rotationlock, "",
                       _("Prevent accidental canvas rotation by disabling on-canvas keyboard and mouse actions for rotation"), true);

    _ui_rulersel.init( _("Show selection in ruler"), "/options/ruler/show_bbox", true);
    _page_ui.add_line( false, "", _ui_rulersel, "", _("Shows a blue line in the ruler where the selection is."));

    _page_ui.add_group_header(_("User Interface"), 2);
    // _page_ui.add_group_header(_("Handle size"));
    _mouse_grabsize.init("/options/grabsize/value", 1, 15, 1, 2, 3, 0);
    _page_ui.add_line(true, _("Handle size"), _mouse_grabsize, "", _("Set the relative size of node handles"), true);
    {
        auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL);
        auto img = Gtk::make_managed<Gtk::Picture>();
        auto scale = get_scale_factor();
        auto surface = draw_handles_preview(scale);
        img->set_layout_manager(Gtk::BinLayout::create());
        img->set_size_request(surface->get_width() / scale, surface->get_height() / scale);
        img->set_paintable(to_texture(surface));
        img->set_hexpand();
        img->set_halign(Gtk::Align::CENTER);
        box->append(*img);
        auto cb = Gtk::make_managed<Inkscape::UI::Widget::IconComboBox>(false);
        cb->set_valign(Gtk::Align::CENTER);
        auto& mgr = Handles::Manager::get();
        int i = 0;
        for (auto theme : mgr.get_handle_themes()) {
            unsigned int frame = theme.positive ? 0x000000 : 0xffffff; // black or white
            cb->add_row(draw_color_preview(theme.rgb_accent_color, frame, get_scale_factor()), theme.title, i++);
        }
        cb->refilter();
        cb->set_active_by_id(mgr.get_selected_theme());

        // Update on auto-reload or theme change
        mgr.connectCssUpdated(sigc::track_object(
            [=, this]() { img->set_paintable(to_texture(draw_handles_preview(get_scale_factor()))); }, *this));
        cb->signal_changed().connect([=, this](int id) {
            Handles::Manager::get().select_theme(id);
        });

        box->append(*cb);
        _handle_size = Preferences::PreferencesObserver::create("/options/grabsize/value", [img, this](const Preferences::Entry&){
            img->set_paintable(to_texture(draw_handles_preview(get_scale_factor())));
        });
        _page_ui.add_line(true, _("Handle colors"), *box, "", "Select handle color scheme.");
    }
    _narrow_spinbutton.init(_("Use narrow number entry boxes"), "/theme/narrowSpinButton", false);
    _page_ui.add_line(false, "", _narrow_spinbutton, "", _("Make number editing boxes smaller by limiting padding"), false);

    _page_ui.add_group_header(_("Status bar"), 2);
    auto const sb_style = Gtk::make_managed<UI::Widget::PrefCheckButton>();
    sb_style->init(_("Show current style"), "/statusbar/visibility/style", true);
    _page_ui.add_line(false, "", *sb_style, "", _("Control visibility of current fill, stroke and opacity in status bar."), true);
    auto const sb_layer = Gtk::make_managed<UI::Widget::PrefCheckButton>();
    sb_layer->init(_("Show layer selector"), "/statusbar/visibility/layer", true);
    _page_ui.add_line(false, "", *sb_layer, "", _("Control visibility of layer selection menu in status bar."), true);
    auto const sb_coords = Gtk::make_managed<UI::Widget::PrefCheckButton>();
    sb_coords->init(_("Show mouse coordinates"), "/statusbar/visibility/coordinates", true);
    _page_ui.add_line(false, "", *sb_coords, "", _("Control visibility of mouse coordinates X & Y in status bar."), true);
    auto const sb_rotate = Gtk::make_managed<UI::Widget::PrefCheckButton>();
    sb_rotate->init(_("Show canvas rotation"), "/statusbar/visibility/rotation", true);
    _page_ui.add_line(false, "", *sb_rotate, "", _("Control visibility of canvas rotation in status bar."), true);

    _page_ui.add_group_header(_("Mouse cursors"), 2);
    _ui_cursorscaling.init(_("Enable scaling"), "/options/cursorscaling", true);
    _page_ui.add_line(false, "", _ui_cursorscaling, "", _("When off, cursor scaling is disabled. Cursor scaling may be broken when fractional scaling is enabled."), true);
    _ui_cursor_shadow.init(_("Show drop shadow"), "/options/cursor-drop-shadow", true);
    _page_ui.add_line(false, "", _ui_cursor_shadow, "", _("Control visibility of drop shadow for Inkscape cursors."), true);

    // Theme
    _page_theme.add_group_header(_("Theme"));
    _dark_theme.init(_("Use dark theme"), "/theme/preferDarkTheme", false);
    Glib::ustring current_theme = prefs->getString("/theme/gtkTheme", prefs->getString("/theme/defaultGtkTheme", ""));
    Glib::ustring default_theme = prefs->getString("/theme/defaultGtkTheme");
    Glib::ustring theme = "";
    {
        dark_themes = INKSCAPE.themecontext->get_available_themes();
        std::vector<Glib::ustring> labels;
        std::vector<Glib::ustring> values;
        for (auto const &[theme, dark] : dark_themes) {
            if (theme == default_theme) {
                continue;
            }
            values.emplace_back(theme);
            labels.emplace_back(theme);
        }
        std::sort(labels.begin(), labels.end());
        std::sort(values.begin(), values.end());
        labels.erase(unique(labels.begin(), labels.end()), labels.end());
        values.erase(unique(values.begin(), values.end()), values.end());
        values.emplace_back("");
        Glib::ustring default_theme_label = _("Use system theme");
        default_theme_label += " (" + default_theme + ")";
        labels.emplace_back(default_theme_label);

        _gtk_theme.init("/theme/gtkTheme", labels, values, "");
        _page_theme.add_line(false, _("Change GTK theme:"), _gtk_theme, "", "", false);
        _gtk_theme.signal_changed().connect(sigc::mem_fun(*this, &InkscapePreferences::comboThemeChange));
    }

    _sys_user_themes_dir_copy.init(g_build_filename(g_get_user_data_dir(), "themes", nullptr), _("Open themes folder"));
    _page_theme.add_line(true, _("User themes:"), _sys_user_themes_dir_copy, "", _("Location of the user’s themes"), true, Gtk::make_managed<Gtk::Box>());
    _contrast_theme.init("/theme/contrast", 1, 10, 1, 2, 10, 1);

    _page_theme.add_line(true, "", _dark_theme, "", _("Use dark theme"), true);
    {
        auto const font_scale = Gtk::make_managed<UI::Widget::PrefSlider>();
        font_scale->init(ThemeContext::get_font_scale_pref_path(), 50, 150, 5, 5, 100, 0); // 50% to 150%
        font_scale->getSlider()->set_format_value_func([=](double const val) {
            return Inkscape::ustring::format_classic(std::fixed, std::setprecision(0), val) + "%";
        });
        // Live updates commented out; too disruptive
        // font_scale->getSlider()->signal_value_changed().connect([=](){
            // INKSCAPE.themecontext->adjust_global_font_scale(font_scale->getSlider()->get_value() / 100.0);
        // });
        auto const space = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL);
        space->set_valign(Gtk::Align::CENTER);
        auto const reset = Gtk::make_managed<Gtk::Button>();
        reset->set_tooltip_text(_("Reset font size to 100%"));
        reset->set_image_from_icon_name("reset-settings-symbolic");
        reset->set_size_request(30, -1);
        auto const apply = Gtk::make_managed<Gtk::Button>(_("Apply"));
        apply->set_tooltip_text(_("Apply font size changes to the UI"));
        apply->set_valign(Gtk::Align::FILL);
        apply->set_margin_end(5);
        reset->set_valign(Gtk::Align::FILL);
        space->append(*apply);
        space->append(*reset);
        reset->signal_clicked().connect([=](){
            font_scale->getSlider()->set_value(100);
            INKSCAPE.themecontext->adjustGlobalFontScale(1.0);
        });
        apply->signal_clicked().connect([=](){
            INKSCAPE.themecontext->adjustGlobalFontScale(font_scale->getSlider()->get_value() / 100.0);
        });
        _page_theme.add_line(false, _("_Font scale:"), *font_scale, "", _("Adjust size of UI fonts"), true, space);
    }
    auto const space = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL);
    space->set_size_request(_sb_width / 3, -1);
    _page_theme.add_line(false, _("_Contrast:"), _contrast_theme, "",
                         _("Make background brighter or darker to adjust contrast"), true, space);
    _contrast_theme.getSlider()->signal_value_changed().connect(sigc::mem_fun(*this, &InkscapePreferences::contrastThemeChange));

    _dark_theme.get_parent()->set_visible(dark_themes[current_theme]);

    _dark_theme.signal_toggled().connect(sigc::mem_fun(*this, &InkscapePreferences::preferDarkThemeChange));

    // Icons
    _page_theme.add_group_header(_("Icons"));
    {
        using namespace Inkscape::IO::Resource;
        std::vector<Glib::ustring> labels;
        std::vector<Glib::ustring> values;
        Glib::ustring default_icon_theme = prefs->getString("/theme/defaultIconTheme");
        for (auto &&folder : get_foldernames(ICONS, { "application" })) {
            // from https://stackoverflow.com/questions/8520560/get-a-file-name-from-a-path#8520871
            // Maybe we can link boost path utilities
            // Remove directory if present.
            // Do this before extension removal in case the directory has a period character.
            const size_t last_slash_idx = folder.find_last_of("\\/");
            if (std::string::npos != last_slash_idx) {
                folder.erase(0, last_slash_idx + 1);
            }

            // we want use Adwaita instead fallback hicolor theme
            auto const folder_utf8 = Glib::filename_to_utf8(folder);
            if (folder_utf8 == default_icon_theme) {
                continue;
            }

            labels.emplace_back(          folder) ;
            values.emplace_back(std::move(folder));
        }
        std::sort(labels.begin(), labels.end());
        std::sort(values.begin(), values.end());
        labels.erase(unique(labels.begin(), labels.end()), labels.end());
        values.erase(unique(values.begin(), values.end()), values.end());
        values.emplace_back("");
        Glib::ustring default_icon_label = _("Use system icons");
        default_icon_label += " (" + default_icon_theme + ")";
        labels.emplace_back(default_icon_label);

        _icon_theme.init("/theme/iconTheme", labels, values, "");
        _page_theme.add_line(false, _("Change icon theme:"), _icon_theme, "", "", false);
        _icon_theme.signal_changed().connect(sigc::mem_fun(*this, &InkscapePreferences::symbolicThemeCheck));
        _sys_user_icons_dir_copy.init((char const *)IO::Resource::get_path(IO::Resource::USER, IO::Resource::ICONS, ""),
                             _("Open icons folder"));
        _page_theme.add_line(true, _("User icons: "), _sys_user_icons_dir_copy, "", _("Location of the user’s icons"), true, Gtk::make_managed<Gtk::Box>());
    }
    Glib::ustring themeiconname = prefs->getString("/theme/iconTheme", prefs->getString("/theme/defaultIconTheme", ""));
    _symbolic_icons.init(_("Use symbolic icons"), "/theme/symbolicIcons", false);
    _symbolic_icons.signal_toggled().connect(sigc::mem_fun(*this, &InkscapePreferences::toggleSymbolic));
    _page_theme.add_line(true, "", _symbolic_icons, "", "", true);
    _symbolic_base_colors.init(_("Use default base color for icons"), "/theme/symbolicDefaultBaseColors", true);
    _symbolic_base_colors.signal_toggled().connect(sigc::mem_fun(*this, &InkscapePreferences::resetIconsColorsWrapper));
    _page_theme.add_line(true, "", _symbolic_base_colors, "", "", true);
    _symbolic_highlight_colors.init(_("Use default highlight colors for icons"), "/theme/symbolicDefaultHighColors", true);
    _symbolic_highlight_colors.signal_toggled().connect(sigc::mem_fun(*this, &InkscapePreferences::resetIconsColorsWrapper));
    _page_theme.add_line(true, "", _symbolic_highlight_colors, "", "", true);
    _symbolic_base_color.init(_("Color for symbolic icons:"), "/theme/" + themeiconname + "/symbolicBaseColor",
                              "#2E3436ff");
    _symbolic_success_color.init(_("Color for symbolic success icons:"),
                                 "/theme/" + themeiconname + "/symbolicSuccessColor", "#4AD589ff");
    _symbolic_warning_color.init(_("Color for symbolic warning icons:"),
                                 "/theme/" + themeiconname + "/symbolicWarningColor", "#F57900ff");
    _symbolic_error_color.init(_("Color for symbolic error icons:"), "/theme/" + themeiconname + "/symbolicErrorColor",
                               "#CC0000ff");
    _symbolic_base_color.add_css_class("system_base_color");
    _symbolic_success_color.add_css_class("system_success_color");
    _symbolic_warning_color.add_css_class("system_warning_color");
    _symbolic_error_color.add_css_class("system_error_color");
    _symbolic_base_color.add_css_class("symboliccolors");
    _symbolic_success_color.add_css_class("symboliccolors");
    _symbolic_warning_color.add_css_class("symboliccolors");
    _symbolic_error_color.add_css_class("symboliccolors");
    auto const changeIconsColor = sigc::hide(sigc::mem_fun(*this, &InkscapePreferences::changeIconsColors));
    _symbolic_base_color.connectChanged   (changeIconsColor);
    _symbolic_warning_color.connectChanged(changeIconsColor);
    _symbolic_success_color.connectChanged(changeIconsColor);
    _symbolic_error_color.connectChanged  (changeIconsColor);
    auto const icon_buttons = Gtk::make_managed<Gtk::Box>();
    UI::pack_start(*icon_buttons, _symbolic_base_color, true, true, 4);
    _page_theme.add_line(false, "", *icon_buttons, _("Icon color base"),
                         _("Base color for icons"), false);
    auto const icon_buttons_hight = Gtk::make_managed<Gtk::Box>();
    UI::pack_start(*icon_buttons_hight, _symbolic_success_color, true, true, 4);
    UI::pack_start(*icon_buttons_hight, _symbolic_warning_color, true, true, 4);
    UI::pack_start(*icon_buttons_hight, _symbolic_error_color, true, true, 4);
    _page_theme.add_line(false, "", *icon_buttons_hight, _("Icon color highlights"),
                         _("Highlight colors supported by some symbolic icon themes"),
                         false);
    auto const icon_buttons_def = Gtk::make_managed<Gtk::Box>();
    resetIconsColors();
    changeIconsColors();
    _page_theme.add_line(false, "", *icon_buttons_def, "",
                         _("Reset theme colors for some symbolic icon themes"),
                         false);
        Glib::ustring menu_icons_labels[] = {_("Yes"), _("No"), _("Theme decides")};
        int menu_icons_values[] = {1, -1, 0};
        _menu_icons.init("/theme/menuIcons", menu_icons_labels, menu_icons_values, 0);
        _page_theme.add_line(false, _("Show icons in menus:"), _menu_icons, "",
                             _("You can either enable or disable all icons in menus. By default, the setting for the 'use-icon' attribute in the 'menus.ui' file determines whether to display icons in menus."), false, reset_icon());
        _shift_icons.init(_("Shift icons in menus"), "/theme/shiftIcons", true);
        _page_theme.add_line(true, "", _shift_icons, "",
                             _("This preference fixes icon positions in menus."), false, reset_icon());

    _page_theme.add_group_header(_("XML Editor"));
#if WITH_GSOURCEVIEW
    {
        auto manager = gtk_source_style_scheme_manager_get_default();
        auto ids = gtk_source_style_scheme_manager_get_scheme_ids(manager);

        auto const syntax = Gtk::make_managed<UI::Widget::PrefCombo>();
        std::vector<Glib::ustring> labels;
        std::vector<Glib::ustring> values;
        for (const char* style = *ids; style; style = *++ids) {
            if (auto scheme = gtk_source_style_scheme_manager_get_scheme(manager, style)) {
                auto name = gtk_source_style_scheme_get_name(scheme);
                labels.emplace_back(name);
            }
            else {
                labels.emplace_back(style);
            }
            values.emplace_back(style);
        }
        syntax->init("/theme/syntax-color-theme", labels, values, "");
        _page_theme.add_line(false, _("Color theme:"), *syntax, "", _("Syntax coloring for XML Editor"), false);
    }
#endif
    {
        auto const font_button = Gtk::make_managed<Gtk::Button>("...");
        font_button->set_halign(Gtk::Align::START);
        auto const font_box = Gtk::make_managed<Gtk::Entry>();
        font_box->set_editable(false);
        font_box->set_sensitive(false);
        auto theme = INKSCAPE.themecontext;
        font_box->set_text(theme->getMonospacedFont().to_string());
        font_button->signal_clicked().connect([=, this]{
            auto dlg = std::make_unique<Gtk::FontChooserDialog>();
            // show fixed-size fonts only
            dlg->set_filter_func([](const Glib::RefPtr<const Pango::FontFamily>& family, const Glib::RefPtr<const Pango::FontFace>& face) {
                return family && family->is_monospace();
            });
            dlg->set_font_desc(theme->getMonospacedFont());
            dlg->signal_response().connect([=, d = dlg.get()] (int response) {
                if (response == Gtk::ResponseType::OK) {
                    auto desc = d->get_font_desc();
                    theme->saveMonospacedFont(desc);
                    theme->adjustGlobalFontScale(theme->getFontScale() / 100);
                    font_box->set_text(desc.to_string());
                }
            });
            dialog_show_modal_and_selfdestruct(std::move(dlg), get_root());
        });
        _page_theme.add_line(false, _("Monospaced font:"), *font_box, "", _("Select fixed-width font"), true, font_button);

        auto const mono_font = Gtk::make_managed<UI::Widget::PrefCheckButton>();
        mono_font->init( _("Use monospaced font"), "/dialogs/xml/mono-font", false);
        _page_theme.add_line(false, _("XML tree:"), *mono_font, "", _("Use fixed-width font in XML Editor"), false);
    }

    //=======================================================================================================

    this->AddPage(_page_theme, _("Theming"), iter_ui, PREFS_PAGE_UI_THEME);
    symbolicThemeCheck();

    // Toolbars
    _page_toolbars.add_group_header(_("Toolbars"));
    try {
        auto builder = Inkscape::UI::create_builder("toolbar-tool-prefs.ui");
        auto &toolbox = get_widget<Gtk::Box>(builder, "tool-toolbar-prefs");

        for_each_descendant(toolbox, [=](Gtk::Widget &widget) {
            if (auto const button = dynamic_cast<Gtk::ToggleButton *>(&widget)) {
                // do not execute any action:
                button->set_action_name("");

                button->set_sensitive();
                auto action_name = sp_get_action_target(button);
                auto path = Inkscape::UI::Toolbar::ToolToolbar::get_tool_visible_button_path(action_name);
                auto visible = Inkscape::Preferences::get()->getBool(path, true);
                button->set_active(visible);
                button->signal_clicked().connect([=](){
                    bool new_state = !button->get_active();
                    button->set_active(new_state);                    
                    Inkscape::Preferences::get()->setBool(path, button->get_active());
                });
                auto *iapp = InkscapeApplication::instance();
                if (iapp) {
                    auto tooltip =
                        iapp->get_action_extra_data().get_tooltip_for_action(get_tool_action(action_name), true, true);
                    button->set_tooltip_markup(tooltip);
                }
            }
            return ForEachResult::_continue;
        });

        _page_toolbars.add_line(false, "", toolbox, "", _("Select visible tool buttons"), true);

        struct tbar_info {const char* label; const char* prefs;} toolbars[] = {
            {_("Toolbox icon size:"),     Inkscape::UI::Toolbar::tools_icon_size},
            {_("Control bar icon size:"), Inkscape::UI::Toolbar::ctrlbars_icon_size},
        };
        const int min = Inkscape::UI::Toolbar::min_pixel_size;
        const int max = Inkscape::UI::Toolbar::max_pixel_size;
        auto const format_value = [](int const val) -> Glib::ustring
                                  { return std::to_string(100 * val / min) += '%'; };
        for (auto&& tbox : toolbars) {
            auto const slider = Gtk::make_managed<UI::Widget::PrefSlider>(false);
            slider->init(tbox.prefs, min, max, 1, 4, min, 0);
            slider->getSlider()->set_format_value_func(format_value);
            slider->getSlider()->set_draw_value();
            slider->getSlider()->add_css_class("small-marks");
            for (int i = min; i <= max; i += 8) {
                auto const markup = (i % min == 0) ? format_value(i) : Glib::ustring{};
                slider->getSlider()->add_mark(i, Gtk::PositionType::BOTTOM, markup);
            }
            _page_toolbars.add_line(false, tbox.label, *slider, "", _("Adjust toolbar icon size"));
        }

        std::vector<PrefItem> snap = {
            { _("Simple"), 1, _("Present simplified snapping options that manage all advanced settings"), true },
            { _("Advanced"), 0, _("Expose all snapping options for manual control") },
            { _("Permanent"), 2, _("All advanced snap options appear in a permanent bar") },
        };
        _page_toolbars.add_line(false, _("Snap controls bar:"), *Gtk::make_managed<PrefRadioButtons>(snap, "/toolbox/simplesnap"), "", "");
    } catch (Glib::Error const &error) {
        g_error("Couldn't load toolbar-tool-prefs user interface file: `%s`", error.what());
    }

    this->AddPage(_page_toolbars, _("Toolbars"), iter_ui, PREFS_PAGE_UI_TOOLBARS);

    // Windows
    _win_save_geom.init ( _("Save and restore window geometry for each document"), "/options/savewindowgeometry/value", PREFS_WINDOW_GEOMETRY_FILE, true, nullptr);
    _win_save_geom_prefs.init ( _("Remember and use last window's geometry"), "/options/savewindowgeometry/value", PREFS_WINDOW_GEOMETRY_LAST, false, &_win_save_geom);
    _win_save_geom_off.init ( _("Don't save window geometry"), "/options/savewindowgeometry/value", PREFS_WINDOW_GEOMETRY_NONE, false, &_win_save_geom);

    _win_native.init ( _("Native open/save dialogs"), "/options/desktopintegration/value", 1, true, nullptr);
    _win_gtk.init ( _("GTK open/save dialogs"), "/options/desktopintegration/value", 0, false, &_win_native);

    {
        Glib::ustring startModeLabels[] = {C_("Start mode", "Nothing"),
                                             C_("Start mode", "Splash screen only"),
                                             C_("Start mode", "Welcome screen")};
        int startModeValues[] = {0, 1, 2};

        _win_start_mode.init( "/options/boot/mode", startModeLabels, startModeValues, 2);
        _page_windows.add_line( false, _("Show when starting:"),  _win_start_mode, "",
                           _("Set what shows when loading the program normally."), false);
    }

    _win_hide_task.init ( _("Dialogs are hidden in taskbar"), "/options/dialogsskiptaskbar/value", true);
    _win_save_viewport.init ( _("Save and restore documents viewport"), "/options/savedocviewport/value", true);
    _win_zoom_resize.init ( _("Zoom when window is resized"), "/options/stickyzoom/value", false);
    _win_ontop_none.init ( C_("Dialog on top", "None"), "/options/transientpolicy/value", PREFS_DIALOGS_WINDOWS_NONE, false, nullptr);
    _win_ontop_normal.init ( _("Normal"), "/options/transientpolicy/value", PREFS_DIALOGS_WINDOWS_NORMAL, true, &_win_ontop_none);
    _win_ontop_agressive.init ( _("Aggressive"), "/options/transientpolicy/value", PREFS_DIALOGS_WINDOWS_AGGRESSIVE, false, &_win_ontop_none);

    _win_dialogs_labels_auto.init( _("Automatic"), "/options/notebooklabels/value", PREFS_NOTEBOOK_LABELS_AUTO, true, nullptr);
    _win_dialogs_labels_active.init( _("Active"), "/options/notebooklabels/value", PREFS_NOTEBOOK_LABELS_ACTIVE, true, nullptr);
    _win_dialogs_labels_off.init( _("Off"), "/options/notebooklabels/value", PREFS_NOTEBOOK_LABELS_OFF, false, &_win_dialogs_labels_auto);

    _win_dialogs_tab_close_btn.init(_("Show close button in tab"), "/options/notebooktabs/closebutton", true);

    {
        Glib::ustring defaultSizeLabels[] = {C_("Window size", "Default"),
                                             C_("Window size", "Small"),
                                             C_("Window size", "Large"),
                                             C_("Window size", "Maximized")};
        int defaultSizeValues[] = {PREFS_WINDOW_SIZE_NATURAL,
                                   PREFS_WINDOW_SIZE_SMALL,
                                   PREFS_WINDOW_SIZE_LARGE,
                                   PREFS_WINDOW_SIZE_MAXIMIZED};

        _win_default_size.init( "/options/defaultwindowsize/value", defaultSizeLabels, defaultSizeValues, PREFS_WINDOW_SIZE_NATURAL);
        _page_windows.add_line( false, _("Default window size:"),  _win_default_size, "",
                           _("Set the default window size"), false);
    }

    _page_windows.add_group_header( _("Saving window size and position"), 4);
    _page_windows.add_line( true, "", _win_save_geom_off, "",
                            _("Let the window manager determine placement of all windows"));
    _page_windows.add_line( true, "", _win_save_geom_prefs, "",
                            _("Remember and use the last window's geometry (saves geometry to user preferences)"));
    _page_windows.add_line( true, "", _win_save_geom, "",
                            _("Save and restore window geometry for each document (saves geometry in the document)"));

#ifdef _WIN32
    _page_windows.add_group_header( _("Desktop integration"));
    _page_windows.add_line( true, "", _win_native, "",
                            _("Use Windows like open and save dialogs"));
    _page_windows.add_line( true, "", _win_gtk, "",
                            _("Use GTK open and save dialogs "));
#endif
    _page_windows.add_group_header(_("Dialogs settings"), 4);

    std::vector<PrefItem> dock = {
        { _("Docked"), PREFS_DIALOGS_BEHAVIOR_DOCKABLE, _("Allow dialog docking"), true },
        { _("Floating"), PREFS_DIALOGS_BEHAVIOR_FLOATING, _("Disable dialog docking") }
    };
    _page_windows.add_line(true, _("Dialog behavior"), *Gtk::make_managed<PrefRadioButtons>(dock, "/options/dialogtype/value"), "", "", false, reset_icon());

#ifndef _WIN32 // non-Win32 special code to enable transient dialogs
    std::vector<PrefItem> on_top = {
        { C_("Dialog on top", "None"), PREFS_DIALOGS_WINDOWS_NONE, _("Dialogs are treated as regular windows") },
        { _("Normal"),    PREFS_DIALOGS_WINDOWS_NORMAL,     _("Dialogs stay on top of document windows"), true },
        { _("Aggressive"), PREFS_DIALOGS_WINDOWS_AGGRESSIVE, _("Same as Normal but may work better with some window managers") }
    };
    _page_windows.add_line(true, _("Dialog on top"), *Gtk::make_managed<PrefRadioButtons>(on_top, "/options/transientpolicy/value"), "", "");
#endif

    std::vector<PrefItem> labels = {
        { _("Always"), PREFS_NOTEBOOK_LABELS_AUTO, _("Dialog names will be displayed when there is enough space"), true },
        { _("Active tab only"), PREFS_NOTEBOOK_LABELS_ACTIVE, _("Only show label on active tab") },
        { _("Off"), PREFS_NOTEBOOK_LABELS_OFF, _("Only show dialog icons") }
    };
    _page_windows.add_line(true, _("Tab labels"), *Gtk::make_managed<PrefRadioButtons>(labels, "/options/notebooklabels/value"), "", "", false);
    _page_windows.add_line(true, "Dialog tabs", _win_dialogs_tab_close_btn, "", _("Show close button in dialog tabs"));

    auto const save_dlg = Gtk::make_managed<PrefCheckButton>();
    save_dlg->init(_("Save and restore dialogs' status"), "/options/savedialogposition/value", true);
    _page_windows.add_line(true, "", *save_dlg, "", _("Save and restore dialogs' status (the last open windows dialogs are saved when it closes)"));

#ifndef _WIN32 // FIXME: Temporary Win32 special code to enable transient dialogs
    _page_windows.add_line( true, "", _win_hide_task, "",
                            _("Whether dialog windows are to be hidden in the window manager taskbar"));
#endif
    _page_windows.add_group_header( _("Text and Font dialog"));
    std::vector<PrefItem> lister = {
        { _("List fonts and styles"), 0, _("List fonts and styles separately"), true },
        { _("Unified font browser (experimental)"), 1, _("Show all font styles in a single list") }
    };
    _page_windows.add_line(true, _("Font selector"), *Gtk::make_managed<PrefRadioButtons>(lister, "/options/font/browser"), "", "", false, reset_icon());

    _page_windows.add_group_header( _("Miscellaneous"));

    _page_windows.add_line( true, "", _win_zoom_resize, "",
                            _("Zoom drawing when document window is resized, to keep the same area visible (this is the default which can be changed in any window using the button above the right scrollbar)"));
    _page_windows.add_line( true, "", _win_save_viewport, "",
                            _("Save documents viewport (zoom and panning position). Useful to turn off when sharing version controlled files."));

    this->AddPage(_page_windows, _("Windows"), iter_ui, PREFS_PAGE_UI_WINDOWS);

    // default colors in RGBA
    static auto GRID_DEFAULT_MAJOR_COLOR = "#0099e54d";
    static auto GRID_DEFAULT_BLOCK_COLOR = "#0047cb4d";

    // Color pickers
    _compact_colorselector.init(_("Use compact color selector mode switch"), "/colorselector/switcher", true);
    _page_color_pickers.add_line(false, "", _compact_colorselector, "", _("Use compact combo box for selecting color modes"), false);

    _page_color_pickers.add_group_header(_("Visible color pickers"));
    {
        auto const container = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL);
        auto prefs = Inkscape::Preferences::get();
        for (auto& space : Colors::Manager::get().spaces(Colors::Space::Traits::Picker)) {
            auto const btn = Gtk::make_managed<Gtk::ToggleButton>();
            auto const box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL);
            auto const label = Gtk::make_managed<Gtk::Label>(space->getName());
            label->set_valign(Gtk::Align::CENTER);
            UI::pack_start(*box, *label);
            UI::pack_start(*box, *Gtk::make_managed<Gtk::Image>(Gio::ThemedIcon::create(space->getIcon())));
            box->set_spacing(3);
            auto path = space->getPrefsPath() + "visible";
            btn->set_active(prefs->getBool(path));
            btn->set_child(*box);
            btn->set_has_frame(false);

            btn->signal_toggled().connect([=, path = std::move(path)]() {
                prefs->setBool(path, btn->get_active());

                auto const buttons = UI::get_children(*container);
                auto const is_active = [](Gtk::Widget const * const child)
                    { return dynamic_cast<Gtk::ToggleButton const &>(*child).get_active(); };
                if (!buttons.empty() && std::none_of(buttons.begin(), buttons.end(), is_active)) {
                    // all pickers hidden; not a good combination; select first one
                    dynamic_cast<Gtk::ToggleButton &>(*buttons.front()).set_active(true);
                }
            });

            UI::pack_start(*container, *btn);
        }

        container->set_spacing(5);
        _page_color_pickers.add_line(true, "", *container, "", _("Select color pickers"), false);
    }

    AddPage(_page_color_pickers, _("Color Selector"), iter_ui, PREFS_PAGE_UI_COLOR_PICKERS);
    // end of Color pickers

    // Grids
    _page_grids.add_group_header( _("Line color when zooming out"));

    _grids_no_emphasize_on_zoom.init( _("Minor grid line color"), "/options/grids/no_emphasize_when_zoomedout", 1, true, nullptr);
    _page_grids.add_line( true, "", _grids_no_emphasize_on_zoom, "", _("The gridlines will be shown in minor grid line color"), false);
    _grids_emphasize_on_zoom.init( _("Major grid line color"), "/options/grids/no_emphasize_when_zoomedout", 0, false, &_grids_no_emphasize_on_zoom);
    _page_grids.add_line( true, "", _grids_emphasize_on_zoom, "", _("The gridlines will be shown in major grid line color"), false);

    _page_grids.add_group_header( _("Default grid settings"));

    _page_grids.add_line( true, "", _grids_notebook, "", "", false);
    _grids_notebook.set_halign(Gtk::Align::START);
    _grids_notebook.set_hexpand(false);
    auto& grid_modular = *Gtk::make_managed<UI::Widget::DialogPage>();
    _grids_notebook.append_page(_grids_xy,     _("Rectangular Grid"));
    _grids_notebook.append_page(_grids_axonom, _("Axonometric Grid"));
    _grids_notebook.append_page(grid_modular, _("Modular Grid"));
    {
    // Rectangular SPGrid properties
        _grids_xy_units.init("/options/grids/xy/units");
        _grids_xy.add_line( false, _("Grid units:"), _grids_xy_units, "", "", false);
        _grids_xy.add_line( false, _("Origin X:"), _grids_xy_origin_x, "", _("X coordinate of grid origin"), false);
        _grids_xy.add_line( false, _("Origin Y:"), _grids_xy_origin_y, "", _("Y coordinate of grid origin"), false);
        _grids_xy.add_line( false, _("Spacing X:"), _grids_xy_spacing_x, "", _("Distance between vertical grid lines"), false);
        _grids_xy.add_line( false, _("Spacing Y:"), _grids_xy_spacing_y, "", _("Distance between horizontal grid lines"), false);

        _grids_xy_empcolor.init(_("Grid color:"), "/options/grids/xy/empcolor", GRID_DEFAULT_MAJOR_COLOR);
        _grids_xy.add_line( false, _("Grid color:"), _grids_xy_empcolor, "", _("Color used for grid lines"), false);
        _grids_xy_empspacing.init("/options/grids/xy/empspacing", 1.0, 1000.0, 1.0, 5.0, 5.0, true, false);
        _grids_xy.add_line( false, _("Major grid line every:"), _grids_xy_empspacing, "", "", false);
        _grids_xy_dotted.init( _("Show dots instead of lines"), "/options/grids/xy/dotted", false);
        _grids_xy.add_line( false, "", _grids_xy_dotted, "", _("If set, display dots at gridpoints instead of gridlines"), false);

    // Axonometric SPGrid properties:
        _grids_axonom_units.init("/options/grids/axonom/units");
        _grids_axonom.add_line( false, _("Grid units:"), _grids_axonom_units, "", "", false);
        _grids_axonom.add_line( false, _("Origin X:"), _grids_axonom_origin_x, "", _("X coordinate of grid origin"), false);
        _grids_axonom.add_line( false, _("Origin Y:"), _grids_axonom_origin_y, "", _("Y coordinate of grid origin"), false);
        _grids_axonom.add_line( false, _("Spacing Y:"), _grids_axonom_spacing_y, "", _("Base length of z-axis"), false);
        _grids_axonom_angle_x.init("/options/grids/axonom/angle_x", -360.0, 360.0, 1.0, 10.0, 30.0, false, false);
        _grids_axonom_angle_z.init("/options/grids/axonom/angle_z", -360.0, 360.0, 1.0, 10.0, 30.0, false, false);
        _grids_axonom.add_line( false, _("Angle X:"), _grids_axonom_angle_x, "", _("Angle of x-axis"), false);
        _grids_axonom.add_line( false, _("Angle Z:"), _grids_axonom_angle_z, "", _("Angle of z-axis"), false);
        _grids_axonom_empcolor.init(_("Grid color:"), "/options/grids/axonom/empcolor", GRID_DEFAULT_MAJOR_COLOR);
        _grids_axonom.add_line( false, _("Grid color:"), _grids_axonom_empcolor, "", _("Color used for grid lines"), false);
        _grids_axonom_empspacing.init("/options/grids/axonom/empspacing", 1.0, 1000.0, 1.0, 5.0, 5.0, true, false);
        _grids_axonom.add_line( false, _("Major grid line every:"), _grids_axonom_empspacing, "", "", false);
    // Modular grid
        auto const units = Gtk::make_managed<UI::Widget::PrefUnit>();
        units->init("/options/grids/modular/units");
        auto const origin_x = Gtk::make_managed<UI::Widget::PrefSpinButton>();
        auto const origin_y = Gtk::make_managed<UI::Widget::PrefSpinButton>();
        auto const block_width = Gtk::make_managed<UI::Widget::PrefSpinButton>();
        auto const block_height = Gtk::make_managed<UI::Widget::PrefSpinButton>();
        auto const gap_x = Gtk::make_managed<UI::Widget::PrefSpinButton>();
        auto const gap_y = Gtk::make_managed<UI::Widget::PrefSpinButton>();
        auto const margin_x = Gtk::make_managed<UI::Widget::PrefSpinButton>();
        auto const margin_y = Gtk::make_managed<UI::Widget::PrefSpinButton>();
        auto const color_major = Gtk::make_managed<UI::Widget::PrefColorPicker>();
        color_major->init(_("Grid color:"), "/options/grids/modular/empcolor", GRID_DEFAULT_BLOCK_COLOR);

        grid_modular.add_line(false, _("Grid units:"), *units, "", "", false);
        grid_modular.add_line(false, _("Origin X:"), *origin_x, "", _("X coordinate of grid origin"), false);
        grid_modular.add_line(false, _("Origin Y:"), *origin_y, "", _("Y coordinate of grid origin"), false);
        grid_modular.add_line( false, _("Block width:"), *block_width, "", _("Width of grid modules"), false);
        grid_modular.add_line( false, _("Block height:"), *block_height, "", _("Height of grid modules"), false);
        grid_modular.add_line(false, _("Gap X:"), *gap_x, "", _("Horizontal distance between blocks"), false);
        grid_modular.add_line(false, _("Gap Y:"), *gap_y, "", _("Vertical distance between blocks"), false);
        grid_modular.add_line(false, _("Margin X:"), *margin_x, "", _("Right and left margins"), false);
        grid_modular.add_line(false, _("Margin Y:"), *margin_y, "", _("Top and bottom margins"), false);
        grid_modular.add_line( false, _("Grid color:"), *color_major, "", _("Color used for grid blocks"), false);

        for (auto [spin, path] : (std::tuple<PrefSpinButton*, const char*>[]) {
            {&_grids_xy_origin_x,  "/options/grids/xy/origin_x"},
            {&_grids_xy_origin_y,  "/options/grids/xy/origin_y"},
            {&_grids_xy_spacing_x, "/options/grids/xy/spacing_x"},
            {&_grids_xy_spacing_y, "/options/grids/xy/spacing_y"},
            {&_grids_axonom_origin_x,  "/options/grids/axonom/origin_x"},
            {&_grids_axonom_origin_y,  "/options/grids/axonom/origin_y"},
            {&_grids_axonom_spacing_y, "/options/grids/axonom/spacing_y"},
            {origin_x,     "/options/grids/modular/origin_x"},
            {origin_y,     "/options/grids/modular/origin_y"},
            {block_width,  "/options/grids/modular/spacing_x"},
            {block_height, "/options/grids/modular/spacing_y"},
            {gap_x,        "/options/grids/modular/gapx"},
            {gap_y,        "/options/grids/modular/gapy"},
            {margin_x,     "/options/grids/modular/marginx"},
            {margin_y,     "/options/grids/modular/marginy"},
        }) {
            spin->init(path, -10'000.0, 10'000.0, 0.1, 1.0, 0.0, false, false);
            spin->set_digits(5);
            spin->set_width_chars(12);
        }
    }

    this->AddPage(_page_grids, _("Grids"), iter_ui, PREFS_PAGE_UI_GRIDS);

    // Command Palette
    _page_command_palette.add_group_header(_("Display Options"));

    _cp_show_full_action_name.init(_("Show command line argument names"), "/options/commandpalette/showfullactionname/value", false);
    _page_command_palette.add_line(true, "", _cp_show_full_action_name, "", _("Show action argument names in the command palette suggestions, most useful for using them on the command line"));

    _cp_show_untranslated_name.init(_("Show untranslated (English) names"),  "/options/commandpalette/showuntranslatedname/value", true);
    _page_command_palette.add_line(true, "", _cp_show_untranslated_name, "", _("Also show the English names of the command"));

    this->AddPage(_page_command_palette, _("Command Palette"), iter_ui, PREFS_PAGE_COMMAND_PALETTE);
    // /Command Palette


    initKeyboardShortcuts(iter_ui);
}

static void profileComboChanged( Gtk::ComboBoxText* combo )
{
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    int rowNum = combo->get_active_row_number();
    if ( rowNum < 1 ) {
        prefs->setString("/options/displayprofile/uri", "");
    } else {
        Glib::ustring active = combo->get_active_text();

        auto &cms_system = Inkscape::Colors::CMS::System::get();
        if (auto profile = cms_system.getProfile(active)) {
            prefs->setString("/options/displayprofile/uri", profile->getPath());
        }
    }
}

static void proofComboChanged( Gtk::ComboBoxText* combo )
{
    Glib::ustring active = combo->get_active_text();
    auto &cms_system = Inkscape::Colors::CMS::System::get();
    if (auto profile = cms_system.getProfile(active)) {
        Inkscape::Preferences *prefs = Inkscape::Preferences::get();
        prefs->setString("/options/softproof/uri", profile->getPath());
    }
}

static void gamutColorChanged( Gtk::ColorButton* btn ) {
    auto rgba = btn->get_rgba();
    auto r = rgba.get_red_u();
    auto g = rgba.get_green_u();
    auto b = rgba.get_blue_u();

    gchar* tmp = g_strdup_printf("#%02x%02x%02x", (r >> 8), (g >> 8), (b >> 8) );

    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    prefs->setString("/options/softproof/gamutcolor", tmp);
    g_free(tmp);
}

void InkscapePreferences::initPageIO()
{
    Gtk::TreeModel::iterator iter_io = this->AddPage(_page_io, _("Input/Output"), PREFS_PAGE_IO);

    _save_use_current_dir.init( _("Use current directory for \"Save As ...\""), "/dialogs/save_as/use_current_dir", true);
    _page_io.add_line( false, "", _save_use_current_dir, "",
                         _("When this option is on, the \"Save as...\" and \"Save a Copy...\" dialogs will always open in the directory where the currently open document is; when it's off, each will open in the directory where you last saved a file using it"), true);

    _misc_default_metadata.init( _("Add default metadata to new documents"), "/metadata/addToNewFile", false);
    _page_io.add_line( false, "", _misc_default_metadata, "",
                           _("Add default metadata to new documents. Default metadata can be set from Document Properties->Metadata."), true);

    _export_all_extensions.init( _("Show all outputs in Export Dialog"), "/dialogs/export/show_all_extensions", false);
    _page_io.add_line( false, "", _export_all_extensions, "",
                           _("Will list all possible output extensions in the Export Dialog selection."), true);

    // Input devices options
    _mouse_sens.init ( "/options/cursortolerance/value", 0.0, 30.0, 1.0, 1.0, 8.0, true, false);
    _page_mouse.add_line( false, _("_Grab sensitivity:"), _mouse_sens, _("pixels"),
                           _("How close on the screen you need to be to an object to be able to grab it with mouse (in screen pixels)"), false, reset_icon());
    _mouse_thres.init ( "/options/dragtolerance/value", 0.0, 100.0, 1.0, 1.0, 8.0, true, false);
    _page_mouse.add_line( false, _("_Click/drag threshold:"), _mouse_thres, _("pixels"),
                           _("Maximum mouse drag (in screen pixels) which is considered a click, not a drag"), false);

    _mouse_use_ext_input.init( _("Use pressure-sensitive tablet"), "/options/useextinput/value", true);
    _page_mouse.add_line(false, "",_mouse_use_ext_input, "",
                        _("Use the capabilities of a tablet or other pressure-sensitive device. Disable this only if you have problems with the tablet (you can still use it as a mouse)"), false, reset_icon());

    _mouse_switch_on_ext_input.init( _("Switch tool based on tablet device"), "/options/switchonextinput/value", false);
    _page_mouse.add_line(false, "",_mouse_switch_on_ext_input, "",
                        _("Change tool as different devices are used on the tablet (pen, eraser, mouse)"), false, reset_icon());
    this->AddPage(_page_mouse, _("Input devices"), iter_io, PREFS_PAGE_IO_MOUSE);

    // SVG output options
    _svgoutput_usenamedcolors.init( _("Use named colors"), "/options/svgoutput/usenamedcolors", false);
    _page_svgoutput.add_line( false, "", _svgoutput_usenamedcolors, "", _("If set, write the CSS name of the color when available (e.g. 'red' or 'magenta') instead of the numeric value"), false);

    _page_svgoutput.add_group_header( _("XML formatting"));

    _svgoutput_inlineattrs.init( _("Inline attributes"), "/options/svgoutput/inlineattrs", false);
    _page_svgoutput.add_line( true, "", _svgoutput_inlineattrs, "", _("Put attributes on the same line as the element tag"), false);

    _svgoutput_indent.init("/options/svgoutput/indent", 0.0, 1000.0, 1.0, 2.0, 2.0, true, false);
    _page_svgoutput.add_line( true, _("_Indent, spaces:"), _svgoutput_indent, "", _("The number of spaces to use for indenting nested elements; set to 0 for no indentation"), false);

    _page_svgoutput.add_group_header( _("Path data"));

    Glib::ustring const pathstringFormatLabels[] = {_("Absolute"), _("Relative"), _("Optimized")};
    int const pathstringFormatValues[] = {0, 1, 2};
    _svgoutput_pathformat.init("/options/svgoutput/pathstring_format", pathstringFormatLabels, pathstringFormatValues, 2);
    _page_svgoutput.add_line( true, _("Path string format:"), _svgoutput_pathformat, "", _("Path data should be written: only with absolute coordinates, only with relative coordinates, or optimized for string length (mixed absolute and relative coordinates)"), false);

    _svgoutput_forcerepeatcommands.init( _("Force repeat commands"), "/options/svgoutput/forcerepeatcommands", false);
    _page_svgoutput.add_line( true, "", _svgoutput_forcerepeatcommands, "", _("Force repeating of the same path command (for example, 'L 1,2 L 3,4' instead of 'L 1,2 3,4')"), false);

    _page_svgoutput.add_group_header( _("Numbers"));

    _svgoutput_numericprecision.init("/options/svgoutput/numericprecision", 1.0, 16.0, 1.0, 2.0, 8.0, true, false);
    _page_svgoutput.add_line( true, _("_Numeric precision:"), _svgoutput_numericprecision, "", _("Significant figures of the values written to the SVG file"), false);

    _svgoutput_minimumexponent.init("/options/svgoutput/minimumexponent", -32.0, -1, 1.0, 2.0, -8.0, true, false);
    _page_svgoutput.add_line( true, _("Minimum _exponent:"), _svgoutput_minimumexponent, "", _("The smallest number written to SVG is 10 to the power of this exponent; anything smaller is written as zero"), false);

    /* Code to add controls for attribute checking options */

    /* Add incorrect style properties options  */
    _page_svgoutput.add_group_header( _("Improper Attributes Actions"));

    _svgoutput_attrwarn.init( _("Print warnings"), "/options/svgoutput/incorrect_attributes_warn", true);
    _page_svgoutput.add_line( true, "", _svgoutput_attrwarn, "", _("Print warning if invalid or non-useful attributes found. Database files located in inkscape_data_dir/attributes."), false);
    _svgoutput_attrremove.init( _("Remove attributes"), "/options/svgoutput/incorrect_attributes_remove", false);
    _page_svgoutput.add_line( true, "", _svgoutput_attrremove, "", _("Delete invalid or non-useful attributes from element tag"), false);

    /* Add incorrect style properties options  */
    _page_svgoutput.add_group_header( _("Inappropriate Style Properties Actions"));

    _svgoutput_stylepropwarn.init( _("Print warnings"), "/options/svgoutput/incorrect_style_properties_warn", true);
    _page_svgoutput.add_line( true, "", _svgoutput_stylepropwarn, "", _("Print warning if inappropriate style properties found (i.e. 'font-family' set on a <rect>). Database files located in inkscape_data_dir/attributes."), false);
    _svgoutput_stylepropremove.init( _("Remove style properties"), "/options/svgoutput/incorrect_style_properties_remove", false);
    _page_svgoutput.add_line( true, "", _svgoutput_stylepropremove, "", _("Delete inappropriate style properties"), false);

    /* Add default or inherited style properties options  */
    _page_svgoutput.add_group_header( _("Non-useful Style Properties Actions"));

    _svgoutput_styledefaultswarn.init( _("Print warnings"), "/options/svgoutput/style_defaults_warn", true);
    _page_svgoutput.add_line( true, "", _svgoutput_styledefaultswarn, "", _("Print warning if redundant style properties found (i.e. if a property has the default value and a different value is not inherited or if value is the same as would be inherited). Database files located in inkscape_data_dir/attributes."), false);
    _svgoutput_styledefaultsremove.init( _("Remove style properties"), "/options/svgoutput/style_defaults_remove", false);
    _page_svgoutput.add_line( true, "", _svgoutput_styledefaultsremove, "", _("Delete redundant style properties"), false);

    _page_svgoutput.add_group_header( _("Check Attributes and Style Properties on"));

    _svgoutput_check_reading.init( _("Reading"), "/options/svgoutput/check_on_reading", false);
    _page_svgoutput.add_line( true, "", _svgoutput_check_reading, "", _("Check attributes and style properties on reading in SVG files (including those internal to Inkscape which will slow down startup)"), false);
    _svgoutput_check_editing.init( _("Editing"), "/options/svgoutput/check_on_editing", false);
    _page_svgoutput.add_line( true, "", _svgoutput_check_editing, "", _("Check attributes and style properties while editing SVG files (may slow down Inkscape, mostly useful for debugging)"), false);
    _svgoutput_check_writing.init( _("Writing"), "/options/svgoutput/check_on_writing", true);
    _page_svgoutput.add_line( true, "", _svgoutput_check_writing, "", _("Check attributes and style properties on writing out SVG files"), false);

    this->AddPage(_page_svgoutput, _("SVG output"), iter_io, PREFS_PAGE_IO_SVGOUTPUT);

    // SVG Export Options ==========================================

    // SVG 2 Fallbacks
    _page_svgexport.add_group_header( _("SVG 2"));
    _svgexport_insert_text_fallback.init( _("Insert SVG 1.1 fallback in text"),                                     "/options/svgexport/text_insertfallback",    true );
    _svgexport_insert_mesh_polyfill.init( _("Insert JavaScript code for mesh gradients"),                            "/options/svgexport/mesh_insertpolyfill",    true );
    _svgexport_insert_hatch_polyfill.init( _("Insert JavaScript code for SVG2 hatches"),                       "/options/svgexport/hatch_insertpolyfill",   true );

    _page_svgexport.add_line( false, "", _svgexport_insert_text_fallback,  "", _("Adds fallback options for non-SVG 2 renderers."), false);
    _page_svgexport.add_line( false, "", _svgexport_insert_mesh_polyfill,  "", _("Adds a JavaScript polyfill for rendering meshes in web browsers."), false);
    _page_svgexport.add_line( false, "", _svgexport_insert_hatch_polyfill,  "", _("Adds a JavaScript polyfill for rendering hatches in web browsers."), false);

    // SVG Export Options (SVG 2 -> SVG 1)
    _page_svgexport.add_group_header( _("SVG 2 to SVG 1.1"));

    _svgexport_remove_marker_auto_start_reverse.init( _("Use correct marker direction in SVG 1.1 renderers"),               "/options/svgexport/marker_autostartreverse", false);
    _svgexport_remove_marker_context_paint.init(      _("Use correct marker colors in SVG 1.1 renderers"), "/options/svgexport/marker_contextpaint",     false);

    _page_svgexport.add_line( false, "", _svgexport_remove_marker_auto_start_reverse, "", _("SVG 2 allows markers to automatically be reversed at the start of a path with 'auto_start_reverse'. This adds a rotated duplicate of the marker's definition."), false);
    _page_svgexport.add_line( false, "", _svgexport_remove_marker_context_paint,      "", _("SVG 2 allows markers to automatically match the stroke color by using 'context_paint' or 'context_fill'. This adjusts the markers own colors."),           false);

    this->AddPage(_page_svgexport, _("SVG export"), iter_io, PREFS_PAGE_IO_SVGEXPORT);


    // CMS options
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    /* TRANSLATORS: see http://www.newsandtech.com/issues/2004/03-04/pt/03-04_rendering.htm */
    Glib::ustring const intentLabels[] = {_("Perceptual"), _("Relative Colorimetric"),
                                          _("Saturation"), _("Absolute Colorimetric")};
    int const intentValues[] = {0, 1, 2, 3};

    _page_cms.add_group_header( _("Display adjustment"));

    Glib::ustring tmpStr;
    for (auto const &path : Inkscape::Colors::CMS::System::get().getDirectoryPaths()) {
        tmpStr += "\n";
        tmpStr += path.first;
    }

    gchar* profileTip = g_strdup_printf(_("The ICC profile to use to calibrate display output.\nSearched directories:%s"), tmpStr.c_str());
    _page_cms.add_line( true, _("User monitor profile:"), _cms_display_profile, "",
                        profileTip, false);
    g_free(profileTip);
    profileTip = nullptr;

    _cms_from_user.init( _("Use profile from user"), "/options/displayprofile/use_user_profile", false);
    _page_cms.add_line( true, "", _cms_from_user, "",
                        _("Use a user-specified ICC profile for monitor color correction. Warning: System wide color correction should be disabled."), false);

    _cms_intent.init("/options/displayprofile/intent", intentLabels, intentValues, 0);
    _page_cms.add_line( true, _("Display rendering intent:"), _cms_intent, "",
                        _("The rendering intent to use to calibrate display output"), false);

    _page_cms.add_group_header( _("Proofing"));

    _cms_softproof.init( _("Simulate output on screen"), "/options/softproof/enable", false);
    _page_cms.add_line( true, "", _cms_softproof, "",
                        _("Simulates output of target device"), false);

    _cms_gamutwarn.init( _("Mark out of gamut colors"), "/options/softproof/gamutwarn", false);
    _page_cms.add_line( true, "", _cms_gamutwarn, "",
                        _("Highlights colors that are out of gamut for the target device"), false);

    Glib::ustring colorStr = prefs->getString("/options/softproof/gamutcolor");

    Gdk::RGBA tmpColor( colorStr.empty() ? "#00ff00" : colorStr);
    _cms_gamutcolor.set_rgba( tmpColor );

    _page_cms.add_line( true, _("Out of gamut warning color:"), _cms_gamutcolor, "",
                        _("Selects the color used for out of gamut warning"), false);

    _page_cms.add_line( true, _("Device profile:"), _cms_proof_profile, "",
                        _("The ICC profile to use to simulate device output"), false);

    _cms_proof_intent.init("/options/softproof/intent", intentLabels, intentValues, 0);
    _page_cms.add_line( true, _("Device rendering intent:"), _cms_proof_intent, "",
                        _("The rendering intent to use to calibrate device output"), false);

    _cms_proof_blackpoint.init( _("Black point compensation"), "/options/softproof/bpc", false);
    _page_cms.add_line( true, "", _cms_proof_blackpoint, "",
                        _("Enables black point compensation"), false);

    {
        auto &cms_system = Inkscape::Colors::CMS::System::get();
        std::string current = prefs->getString( "/options/displayprofile/uri" );
        gint index = 0;
        _cms_display_profile.append(_("<none>"));
        index++;
        for (auto profile : cms_system.getDisplayProfiles()) {
            _cms_display_profile.append(profile->getName());
            if (profile->getPath() == current) {
                _cms_display_profile.set_active(index);
            }
            index++;
        }
        if ( current.empty() ) {
            _cms_display_profile.set_active(0);
        }

        current = prefs->getString("/options/softproof/uri");
        index = 0;
        for (auto profile : cms_system.getOutputProfiles()) {
            _cms_proof_profile.append(profile->getName());
            if (profile->getPath() == current) {
                _cms_proof_profile.set_active(index);
            }
            index++;
        }
    }

    _cms_gamutcolor.signal_color_set().connect(sigc::bind(&gamutColorChanged, &_cms_gamutcolor));

    _cms_display_profile.signal_changed().connect(sigc::bind(&profileComboChanged, &_cms_display_profile));
    _cms_proof_profile.signal_changed().connect(sigc::bind(&proofComboChanged, &_cms_proof_profile));

    this->AddPage(_page_cms, _("Color management"), iter_io, PREFS_PAGE_IO_CMS);

    // Autosave options
    _save_autosave_enable.init( _("Enable autosave"), "/options/autosave/enable", true);
    _page_autosave.add_line(false, "", _save_autosave_enable, "", _("Automatically save the current document(s) at a given interval, thus minimizing loss in case of a crash"), false);
    if (prefs->getString("/options/autosave/path").empty()) {
        // Set the default fallback "tmp dir" if autosave path is not set.
        prefs->setString("/options/autosave/path", Glib::build_filename(Glib::get_user_cache_dir(), "inkscape"));
    }

    _save_autosave_path_dir.init(prefs->getString("/options/autosave/path"), "/options/autosave/path",
                                 Glib::build_filename(Glib::get_user_cache_dir(), "inkscape"));
    _page_autosave.add_line(false, C_("Filesystem", "Autosave _directory:"), _save_autosave_path_dir, "",
                            _("The directory where autosaves will be written. This should be an absolute path (starts "
                              "with / on UNIX or a drive letter such as C: on Windows)."),
                            true);

    _save_autosave_interval.init("/options/autosave/interval", 1.0, 10800.0, 1.0, 10.0, 10.0, true, false);
    _page_autosave.add_line(false, _("_Interval (in minutes):"), _save_autosave_interval, "", _("Interval (in minutes) at which document will be autosaved"), false);
    _save_autosave_max.init("/options/autosave/max", 1.0, 10000.0, 1.0, 10.0, 10.0, true, false);
    _page_autosave.add_line(false, _("_Maximum number of autosaves:"), _save_autosave_max, "", _("Maximum number of autosaved files; use this to limit the storage space used"), false);

    // When changing the interval or enabling/disabling the autosave function,
    // update our running configuration
    _save_autosave_enable.changed_signal.connect([](bool) { Inkscape::AutoSave::restart(); });
    _save_autosave_interval.changed_signal.connect([](double) { Inkscape::AutoSave::restart(); });

    this->AddPage(_page_autosave, _("Autosave"), iter_io, PREFS_PAGE_IO_AUTOSAVE);

    // No Result
    _page_notfound.add_group_header(_("No matches were found, try another search!"));
}

void InkscapePreferences::initPageBehavior()
{
    Gtk::TreeModel::iterator iter_behavior = this->AddPage(_page_behavior, _("Behavior"), PREFS_PAGE_BEHAVIOR);

    _misc_simpl.init("/options/simplifythreshold/value", 0.0001, 1.0, 0.0001, 0.0010, 0.0010, false, false);
    _page_behavior.add_line( false, _("_Simplification threshold:"), _misc_simpl, "",
                           _("How strong is the Node tool's Simplify command by default. If you invoke this command several times in quick succession, it will act more and more aggressively; invoking it again after a pause restores the default threshold."), false);

    _undo_limit.init("", "/options/undo/limit", true);
    _page_behavior.add_line(false, _("Limit Undo Size:"), _undo_limit, "",
                         _("Enable the undo limit and remove old changes. Disabling this option will use more memory."));
    _undo_size.init("/options/undo/size", 1.0, 32000.0, 1.0, 1.0, 200.0, true, false);
    _page_behavior.add_line(false, _("Maximum _Undo Size:"), _undo_size, "",
                         _("How large the undo log will be allowed to get before being trimmed to free memory."), false );
    _undo_limit.changed_signal.connect(sigc::mem_fun(_undo_size, &Gtk::Widget::set_sensitive));
    _undo_size.set_sensitive(_undo_limit.get_active());

    _markers_color_stock.init ( _("Color stock markers the same color as object"), "/options/markers/colorStockMarkers", true);
    _markers_color_custom.init ( _("Color custom markers the same color as object"), "/options/markers/colorCustomMarkers", false);
    _markers_color_update.init ( _("Update marker color when object color changes"), "/options/markers/colorUpdateMarkers", true);

    // Selecting options
    _sel_all.init ( _("Select in all layers"), "/options/kbselection/inlayer", PREFS_SELECTION_ALL, false, nullptr);
    _sel_current.init ( _("Select only within current layer"), "/options/kbselection/inlayer", PREFS_SELECTION_LAYER, true, &_sel_all);
    _sel_recursive.init ( _("Select in current layer and sublayers"), "/options/kbselection/inlayer", PREFS_SELECTION_LAYER_RECURSIVE, false, &_sel_all);
    _sel_hidden.init ( _("Ignore hidden objects and layers"), "/options/kbselection/onlyvisible", true);
    _sel_locked.init ( _("Ignore locked objects and layers"), "/options/kbselection/onlysensitive", true);
    _sel_inlayer_same.init ( _("Select same behaves like select all"), "/options/selection/samelikeall", false);

    _sel_layer_deselects.init ( _("Deselect upon layer change"), "/options/selection/layerdeselect", true);

    _sel_touch_topmost_only.init ( _("Select the topmost items only when in touch selection mode"), "/options/selection/touchsel_topmost_only", true);
    _sel_zero_opacity.init(_("Select transparent objects, strokes, and fills"), "/options/selection/zeroopacity", false);

    _page_select.add_line( false, "", _sel_layer_deselects, "",
                           _("Uncheck this to be able to keep the current objects selected when the current layer changes"));
    _page_select.add_line(
        false, "", _sel_zero_opacity, "",
        _("Check to make objects, strokes, and fills which are completely transparent selectable even if not in outline mode"));

    _page_select.add_line( false, "", _sel_touch_topmost_only, "",
                           _("In touch selection mode, if multiple items overlap at a point, select only the topmost item"));

    _page_select.add_group_header( _("Ctrl+A, Tab, Shift+Tab"));
    _page_select.add_line( true, "", _sel_all, "",
                           _("Make keyboard selection commands work on objects in all layers"));
    _page_select.add_line( true, "", _sel_current, "",
                           _("Make keyboard selection commands work on objects in current layer only"));
    _page_select.add_line( true, "", _sel_recursive, "",
                           _("Make keyboard selection commands work on objects in current layer and all its sublayers"));
    _page_select.add_line( true, "", _sel_hidden, "",
                           _("Uncheck this to be able to select objects that are hidden (either by themselves or by being in a hidden layer)"));
    _page_select.add_line( true, "", _sel_locked, "",
                           _("Uncheck this to be able to select objects that are locked (either by themselves or by being in a locked layer)"));
    _page_select.add_line( true, "", _sel_inlayer_same, "",
                           _("Check this to make the 'select same' functions work like the select all functions, restricting to current layer only."));

    _sel_cycle.init ( _("Wrap when cycling objects in z-order"), "/options/selection/cycleWrap", true);

    _page_select.add_group_header( _("Alt+Scroll Wheel"));
    _page_select.add_line( true, "", _sel_cycle, "",
                           _("Wrap around at start and end when cycling objects in z-order"));

    auto const paste_above_selected = Gtk::make_managed<UI::Widget::PrefCheckButton>();
    paste_above_selected->init(_("Paste above selection instead of layer-top"), "/options/paste/aboveselected", true);
    _page_select.add_line(false, "", *paste_above_selected, "",
                          _("If checked, pasted items and imported documents will be placed immediately above the "
                            "current selection (z-order). Otherwise, insertion happens on top of all objects in the current layer."));

    this->AddPage(_page_select, _("Selecting"), iter_behavior, PREFS_PAGE_BEHAVIOR_SELECTING);

    // Transforms options
    _trans_scale_stroke.init ( _("Scale stroke width"), "/options/transform/stroke", true);
    _trans_scale_corner.init ( _("Scale rounded corners in rectangles"), "/options/transform/rectcorners", false);
    _trans_gradient.init ( _("Transform gradients"), "/options/transform/gradient", true);
    _trans_pattern.init ( _("Transform patterns"), "/options/transform/pattern", false);
    _trans_dash_scale.init(_("Scale dashes with stroke"), "/options/dash/scale", true);
    _trans_optimized.init ( _("Optimized"), "/options/preservetransform/value", 0, true, nullptr);
    _trans_preserved.init ( _("Preserved"), "/options/preservetransform/value", 1, false, &_trans_optimized);

    _page_transforms.add_line( false, "", _trans_scale_stroke, "",
                               _("When scaling objects, scale the stroke width by the same proportion"));
    _page_transforms.add_line( false, "", _trans_scale_corner, "",
                               _("When scaling rectangles, scale the radii of rounded corners"));
    _page_transforms.add_line( false, "", _trans_gradient, "",
                               _("Move gradients (in fill or stroke) along with the objects"));
    _page_transforms.add_line( false, "", _trans_pattern, "",
                               _("Move patterns (in fill or stroke) along with the objects"));
    _page_transforms.add_line(false, "", _trans_dash_scale, "", _("When changing stroke width, scale dash array"));
    _page_transforms.add_group_header( _("Store transformation"));
    _page_transforms.add_line( true, "", _trans_optimized, "",
                               _("If possible, apply transformation to objects without adding a transform= attribute"));
    _page_transforms.add_line( true, "", _trans_preserved, "",
                               _("Always store transformation as a transform= attribute on objects"));
    
    this->AddPage(_page_transforms, _("Transforms"), iter_behavior, PREFS_PAGE_BEHAVIOR_TRANSFORMS);

    // Scrolling options
    _scroll_wheel.init ( "/options/wheelscroll/value", 0.0, 1000.0, 1.0, 1.0, 40.0, true, false);
    _page_scrolling.add_line( false, _("Mouse _wheel scrolls by:"), _scroll_wheel, _("pixels"),
                           _("One mouse wheel notch scrolls by this distance in screen pixels (horizontally with Shift)"), false);
    _page_scrolling.add_group_header( _("Ctrl+arrows"));
    _scroll_arrow_px.init ( "/options/keyscroll/value", 0.0, 1000.0, 1.0, 1.0, 10.0, true, false);
    _page_scrolling.add_line( true, _("Sc_roll by:"), _scroll_arrow_px, _("pixels"),
                           _("Pressing Ctrl+arrow key scrolls by this distance (in screen pixels)"), false);
    _scroll_arrow_acc.init ( "/options/scrollingacceleration/value", 0.0, 5.0, 0.01, 1.0, 0.35, false, false);
    _page_scrolling.add_line( true, _("_Acceleration:"), _scroll_arrow_acc, "",
                           _("Pressing and holding Ctrl+arrow will gradually speed up scrolling (0 for no acceleration)"), false);
    _page_scrolling.add_group_header( _("Autoscrolling"));
    _scroll_auto_speed.init ( "/options/autoscrollspeed/value", 0.0, 5.0, 0.01, 1.0, 0.7, false, false);
    _page_scrolling.add_line( true, _("_Speed:"), _scroll_auto_speed, "",
                           _("How fast the canvas autoscrolls when you drag beyond canvas edge (0 to turn autoscroll off)"), false);
    _scroll_auto_thres.init ( "/options/autoscrolldistance/value", -600.0, 600.0, 1.0, 1.0, -10.0, true, false);
    _page_scrolling.add_line( true, _("_Threshold:"), _scroll_auto_thres, _("pixels"),
                           _("How far (in screen pixels) you need to be from the canvas edge to trigger autoscroll; positive is outside the canvas, negative is within the canvas"), false);
    _scroll_space.init ( _("Mouse move pans when Space is pressed"), "/options/spacebarpans/value", true);
    _page_scrolling.add_line( true, "", _scroll_space, "",
                            _("When on, pressing and holding Space and dragging pans canvas"));
    this->AddPage(_page_scrolling, _("Scrolling"), iter_behavior, PREFS_PAGE_BEHAVIOR_SCROLLING);

    // Snapping options
    _page_snapping.add_group_header( _("Snap indicator"));

    _snap_indicator.init( _("Enable snap indicator"), "/options/snapindicator/value", true);
    _page_snapping.add_line( true, "", _snap_indicator, "",
                             _("After snapping, a symbol is drawn at the point that has snapped"));
    _snap_indicator.changed_signal.connect( sigc::mem_fun(_snap_persistence, &Gtk::Widget::set_sensitive) );

    _snap_indicator_distance.init( _("Show snap distance in case of alignment or distribution snap"), "/options/snapindicatordistance/value", false);
    _page_snapping.add_line( true, "", _snap_indicator_distance, "",
                             _("Show snap distance in case of alignment or distribution snap"));

    _snap_persistence.init("/options/snapindicatorpersistence/value", 0.1, 10, 0.1, 1, 2, 1);
    _page_snapping.add_line( true, _("Snap indicator persistence (in seconds):"), _snap_persistence, "",
                             _("Controls how long the snap indicator message will be shown, before it disappears"), true);


    _page_snapping.add_group_header( _("What should snap"));

    _snap_closest_only.init( _("Only snap the node closest to the pointer"), "/options/snapclosestonly/value", false);
    _page_snapping.add_line( true, "", _snap_closest_only, "",
                             _("Only try to snap the node that is initially closest to the mouse pointer"));

    _snap_mouse_pointer.init( _("Snap the mouse pointer when dragging a constrained knot"), "/options/snapmousepointer/value", false);
    _page_snapping.add_line( true, "", _snap_mouse_pointer, "",
                             _("When dragging a knot along a constraint line, then snap the position of the mouse pointer instead of snapping the projection of the knot onto the constraint line"));

    _snap_weight.init("/options/snapweight/value", 0, 1, 0.1, 0.2, 0.5, 1);
    _page_snapping.add_line( true, _("_Weight factor:"), _snap_weight, "",
                             _("When multiple snap solutions are found, then Inkscape can either prefer the closest transformation (when set to 0), or prefer the node that was initially the closest to the pointer (when set to 1)"), true);

    _page_snapping.add_group_header( _("Delayed snap"));

    _snap_delay.init("/options/snapdelay/value", 0, 1, 0.1, 0.2, 0, 1);
    _page_snapping.add_line( true, _("Delay (in seconds):"), _snap_delay, "",
                             _("Postpone snapping as long as the mouse is moving, and then wait an additional fraction of a second. This additional delay is specified here. When set to zero or to a very small number, snapping will be immediate."), true);

    _page_snapping.add_group_header( _("Restrict Snap Targets"));

    _snap_always_grid.init(_("Always snap to grids"), "/options/snap/grid/always", false);
    _page_snapping.add_line(true, "", _snap_always_grid, "", _("When a grid is visible, and snapping to grids is active, other snap targets will be ignored, unless explicitly allowed below."));

    _snap_always_guide.init(_("Always snap to guides"), "/options/snap/guide/always", false);
    _page_snapping.add_line(true, "", _snap_always_guide, "", _("When there are any guidelines in the current viewport, and snapping to guides is active, other snap targets will be ignored, unless explicitly allowed below."));

    _page_snapping.add_group_header( _("While Always Snapping to Grid/Guides"));

    _snap_always_object.init(_("Allow snapping to objects"), "/options/snap/object/always", false);
    _page_snapping.add_line(true, "", _snap_always_object, "", _("Allow snapping to objects while 'Always snap to grids / guides' is active, if an object is closer."));

    _snap_always_align.init(_("Allow alignment snapping"), "/options/snap/alignment/always", false);
    _page_snapping.add_line(true, "", _snap_always_align, "", _("Allow alignment snapping while 'Always snap to grids / guides' is active, if an alignment snap target is closer."));

    _snap_always_dist.init(_("Allow distribution snapping"), "/options/snap/distribution/always", false);
    _page_snapping.add_line(true, "", _snap_always_dist, "", _("Allow distribution snapping while 'Always snap to grids / guides' is active, if a distribution snap target is closer."));

    this->AddPage(_page_snapping, _("Snapping"), iter_behavior, PREFS_PAGE_BEHAVIOR_SNAPPING);

    // Steps options

    //nudgedistance is limited to 1000 in select-context.cpp: use the same limit here
    _steps_arrow.init ( "/options/nudgedistance/value", 0.0, 1000.0, 0.01, 2.0, UNIT_TYPE_LINEAR, "px");
    _page_steps.add_line( false, _("_Arrow keys move by:"), _steps_arrow, "",
                          _("Pressing an arrow key moves selected object(s) or node(s) by this distance"), false);

    //defaultscale is limited to 1000 in select-context.cpp: use the same limit here
    _steps_scale.init ( "/options/defaultscale/value", 0.0, 1000.0, 0.01, 2.0, UNIT_TYPE_LINEAR, "px");
    _page_steps.add_line( false, _("&gt; and &lt; _scale by:"), _steps_scale, "",
                          _("Pressing > or < scales selection up or down by this increment"), false);

    _steps_inset.init ( "/options/defaultoffsetwidth/value", 0.0, 3000.0, 0.01, 2.0, UNIT_TYPE_LINEAR, "px");
    _page_steps.add_line( false, _("_Inset/Outset by:"), _steps_inset, "",
                          _("Inset and Outset commands displace the path by this distance"), false);

    _steps_compass.init ( _("Compass-like display of angles"), "/options/compassangledisplay/value", true);
    _page_steps.add_line( false, "", _steps_compass, "",
                            _("When on, angles are displayed with 0 at north, 0 to 360 range, positive clockwise; otherwise with 0 at east, -180 to 180 range, positive counterclockwise"));

    {
        Glib::ustring const labels[] = {"90", "60", "45", "36", "30", "22.5", "18", "15", "12", "10",
                                        "7.5", "6", "5", "3", "2", "1", "0.5", C_("Rotation angle", "None")};
        int const values[] = {2, 3, 4, 5, 6, 8, 10, 12, 15, 18, 24, 30, 36, 60, 90, 180, 360, 0};
        _steps_rot_snap.init("/options/rotationsnapsperpi/value", labels, values, 12);
    }
    _steps_rot_snap.set_size_request(_sb_width);
    _page_steps.add_line( false, _("_Rotation snaps every:"), _steps_rot_snap, _("degrees"),
                           _("Rotating with Ctrl pressed snaps every that much degrees; also, pressing [ or ] rotates by this amount"), false);

    _steps_rot_relative.init ( _("Relative snapping of guideline angles"), "/options/relativeguiderotationsnap/value", false);
    _page_steps.add_line( false, "", _steps_rot_relative, "",
                            _("When on, the snap angles when rotating a guideline will be relative to the original angle"));

    _steps_zoom.init ( "/options/zoomincrement/value", 101.0, 500.0, 1.0, 1.0, M_SQRT2, true, true);
    _page_steps.add_line( false, _("_Zoom in/out by:"), _steps_zoom, _("%"),
                          _("Zoom tool click, +/- keys, and middle click zoom in and out by this multiplier"), false);

    _middle_mouse_zoom.init ( _("Zoom with middle mouse click"), "/options/middlemousezoom/value", true);
    _page_steps.add_line( true, "", _middle_mouse_zoom, "",
                            _("When activated, clicking the middle mouse button (usually the mouse wheel) zooms."));

    _page_steps.add_group_header( _("Canvas rotation"));

    _steps_rotate.init ( "/options/rotateincrement/value", 1, 90, 1.0, 5.0, 15, false, false);
    _page_steps.add_line( false, _("_Rotate canvas by:"), _steps_rotate, _("degrees"),
                          _("Rotate canvas clockwise and counter-clockwise by this amount."), false);

    _move_rotated.init ( _("Arrow keys move object relative to screen"), "/options/moverotated/value", true);
    _page_steps.add_line( false, "", _move_rotated, "",
                            _("When on, arrow keys move objects relative to screen. When the canvas is rotated, the selection will then still be moved horizontally and vertically relative to the screen, not to the rotated document."));

    this->AddPage(_page_steps, _("Steps"), iter_behavior, PREFS_PAGE_BEHAVIOR_STEPS);

    // Clones options
    _clone_option_parallel.init ( _("Move in parallel"), "/options/clonecompensation/value",
                                  SP_CLONE_COMPENSATION_PARALLEL, true, nullptr);
    _clone_option_stay.init ( _("Stay unmoved"), "/options/clonecompensation/value",
                                  SP_CLONE_COMPENSATION_UNMOVED, false, &_clone_option_parallel);
    _clone_option_transform.init ( _("Move according to transform"), "/options/clonecompensation/value",
                                  SP_CLONE_COMPENSATION_NONE, false, &_clone_option_parallel);
    _clone_option_unlink.init ( _("Are unlinked"), "/options/cloneorphans/value",
                                  SP_CLONE_ORPHANS_UNLINK, true, nullptr);
    _clone_option_delete.init ( _("Are deleted"), "/options/cloneorphans/value",
                                  SP_CLONE_ORPHANS_DELETE, false, &_clone_option_unlink);
    _clone_option_keep.init ( _("Become orphans"), "/options/cloneorphans/value",
                                  SP_CLONE_ORPHANS_KEEP, false, &_clone_option_unlink);

    _page_clones.add_group_header( _("Moving original: clones and linked offsets"));
    _page_clones.add_line(true, "", _clone_option_parallel, "",
                           _("Clones are translated by the same vector as their original"));
    _page_clones.add_line(true, "", _clone_option_stay, "",
                           _("Clones preserve their positions when their original is moved"));
    _page_clones.add_line(true, "", _clone_option_transform, "",
                           _("Each clone moves according to the value of its transform= attribute; for example, a rotated clone will move in a different direction than its original"));
    _page_clones.add_group_header( _("Deleting original: clones"));
    _page_clones.add_line(true, "", _clone_option_unlink, "",
                           _("Orphaned clones are converted to regular objects"));
    _page_clones.add_line(true, "", _clone_option_delete, "",
                           _("Orphaned clones are deleted along with their original"));
    _page_clones.add_line(true, "", _clone_option_keep, "",
                           _("Orphaned clones are not modified"));

    _page_clones.add_group_header( _("Duplicating original+clones/linked offset"));

    _clone_relink_on_duplicate.init ( _("Relink duplicated clones"), "/options/relinkclonesonduplicate/value", false);
    _page_clones.add_line(true, "", _clone_relink_on_duplicate, "",
                        _("When duplicating a selection containing both a clone and its original (possibly in groups), relink the duplicated clone to the duplicated original instead of the old original"));

    _page_clones.add_group_header( _("Unlinking clones"));
    _clone_to_curves.init ( _("Path operations unlink clones"), "/options/pathoperationsunlink/value", true);
    _page_clones.add_line(true, "", _clone_to_curves, "",
                        _("The following path operations will unlink clones: Stroke to path, Object to path, Boolean operations, Combine, Break apart"));
    _clone_ignore_to_curves.init ( _("'Object to Path' only unlinks (keeps LPEs, shapes)"), "/options/clonestocurvesjustunlink/value", true);
    _page_clones.add_line(true, "", _clone_ignore_to_curves, "",
                        _("'Object to path' only unlinks clones when they are converted to paths, but preserves any LPEs and shapes within the clones."));
    //TRANSLATORS: Heading for the Inkscape Preferences "Clones" Page
    this->AddPage(_page_clones, _("Clones"), iter_behavior, PREFS_PAGE_BEHAVIOR_CLONES);

    // Clip paths and masks options
    _mask_mask_on_top.init ( _("When applying, use the topmost selected object as clippath/mask"), "/options/maskobject/topmost", true);
    _page_mask.add_line(false, "", _mask_mask_on_top, "",
                        _("Uncheck this to use the bottom selected object as the clipping path or mask"));
    _mask_mask_on_ungroup.init ( _("When ungrouping, clips/masks are preserved in children"), "/options/maskobject/maskonungroup", true);
    _page_mask.add_line(false, "", _mask_mask_on_ungroup, "",
                        _("Uncheck this to remove clip/mask on ungroup"));
    _mask_mask_remove.init ( _("Remove clippath/mask object after applying"), "/options/maskobject/remove", true);
    _page_mask.add_line(false, "", _mask_mask_remove, "",
                        _("After applying, remove the object used as the clipping path or mask from the drawing"));

    _page_mask.add_group_header( _("Before applying"));

    _mask_grouping_none.init( _("Do not group clipped/masked objects"), "/options/maskobject/grouping", PREFS_MASKOBJECT_GROUPING_NONE, true, nullptr);
    _mask_grouping_separate.init( _("Put every clipped/masked object in its own group"), "/options/maskobject/grouping", PREFS_MASKOBJECT_GROUPING_SEPARATE, false, &_mask_grouping_none);
    _mask_grouping_all.init( _("Put all clipped/masked objects into one group"), "/options/maskobject/grouping", PREFS_MASKOBJECT_GROUPING_ALL, false, &_mask_grouping_none);

    _page_mask.add_line(true, "", _mask_grouping_none, "",
                        _("Apply clippath/mask to every object"));

    _page_mask.add_line(true, "", _mask_grouping_separate, "",
                        _("Apply clippath/mask to groups containing single object"));

    _page_mask.add_line(true, "", _mask_grouping_all, "",
                        _("Apply clippath/mask to group containing all objects"));

    _page_mask.add_group_header( _("After releasing"));

    _mask_ungrouping.init ( _("Ungroup automatically created groups"), "/options/maskobject/ungrouping", true);
    _page_mask.add_line(true, "", _mask_ungrouping, "",
                        _("Ungroup groups created when setting clip/mask"));

    this->AddPage(_page_mask, _("Clippaths and masks"), iter_behavior, PREFS_PAGE_BEHAVIOR_MASKS);

    // Markers options
    _page_markers.add_group_header( _("Stroke Style Markers"));
    _page_markers.add_line( true, "", _markers_color_stock, "",
                           _("Stroke color same as object, fill color either object fill color or marker fill color"));
    _page_markers.add_line( true, "", _markers_color_custom, "",
                           _("Stroke color same as object, fill color either object fill color or marker fill color"));
    _page_markers.add_line( true, "", _markers_color_update, "",
                           _("Update marker color when object color changes"));

    this->AddPage(_page_markers, _("Markers"), iter_behavior, PREFS_PAGE_BEHAVIOR_MARKERS);

    // Clipboard options
    _clipboard_style_computed.init(_("Copy computed style"), "/options/copycomputedstyle/value", 1, true, nullptr);
    _clipboard_style_verbatim.init(_("Copy class and style attributes verbatim"), "/options/copycomputedstyle/value", 0,
                                   false, &_clipboard_style_computed);

    _page_clipboard.add_group_header(_("Copying objects to the clipboard"));
    _page_clipboard.add_line(true, "", _clipboard_style_computed, "",
                             _("The object's 'style' attribute will be set to the computed style, "
                               "preserving the object's appearance as in previous Inkscape versions"));
    _page_clipboard.add_line(
        true, "", _clipboard_style_verbatim, "",
        _("The object's 'style' and 'class' values will be copied verbatim, and will replace those of the target object when using 'Paste style'"));

    this->AddPage(_page_clipboard, _("Clipboard"), iter_behavior, PREFS_PAGE_BEHAVIOR_CLIPBOARD);

    // Document cleanup options
    _page_cleanup.add_group_header( _("Document cleanup"));
    _cleanup_swatches.init ( _("Remove unused swatches when doing a document cleanup"), "/options/cleanupswatches/value", false); // text label
    _page_cleanup.add_line( true, "", _cleanup_swatches, "",
                           _("Remove unused swatches when doing a document cleanup")); // tooltip
    this->AddPage(_page_cleanup, _("Cleanup"), iter_behavior, PREFS_PAGE_BEHAVIOR_CLEANUP);
    _page_lpe.add_group_header( _("General"));
    _lpe_show_experimental.init ( _("Show experimental effects"), "/dialogs/livepatheffect/showexperimental", false); // text label
    _page_lpe.add_line( true, "", _lpe_show_experimental, "",
                            _("Show experimental effects")); // tooltip
    _page_lpe.add_group_header( _("Tiling"));
    _lpe_copy_mirroricons.init ( _("Add advanced tiling options"), "/live_effects/copy/mirroricons", true); // text label
    _page_lpe.add_line( true, "", _lpe_copy_mirroricons, "",
                           _("Enables using 16 advanced mirror options between the copies (so there can be copies that are mirrored differently between the rows and the columns) for Tiling LPE")); // tooltip
    this->AddPage(_page_lpe, _("Live Path Effects (LPE)"), iter_behavior, PREFS_PAGE_BEHAVIOR_LPE);
}

void InkscapePreferences::initPageRendering()
{
    // render threads
    _filter_multi_threaded.init("/options/threading/numthreads", 0.0, 32.0, 1.0, 2.0, 0.0, true, false);
    _page_rendering.add_line(false, _("Number of _Threads:"), _filter_multi_threaded, "", _("Configure number of threads to use when rendering. The default value of zero means choose automatically."), false);

    // rendering cache
    _rendering_cache_size.init("/options/renderingcache/size", 0.0, 4096.0, 1.0, 32.0, 64.0, true, false);
    _page_rendering.add_line( false, _("Rendering _cache size:"), _rendering_cache_size, C_("mebibyte (2^20 bytes) abbreviation","MiB"), _("Set the amount of memory per document which can be used to store rendered parts of the drawing for later reuse; set to zero to disable caching"), false);

    // rendering x-ray radius
    _rendering_xray_radius.init("/options/rendering/xray-radius", 1.0, 1500.0, 1.0, 100.0, 100.0, true, false);
    _page_rendering.add_line( false, _("X-ray radius:"), _rendering_xray_radius, "", _("Radius of the circular area around the mouse cursor in X-ray mode"), false);

    // rendering outline overlay opacity
    _rendering_outline_overlay_opacity.init("/options/rendering/outline-overlay-opacity", 0.0, 100.0, 1.0, 5.0, 50.0, true, false);
    _page_rendering.add_line( false, _("Outline overlay opacity:"), _rendering_outline_overlay_opacity, _("%"), _("Opacity of the overlay in outline overlay view mode"), false);

    // update strategy
    {
        constexpr int values[] = { 1, 2, 3 };
        Glib::ustring const labels[] = { _("Responsive"), _("Full redraw"), _("Multiscale") };
        _canvas_update_strategy.init("/options/rendering/update_strategy", labels, values, 3);
        _page_rendering.add_line(false, _("Update strategy:"), _canvas_update_strategy, "", _("How to update continually changing content when it can't be redrawn fast enough"), false);
    }

    // opengl
    _canvas_request_opengl.init(_("Enable OpenGL"), "/options/rendering/request_opengl", false);
    _page_rendering.add_line(false, "", _canvas_request_opengl, "", _("Request that the canvas should be painted with OpenGL rather than Cairo. If OpenGL is unsupported, it will fall back to Cairo."), false);

    // blur quality
    _blur_quality_best.init ( _("Best quality (slowest)"), "/options/blurquality/value",
                                  BLUR_QUALITY_BEST, false, nullptr);
    _blur_quality_better.init ( _("Better quality (slower)"), "/options/blurquality/value",
                                  BLUR_QUALITY_BETTER, false, &_blur_quality_best);
    _blur_quality_normal.init ( _("Average quality"), "/options/blurquality/value",
                                  BLUR_QUALITY_NORMAL, true, &_blur_quality_best);
    _blur_quality_worse.init ( _("Lower quality (faster)"), "/options/blurquality/value",
                                  BLUR_QUALITY_WORSE, false, &_blur_quality_best);
    _blur_quality_worst.init ( _("Lowest quality (fastest)"), "/options/blurquality/value",
                                  BLUR_QUALITY_WORST, false, &_blur_quality_best);

    _page_rendering.add_group_header( _("Gaussian blur quality for display"));
    _page_rendering.add_line( true, "", _blur_quality_best, "",
                           _("Best quality, but display may be very slow at high zooms (bitmap export always uses best quality)"));
    _page_rendering.add_line( true, "", _blur_quality_better, "",
                           _("Better quality, but slower display"));
    _page_rendering.add_line( true, "", _blur_quality_normal, "",
                           _("Average quality, acceptable display speed"));
    _page_rendering.add_line( true, "", _blur_quality_worse, "",
                           _("Lower quality (some artifacts), but display is faster"));
    _page_rendering.add_line( true, "", _blur_quality_worst, "",
                           _("Lowest quality (considerable artifacts), but display is fastest"));

    // filter quality
    _filter_quality_best.init ( _("Best quality (slowest)"), "/options/filterquality/value",
                                  Inkscape::Filters::FILTER_QUALITY_BEST, false, nullptr);
    _filter_quality_better.init ( _("Better quality (slower)"), "/options/filterquality/value",
                                  Inkscape::Filters::FILTER_QUALITY_BETTER, false, &_filter_quality_best);
    _filter_quality_normal.init ( _("Average quality"), "/options/filterquality/value",
                                  Inkscape::Filters::FILTER_QUALITY_NORMAL, true, &_filter_quality_best);
    _filter_quality_worse.init ( _("Lower quality (faster)"), "/options/filterquality/value",
                                  Inkscape::Filters::FILTER_QUALITY_WORSE, false, &_filter_quality_best);
    _filter_quality_worst.init ( _("Lowest quality (fastest)"), "/options/filterquality/value",
                                  Inkscape::Filters::FILTER_QUALITY_WORST, false, &_filter_quality_best);

    _page_rendering.add_group_header( _("Filter effects quality for display"));
    _page_rendering.add_line( true, "", _filter_quality_best, "",
                           _("Best quality, but display may be very slow at high zooms (bitmap export always uses best quality)"));
    _page_rendering.add_line( true, "", _filter_quality_better, "",
                           _("Better quality, but slower display"));
    _page_rendering.add_line( true, "", _filter_quality_normal, "",
                           _("Average quality, acceptable display speed"));
    _page_rendering.add_line( true, "", _filter_quality_worse, "",
                           _("Lower quality (some artifacts), but display is faster"));
    _page_rendering.add_line( true, "", _filter_quality_worst, "",
                           _("Lowest quality (considerable artifacts), but display is fastest"));

#if CAIRO_VERSION >= CAIRO_VERSION_ENCODE(1, 18, 0)
    _cairo_dithering.init(_("Use dithering"), "/options/dithering/value", true);
    _page_rendering.add_line(false, "", _cairo_dithering, "",  _("Makes gradients smoother. This can significantly impact the size of generated PNG files."));
#endif

    auto const grid = Gtk::make_managed<Gtk::Grid>();
    grid->set_margin(12);
    grid->set_orientation(Gtk::Orientation::VERTICAL);
    grid->set_column_spacing(12);
    grid->set_row_spacing(6);

    auto const revealer = Gtk::make_managed<Gtk::Revealer>();
    revealer->set_child(*grid);
    revealer->set_reveal_child(Inkscape::Preferences::get()->getBool("/options/rendering/devmode"));

    _canvas_developer_mode_enabled.init(_("Enable developer mode"), "/options/rendering/devmode", false);
    _canvas_developer_mode_enabled.signal_toggled().connect([revealer, this] { revealer->set_reveal_child(_canvas_developer_mode_enabled.get_active()); });

    _page_rendering.add_group_header(_("Developer mode"));
    _page_rendering.add_line(true, "", _canvas_developer_mode_enabled, "", _("Enable additional debugging options"), false);
    _page_rendering.attach_next_to(*revealer, Gtk::PositionType::BOTTOM);

    auto add_devmode_line = [&] (Glib::ustring const &label, Gtk::Widget &widget, Glib::ustring const &suffix, Glib::ustring const &tip) {
        widget.set_tooltip_text(tip);

        auto const hb = Gtk::make_managed<Gtk::Box>();
        hb->set_spacing(12);
        hb->set_hexpand(true);
        UI::pack_start(*hb, widget, false, false);
        hb->set_valign(Gtk::Align::CENTER);

        auto const label_widget = Gtk::make_managed<Gtk::Label>(label, Gtk::Align::START, Gtk::Align::CENTER, true);
        label_widget->set_mnemonic_widget(widget);
        label_widget->set_markup(label_widget->get_text());
        label_widget->set_margin_start(12);

        label_widget->set_valign(Gtk::Align::CENTER);
        grid->attach_next_to(*label_widget, Gtk::PositionType::BOTTOM);
        grid->attach_next_to(*hb, *label_widget, Gtk::PositionType::RIGHT, 1, 1);

        if (!suffix.empty()) {
            auto const suffix_widget = Gtk::make_managed<Gtk::Label>(suffix, Gtk::Align::START, Gtk::Align::CENTER, true);
            suffix_widget->set_markup(suffix_widget->get_text());
            UI::pack_start(*hb, *suffix_widget, false, false);
        }
    };

    auto add_devmode_group_header = [&] (Glib::ustring const &name) {
        auto const label_widget = Gtk::make_managed<Gtk::Label>(Glib::ustring(/*"<span size='large'>*/"<b>") + name + Glib::ustring("</b>"/*</span>"*/) , Gtk::Align::START , Gtk::Align::CENTER, true);
        label_widget->set_use_markup(true);
        label_widget->set_valign(Gtk::Align::CENTER);
        grid->attach_next_to(*label_widget, Gtk::PositionType::BOTTOM);
    };

    //TRANSLATORS: The following are options for fine-tuning rendering, meant to be used by developers, 
    //find more explanations at https://gitlab.com/inkscape/inbox/-/issues/6544#note_886540227
    add_devmode_group_header(_("Low-level tuning options"));
    _canvas_tile_size.init("/options/rendering/tile_size", 1.0, 10000.0, 1.0, 0.0, 300.0, true, false);
    add_devmode_line(_("Tile size"), _canvas_tile_size, "", _("Halve rendering tile rectangles until their largest dimension is this small"));
    _canvas_render_time_limit.init("/options/rendering/render_time_limit", 1.0, 5000.0, 1.0, 0.0, 80.0, true, false);
    add_devmode_line(_("Render time limit"), _canvas_render_time_limit, C_("millisecond abbreviation", "ms"), _("The maximum time allowed for a rendering time slice"));
    {
        constexpr int values[] = { 1, 2, 3, 4 };
        Glib::ustring const labels[] = { _("Auto"), _("Persistent"), _("Asynchronous"), _("Synchronous") };
        _canvas_pixelstreamer_method.init("/options/rendering/pixelstreamer_method", labels, values, 1);
        add_devmode_line(_("Pixel streaming method"), _canvas_pixelstreamer_method, "", _("Change the method used for streaming pixel data to the GPU. The default is Auto, which picks the best method available at runtime. As for the other options, higher up is better."));
    }
    _canvas_padding.init("/options/rendering/padding", 0.0, 1000.0, 1.0, 0.0, 350.0, true, false);
    add_devmode_line(_("Buffer padding"), _canvas_padding, C_("pixel abbreviation", "px"), _("Use buffers bigger than the window by this amount"));
    _canvas_prerender.init("/options/rendering/prerender", 0.0, 1000.0, 1.0, 0.0, 100.0, true, false);
    add_devmode_line(_("Prerender margin"), _canvas_prerender, "", _("Pre-render a margin around the visible region."));
    _canvas_preempt.init("/options/rendering/preempt", 0.0, 1000.0, 1.0, 0.0, 250.0, true, false);
    add_devmode_line(_("Preempt size"), _canvas_preempt, "", _("Prevent thin tiles at the rendering edge by making them at least this size."));
    _canvas_coarsener_min_size.init("/options/rendering/coarsener_min_size", 0.0, 1000.0, 1.0, 0.0, 200.0, true, false);
    add_devmode_line(_("Min size for coarsener algorithm"), _canvas_coarsener_min_size, C_("pixel abbreviation", "px"), _("Coarsener algorithm only processes rectangles smaller/thinner than this."));
    _canvas_coarsener_glue_size.init("/options/rendering/coarsener_glue_size", 0.0, 1000.0, 1.0, 0.0, 80.0, true, false);
    add_devmode_line(_("Glue size for coarsener algorithm"), _canvas_coarsener_glue_size, C_("pixel abbreviation", "px"), _("Coarsener algorithm absorbs nearby rectangles within this distance."));
    _canvas_coarsener_min_fullness.init("/options/rendering/coarsener_min_fullness", 0.0, 1.0, 0.0, 0.0, 0.3, false, false);
    add_devmode_line(_("Min fullness for coarsener algorithm"), _canvas_coarsener_min_fullness, "", _("Refuse coarsening algorithm's attempt if the result would be more empty than this."));

    add_devmode_group_header(_("Debugging, profiling and experiments"));
    _canvas_debug_framecheck.init("", "/options/rendering/debug_framecheck", false);
    add_devmode_line(_("Framecheck"), _canvas_debug_framecheck, "", _("Print profiling data of selected operations to a file"));
    _canvas_debug_logging.init("", "/options/rendering/debug_logging", false);
    add_devmode_line(_("Logging"), _canvas_debug_logging, "", _("Log certain events to the console"));
    _canvas_debug_delay_redraw.init("", "/options/rendering/debug_delay_redraw", false);
    add_devmode_line(_("Delay redraw"), _canvas_debug_delay_redraw, "", _("Introduce a fixed delay for each tile"));
    _canvas_debug_delay_redraw_time.init("/options/rendering/debug_delay_redraw_time", 0.0, 1000000.0, 1.0, 0.0, 50.0, true, false);
    add_devmode_line(_("Delay redraw time"), _canvas_debug_delay_redraw_time, C_("microsecond abbreviation", "μs"), _("The delay to introduce for each tile"));
    _canvas_debug_show_redraw.init("", "/options/rendering/debug_show_redraw", false);
    add_devmode_line(_("Show redraw"), _canvas_debug_show_redraw, "", _("Paint a translucent random colour over each newly drawn tile"));
    _canvas_debug_show_unclean.init("", "/options/rendering/debug_show_unclean", false);
    add_devmode_line(_("Show unclean region"), _canvas_debug_show_unclean, "", _("Show the region that needs to be redrawn in red (only in Cairo mode)"));
    _canvas_debug_show_snapshot.init("", "/options/rendering/debug_show_snapshot", false);
    add_devmode_line(_("Show snapshot region"), _canvas_debug_show_snapshot, "", _("Show the region that still contains a saved copy of previously rendered content in blue (only in Cairo mode)"));
    _canvas_debug_show_clean.init("", "/options/rendering/debug_show_clean", false);
    add_devmode_line(_("Show clean region's fragmentation"), _canvas_debug_show_clean, "", _("Show the outlines of the rectangles in the region where rendering is complete in green (only in Cairo mode)"));
    _canvas_debug_disable_redraw.init("", "/options/rendering/debug_disable_redraw", false);
    add_devmode_line(_("Disable redraw"), _canvas_debug_disable_redraw, "", _("Temporarily disable the idle redraw process completely"));
    _canvas_debug_sticky_decoupled.init("", "/options/rendering/debug_sticky_decoupled", false);
    add_devmode_line(_("Sticky decoupled mode"), _canvas_debug_sticky_decoupled, "", _("Stay in decoupled mode even after rendering is complete"));
    _canvas_debug_animate.init("", "/options/rendering/debug_animate", false);
    add_devmode_line(_("Animate"), _canvas_debug_animate, "", _("Continuously adjust viewing parameters in an animation loop."));

    AddPage(_page_rendering, _("Rendering"), PREFS_PAGE_RENDERING);
}

void InkscapePreferences::initPageBitmaps()
{
    /* Note: /options/bitmapoversample removed with Cairo renderer */
    _page_bitmaps.add_group_header( _("Edit"));
    _misc_bitmap_autoreload.init(_("Automatically reload images"), "/options/bitmapautoreload/value", true);
    _page_bitmaps.add_line( false, "", _misc_bitmap_autoreload, "",
                           _("Automatically reload linked images when file is changed on disk"));
    _misc_bitmap_editor.init("/options/bitmapeditor/value", true);
    _page_bitmaps.add_line( false, _("_Bitmap editor:"), _misc_bitmap_editor, "", "", true);
    _misc_svg_editor.init("/options/svgeditor/value", true);
    _page_bitmaps.add_line( false, _("_SVG editor:"), _misc_svg_editor, "", "", true);

    _page_bitmaps.add_group_header( _("Export"));
    _importexport_export_res.init("/dialogs/export/defaultxdpi/value", 0.0, 6000.0, 1.0, 1.0, Inkscape::Util::Quantity::convert(1, "in", "px"), true, false);
    _page_bitmaps.add_line( false, _("Default export _resolution:"), _importexport_export_res, _("dpi"),
                            _("Default image resolution (in dots per inch) in the Export dialog"), false);
    _page_bitmaps.add_group_header( _("Create"));
    _bitmap_copy_res.init("/options/createbitmap/resolution", 1.0, 6000.0, 1.0, 1.0, Inkscape::Util::Quantity::convert(1, "in", "px"), true, false);
    _page_bitmaps.add_line( false, _("Resolution for Create Bitmap _Copy:"), _bitmap_copy_res, _("dpi"),
                            _("Resolution used by the Create Bitmap Copy command"), false);

    _page_bitmaps.add_group_header( _("Import"));
    _bitmap_ask.init(_("Ask about linking and scaling when importing bitmap images"), "/dialogs/import/ask", true);
    _page_bitmaps.add_line( true, "", _bitmap_ask, "",
                           _("Pop-up linking and scaling dialog when importing bitmap image."));
    _svg_ask.init(_("Ask about linking and scaling when importing SVG images"), "/dialogs/import/ask_svg", true);
    _page_bitmaps.add_line( true, "", _svg_ask, "",
                           _("Pop-up linking and scaling dialog when importing SVG image."));

    _svgoutput_usesodipodiabsref.init(_("Store absolute file path for linked images"),
                                      "/options/svgoutput/usesodipodiabsref", false);
    _page_bitmaps.add_line(
        true, "", _svgoutput_usesodipodiabsref, "",
        _("By default, image links are stored as relative paths whenever possible. If this option is enabled, Inkscape "
          "will additionally add an absolute path ('sodipodi:absref' attribute) to the image. This is used as a "
          "fall-back for locating the linked image, for example if the SVG document has been moved on disk. Note that this "
          "will expose your directory structure in the file's source code, which can include personal information like your username."),
        false);

    {
        Glib::ustring labels[] = {_("Embed"), _("Link")};
        Glib::ustring values[] = {"embed", "link"};
        _bitmap_link.init("/dialogs/import/link", labels, values, "link");
        _page_bitmaps.add_line( false, _("Bitmap import/open mode:"), _bitmap_link, "", "", false);
    }

    {
        Glib::ustring labels[] = {_("Include"), _("Pages"), _("Embed"), _("Link"), _("New")};
        Glib::ustring values[] = {"include", "pages", "embed", "link", "new"};
        _svg_link.init("/dialogs/import/import_mode_svg", labels, values, "include");
        _page_bitmaps.add_line( false, _("SVG import mode:"), _svg_link, "", "", false);
    }

    {
        Glib::ustring labels[] = {_("None (auto)"), _("Smooth (optimizeQuality)"), _("Blocky (optimizeSpeed)") };
        Glib::ustring values[] = {"auto", "optimizeQuality", "optimizeSpeed"};
        _bitmap_scale.init("/dialogs/import/scale", labels, values, "scale");
        _page_bitmaps.add_line( false, _("Image scale (image-rendering):"), _bitmap_scale, "", "", false);
    }

    /* Note: /dialogs/import/quality removed use of in r12542 */
    _importexport_import_res.init("/dialogs/import/defaultxdpi/value", 0.0, 6000.0, 1.0, 1.0, Inkscape::Util::Quantity::convert(1, "in", "px"), true, false);
    _page_bitmaps.add_line( false, _("Default _import resolution:"), _importexport_import_res, _("dpi"),
                            _("Default import resolution (in dots per inch) for bitmap and SVG import"), false);
    _importexport_import_res_override.init(_("Override file resolution"), "/dialogs/import/forcexdpi", false);
    _page_bitmaps.add_line( false, "", _importexport_import_res_override, "",
                            _("Use default bitmap resolution in favor of information from file"));

    _page_bitmaps.add_group_header( _("Render"));
    // rendering outlines for pixmap image tags
    _rendering_image_outline.init( _("Images in Outline Mode"), "/options/rendering/imageinoutlinemode", false);
    _page_bitmaps.add_line(false, "", _rendering_image_outline, "", _("When active will render images while in outline mode instead of a red box with an x. This is useful for manual tracing."));

    this->AddPage(_page_bitmaps, _("Imported Images"), PREFS_PAGE_BITMAPS);
}

[[nodiscard]] static auto getShortcutsFileLabelsAndValues()
{
    auto pairs = Inkscape::Shortcuts::get_file_names();
    std::vector<Glib::ustring> labels(pairs.size()), values(pairs.size());
    std::transform(pairs.begin(), pairs.end(), labels.begin(), [](auto &&pair){ return std::move(pair.first ); });
    std::transform(pairs.begin(), pairs.end(), values.begin(), [](auto &&pair){ return std::move(pair.second); });
    return std::pair{std::move(labels), std::move(values)};
}

void InkscapePreferences::initKeyboardShortcuts(Gtk::TreeModel::iterator iter_ui)
{
    // ------- Shortcut file --------
    {
        auto const &[labels, values] = getShortcutsFileLabelsAndValues();
        auto const &default_value = values.front();
        _kb_filelist.init("/options/kbshortcuts/shortcutfile", labels, values, default_value);

        auto tooltip =
            Glib::ustring::compose(_("Select a file of predefined shortcuts and modifiers to use. Any customizations you "
                                     "create will be added separately to %1"),
                                   IO::Resource::get_path_string(IO::Resource::USER, IO::Resource::KEYS, "default.xml"));
        _page_keyshortcuts.add_line( false, _("Keyboard file:"), _kb_filelist, "", tooltip.c_str(), false);
    }

    // ---------- Tree --------
    _kb_store = Gtk::TreeStore::create( _kb_columns );
    _kb_store->set_sort_column ( GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID, Gtk::SortType::ASCENDING ); // only sort in onKBListKeyboardShortcuts()

    _kb_filter = Gtk::TreeModelFilter::create(_kb_store);
    _kb_filter->set_visible_func (sigc::mem_fun(*this, &InkscapePreferences::onKBSearchFilter));

    _kb_shortcut_renderer.property_editable() = true;

    _kb_tree.set_model(_kb_filter);
    _kb_tree.append_column(_("Name"), _kb_columns.name);
    _kb_tree.append_column(_("Shortcut"), _kb_shortcut_renderer);
    _kb_tree.append_column(_("Description"), _kb_columns.description);
    _kb_tree.append_column(_("ID"), _kb_columns.id);

    _kb_tree.set_expander_column(*_kb_tree.get_column(0));

    // Name
    _kb_tree.get_column(0)->set_resizable(true);
    _kb_tree.get_column(0)->set_clickable(true);
    _kb_tree.get_column(0)->set_fixed_width (200);

    // Shortcut
    _kb_tree.get_column(1)->set_resizable(true);
    _kb_tree.get_column(1)->set_clickable(true);
    _kb_tree.get_column(1)->set_fixed_width (150);
    //_kb_tree.get_column(1)->add_attribute(_kb_shortcut_renderer.property_text(), _kb_columns.shortcut);
    _kb_tree.get_column(1)->set_cell_data_func(_kb_shortcut_renderer, &InkscapePreferences::onKBShortcutRenderer);

    // Description
    auto desc_renderer = dynamic_cast<Gtk::CellRendererText*>(_kb_tree.get_column_cell_renderer(2));
    desc_renderer->property_wrap_mode() = Pango::WrapMode::WORD;
    desc_renderer->property_wrap_width() = 600;
    _kb_tree.get_column(2)->set_resizable(true);
    _kb_tree.get_column(2)->set_clickable(true);
    _kb_tree.get_column(2)->set_expand(true);

    // ID
    _kb_tree.get_column(3)->set_resizable(true);
    _kb_tree.get_column(3)->set_clickable(true);

    _kb_shortcut_renderer.signal_accel_edited().connect( sigc::mem_fun(*this, &InkscapePreferences::onKBTreeEdited) );
    _kb_shortcut_renderer.signal_accel_cleared().connect( sigc::mem_fun(*this, &InkscapePreferences::onKBTreeCleared) );

    _kb_notebook.append_page(_kb_page_shortcuts, _("Shortcuts"));
    auto const shortcut_scroller = Gtk::make_managed<Gtk::ScrolledWindow>();
    shortcut_scroller->set_child(_kb_tree);
    shortcut_scroller->set_hexpand();
    shortcut_scroller->set_vexpand();
    // -------- Search --------
    _kb_search.init("/options/kbshortcuts/value", true);
    // clear filter initially to show all shortcuts;
    // this entry will typically be stale and long forgotten and not what user is looking for
    _kb_search.set_text({});
    _kb_page_shortcuts.add_line( false, _("Search:"), _kb_search, "", "", true);
    _kb_page_shortcuts.attach(*shortcut_scroller, 0, 3, 2, 1);

    _mod_store = Gtk::TreeStore::create( _mod_columns );
    _mod_tree.set_model(_mod_store);
    _mod_tree.append_column(_("Name"), _mod_columns.name);
    _mod_tree.append_column("hot", _mod_columns.and_modifiers);
    _mod_tree.append_column(_("ID"), _mod_columns.id);
    _mod_tree.set_tooltip_column(2);

    // In order to get tooltips on header, we must create our own label.
    auto const and_keys_header = Gtk::make_managed<Gtk::Label>(_("Modifier"));
    and_keys_header->set_tooltip_text(_("All keys specified must be held down to activate this functionality."));
    and_keys_header->set_visible(true);
    auto and_keys_column = _mod_tree.get_column(1);
    and_keys_column->set_widget(*and_keys_header);

    auto const edit_bar = Gtk::make_managed<Gtk::Box>();
    _kb_mod_ctrl.set_label("Ctrl");
    _kb_mod_shift.set_label("Shift");
    _kb_mod_alt.set_label("Alt");
    _kb_mod_meta.set_label("Meta");
    _kb_mod_enabled.set_label(_("Enabled"));
    edit_bar->append(_kb_mod_ctrl);
    edit_bar->append(_kb_mod_shift);
    edit_bar->append(_kb_mod_alt);
    edit_bar->append(_kb_mod_meta);
    edit_bar->append(_kb_mod_enabled);
    _kb_mod_ctrl.signal_toggled().connect(sigc::mem_fun(*this, &InkscapePreferences::on_modifier_edited));
    _kb_mod_shift.signal_toggled().connect(sigc::mem_fun(*this, &InkscapePreferences::on_modifier_edited));
    _kb_mod_alt.signal_toggled().connect(sigc::mem_fun(*this, &InkscapePreferences::on_modifier_edited));
    _kb_mod_meta.signal_toggled().connect(sigc::mem_fun(*this, &InkscapePreferences::on_modifier_edited));
    _kb_mod_enabled.signal_toggled().connect(sigc::mem_fun(*this, &InkscapePreferences::on_modifier_enabled));
    _kb_page_modifiers.add_line(false, _("Change:"), *edit_bar, "", "", true);

    _mod_tree.get_selection()->signal_changed().connect(sigc::mem_fun(*this, &InkscapePreferences::on_modifier_selection_changed));
    on_modifier_selection_changed();

    _kb_notebook.append_page(_kb_page_modifiers, _("Tools Modifiers"));
    auto const mod_scroller = Gtk::make_managed<Gtk::ScrolledWindow>();
    mod_scroller->set_child(_mod_tree);
    mod_scroller->set_hexpand();
    mod_scroller->set_vexpand();
    _kb_page_modifiers.attach(*mod_scroller, 0, 1, 2, 1);

    int row = 2;
    _page_keyshortcuts.attach(_kb_notebook, 0, row, 2, 1);

    row++;

    // ------ Reset/Import/Export -------
    auto const box_buttons = Gtk::make_managed<Gtk::Box>();

    box_buttons->set_spacing(4);
    box_buttons->set_hexpand();
    _page_keyshortcuts.attach(*box_buttons, 0, row, 3, 1);

    auto const kb_reset = Gtk::make_managed<Gtk::Button>(_("Reset"));
    kb_reset->set_use_underline();
    kb_reset->set_tooltip_text(_("Remove all your customized keyboard shortcuts, and revert to the shortcuts in the shortcut file listed above"));
    UI::pack_start(*box_buttons, *kb_reset, false, true, 6);

    auto const kb_export = Gtk::make_managed<Gtk::Button>(_("Export ..."));
    kb_export->set_use_underline();
    kb_export->set_tooltip_text(_("Export custom keyboard shortcuts to a file"));
    UI::pack_end(*box_buttons, *kb_export, false, true, 6);

    auto const kb_import = Gtk::make_managed<Gtk::Button>(_("Import ..."));
    kb_import->set_use_underline();
    kb_import->set_tooltip_text(_("Import custom keyboard shortcuts from a file"));
    UI::pack_end(*box_buttons, *kb_import, false, true, 6);

    kb_reset->signal_clicked().connect( sigc::mem_fun(*this, &InkscapePreferences::onKBReset) );
    kb_import->signal_clicked().connect( sigc::mem_fun(*this, &InkscapePreferences::onKBImport) );
    kb_export->signal_clicked().connect( sigc::mem_fun(*this, &InkscapePreferences::onKBExport) );
    _kb_filelist.signal_changed().connect( sigc::mem_fun(*this, &InkscapePreferences::onKBList) );
    _page_keyshortcuts.signal_realize().connect( sigc::mem_fun(*this, &InkscapePreferences::onKBRealize) );

    auto const key = Gtk::EventControllerKey::create();
    key->signal_key_released().connect([this](auto &&...args) { onKBSearchKeyReleased(); });
    _kb_search.add_controller(key);

    _keyboard_sizegroup = Gtk::SizeGroup::create(Gtk::SizeGroup::Mode::HORIZONTAL);
    _keyboard_sizegroup->add_widget(*kb_reset);
    _keyboard_sizegroup->add_widget(*kb_export);
    _keyboard_sizegroup->add_widget(*kb_import);

    this->AddPage(_page_keyshortcuts, _("Keyboard Shortcuts"), iter_ui, PREFS_PAGE_UI_KEYBOARD_SHORTCUTS);

    _kb_shortcuts_loaded = false;
    Gtk::TreeStore::iterator iter_group = _kb_store->append();
    (*iter_group)[_kb_columns.name] = _("Loading ...");
    (*iter_group)[_kb_columns.shortcut] = "";
    (*iter_group)[_kb_columns.id] = "";
    (*iter_group)[_kb_columns.description] = "";
    (*iter_group)[_kb_columns.shortcutkey] = Gtk::AccelKey();
    (*iter_group)[_kb_columns.user_set] = 0;

    Gtk::TreeStore::iterator iter_mods = _mod_store->append();
    (*iter_mods)[_mod_columns.name] = _("Loading ...");
    (*iter_group)[_mod_columns.id] = "";
    (*iter_group)[_mod_columns.description] = _("Unable to load keyboard modifier list.");
    (*iter_group)[_mod_columns.and_modifiers] = "";
}

void InkscapePreferences::onKBList()
{
    // New file path already stored in preferences.
    Inkscape::Shortcuts::getInstance().init();
    onKBListKeyboardShortcuts();
}

void InkscapePreferences::onKBReset()
{
    Inkscape::Shortcuts::getInstance().clear_user_shortcuts();
    onKBListKeyboardShortcuts();
}

void InkscapePreferences::onKBImport()
{
    if (Inkscape::Shortcuts::getInstance().import_shortcuts()) {
        onKBListKeyboardShortcuts();
    }
}

void InkscapePreferences::onKBExport()
{
    Inkscape::Shortcuts::getInstance().export_shortcuts();
}

void InkscapePreferences::onKBSearchKeyReleased()
{
    _kb_filter->refilter();
    auto search = _kb_search.get_text();
    if (search.length() > 2) {
        _kb_tree.expand_all();
    } else {
        _kb_tree.collapse_all();
    }
}

void InkscapePreferences::onKBTreeCleared(const Glib::ustring& path)
{
    // Triggered by "Back" key.
    Gtk::TreeModel::iterator iter = _kb_filter->get_iter(path);
    Glib::ustring id = (*iter)[_kb_columns.id];
    // Gtk::AccelKey current_shortcut_key = (*iter)[_kb_columns.shortcutkey];

    // Remove current shortcut from user file (won't remove from other files).
    Inkscape::Shortcuts::getInstance().remove_user_shortcut(id);

    onKBListKeyboardShortcuts();
}

void InkscapePreferences::onKBTreeEdited (const Glib::ustring& path, guint accel_key, Gdk::ModifierType accel_mods, guint hardware_keycode)
{
    Inkscape::Shortcuts &shortcuts = Inkscape::Shortcuts::getInstance();
    auto const state = static_cast<GdkModifierType>(accel_mods);
    auto const new_shortcut_key = shortcuts.get_from(nullptr, accel_key, hardware_keycode,
                                                     state, true);
    if (new_shortcut_key.is_null()) return;

    Gtk::TreeModel::iterator iter = _kb_filter->get_iter(path);
    Glib::ustring id = (*iter)[_kb_columns.id];
    Gtk::AccelKey const current_shortcut_key = (*iter)[_kb_columns.shortcutkey];

    if (new_shortcut_key.get_key() == current_shortcut_key.get_key() &&
        new_shortcut_key.get_mod() == current_shortcut_key.get_mod())
    {
        return;
    }

    auto iapp = InkscapeApplication::instance();
    InkActionExtraData& action_data = iapp->get_action_extra_data();

    // Check if there is currently an actions assigned to this shortcut; if yes ask if the shortcut should be reassigned
    Glib::ustring action_name;
    Glib::ustring accel = Gtk::Accelerator::name(accel_key, accel_mods);
    auto const &actions = shortcuts.get_actions(accel);

    for (auto possible_action : actions) {
        if (action_data.isSameContext(id, possible_action)) {
            // TODO: Reformat the data attached here so it's compatible with action_data
            action_name = possible_action;
            break;
        }
    }

    if (!action_name.empty()) {
        auto action_label = action_data.get_label_for_action(action_name);

        Glib::ustring message =
            Glib::ustring::compose(_("Keyboard shortcut \"%1\"\nis already assigned to \"%2\""),
                                   shortcuts.get_label(new_shortcut_key), action_label.empty() ? action_name : action_label);
        Gtk::MessageDialog dialog(message, false, Gtk::MessageType::QUESTION, Gtk::ButtonsType::YES_NO, true);
        dialog.set_title(_("Reassign shortcut?"));
        dialog.set_secondary_text(_("Are you sure you want to reassign this shortcut?"));
        dialog.set_transient_for(dynamic_cast<Gtk::Window &>(*get_root()));
        int response = Inkscape::UI::dialog_run(dialog);
        if (response != Gtk::ResponseType::YES) {
            return;
        }
    }

    shortcuts.add_user_shortcut(id, new_shortcut_key);

    onKBListKeyboardShortcuts();
}

static bool is_leaf_visible(const Gtk::TreeModel::const_iterator& iter, const Glib::ustring& search) {
    Glib::ustring name = (*iter)[_kb_columns.name];
    Glib::ustring desc = (*iter)[_kb_columns.description];
    Glib::ustring shortcut = (*iter)[_kb_columns.shortcut];
    Glib::ustring id = (*iter)[_kb_columns.id];

    if (name.lowercase().find(search) != name.npos
        || shortcut.lowercase().find(search) != name.npos
        || desc.lowercase().find(search) != name.npos
        || id.lowercase().find(search) != name.npos) {
        return true;
    }

    for (auto const &child : iter->children()) {
        if (is_leaf_visible(child.get_iter(), search)) {
            return true;
        }
    }

    return false;
}

bool InkscapePreferences::onKBSearchFilter(const Gtk::TreeModel::const_iterator& iter)
{
    Glib::ustring search = _kb_search.get_text().lowercase();
    if (search.empty()) {
        return true;
    }

    return is_leaf_visible(iter, search);
}

void InkscapePreferences::onKBRealize()
{
    if (!_kb_shortcuts_loaded /*&& _current_page == &_page_keyshortcuts*/) {
        _kb_shortcuts_loaded = true;
        onKBListKeyboardShortcuts();
    }
}

void InkscapePreferences::onKBShortcutRenderer(Gtk::CellRenderer *renderer,
                                               Gtk::TreeModel::const_iterator const &iter)
{
    Glib::ustring shortcut = (*iter)[onKBGetCols().shortcut];
    shortcut = Glib::Markup::escape_text(shortcut);
    unsigned int user_set = (*iter)[onKBGetCols().user_set];
    Gtk::CellRendererAccel *accel = dynamic_cast<Gtk::CellRendererAccel *>(renderer);
    if (user_set) {
        accel->property_markup() = Glib::ustring("<span font-weight='bold'> " + shortcut + " </span>").c_str();
    } else {
        accel->property_markup() = Glib::ustring("<span> " + shortcut + " </span>").c_str();
    }
}

void InkscapePreferences::on_modifier_selection_changed()
{
    _kb_is_updated = true;
    Gtk::TreeStore::iterator iter = _mod_tree.get_selection()->get_selected();
    auto selected = static_cast<bool>(iter);

    _kb_mod_ctrl.set_sensitive(selected);
    _kb_mod_shift.set_sensitive(selected);
    _kb_mod_alt.set_sensitive(selected);
    _kb_mod_meta.set_sensitive(selected);
    _kb_mod_enabled.set_sensitive(selected);

    _kb_mod_ctrl.set_active(false);
    _kb_mod_shift.set_active(false);
    _kb_mod_alt.set_active(false);
    _kb_mod_meta.set_active(false);
    _kb_mod_enabled.set_active(false);

    if (selected) {
        Glib::ustring modifier_id = (*iter)[_mod_columns.id];
        auto modifier = Modifiers::Modifier::get(modifier_id.c_str());
        Inkscape::Modifiers::KeyMask mask = Inkscape::Modifiers::NEVER;
        if(modifier != nullptr) {
            mask = modifier->get_and_mask();
        } else {
            _kb_mod_enabled.set_sensitive(false);
        }
        if(mask != Inkscape::Modifiers::NEVER) {
            _kb_mod_enabled.set_active(true);
            _kb_mod_ctrl.set_active(mask & Inkscape::Modifiers::CTRL);
            _kb_mod_shift.set_active(mask & Inkscape::Modifiers::SHIFT);
            _kb_mod_alt.set_active(mask & Inkscape::Modifiers::ALT);
            _kb_mod_meta.set_active(mask & Inkscape::Modifiers::META);
        } else {
            _kb_mod_ctrl.set_sensitive(false);
            _kb_mod_shift.set_sensitive(false);
            _kb_mod_alt.set_sensitive(false);
            _kb_mod_meta.set_sensitive(false);
        }
    }
    _kb_is_updated = false;
}

void InkscapePreferences::on_modifier_enabled()
{
    auto active = _kb_mod_enabled.get_active();
    _kb_mod_ctrl.set_sensitive(active);
    _kb_mod_shift.set_sensitive(active);
    _kb_mod_alt.set_sensitive(active);
    _kb_mod_meta.set_sensitive(active);
    on_modifier_edited();
}

void InkscapePreferences::on_modifier_edited()
{
    Gtk::TreeStore::iterator iter = _mod_tree.get_selection()->get_selected();
    if (!iter || _kb_is_updated) return;

    Glib::ustring modifier_id = (*iter)[_mod_columns.id];
    auto modifier = Modifiers::Modifier::get(modifier_id.c_str());
    if(!_kb_mod_enabled.get_active()) {
        modifier->set_user(Inkscape::Modifiers::NEVER, Inkscape::Modifiers::NOT_SET);
    } else {
        Inkscape::Modifiers::KeyMask mask = 0;
        if(_kb_mod_ctrl.get_active()) mask |= Inkscape::Modifiers::CTRL;
        if(_kb_mod_shift.get_active()) mask |= Inkscape::Modifiers::SHIFT;
        if(_kb_mod_alt.get_active()) mask |= Inkscape::Modifiers::ALT;
        if(_kb_mod_meta.get_active()) mask |= Inkscape::Modifiers::META;
        modifier->set_user(mask, Inkscape::Modifiers::NOT_SET);
    }
    Inkscape::Shortcuts& shortcuts = Inkscape::Shortcuts::getInstance();
    shortcuts.write_user();
    (*iter)[_mod_columns.and_modifiers] = modifier->get_label();
}

void InkscapePreferences::onKBListKeyboardShortcuts()
{
    Inkscape::Shortcuts& shortcuts = Inkscape::Shortcuts::getInstance();

    // Save the current selection
    Gtk::TreeStore::iterator iter = _kb_tree.get_selection()->get_selected();
    Glib::ustring selected_id = "";
    if (iter) {
        selected_id = (*iter)[_kb_columns.id];
    }

    _kb_store->clear();
    _mod_store->clear();

    // Gio::Actions

    auto iapp = InkscapeApplication::instance();
    auto gapp = iapp->gtk_app();

    // std::vector<Glib::ustring> actions = shortcuts.list_all_actions(); // All actions (app, win, doc)

    // Simpler and better to get action list from extra data (contains "detailed action names").
    InkActionExtraData& action_data = iapp->get_action_extra_data();
    std::vector<Glib::ustring> actions = action_data.get_actions();

    // Sort actions by section
    auto action_sort =
        [&](Glib::ustring &a, Glib::ustring &b) {
            return action_data.get_section_for_action(a) < action_data.get_section_for_action(b);
        };
    std::sort (actions.begin(), actions.end(), action_sort);

    Glib::ustring old_section;
    Gtk::TreeStore::iterator iter_group;

    // Fill sections
    for (auto const &action : actions) {
        Glib::ustring section = action_data.get_section_for_action(action);
        if (section.empty()) section = C_("Action Section", "Misc");
        if (section != old_section) {
            iter_group = _kb_store->append();
            (*iter_group)[_kb_columns.name] = g_dpgettext2(NULL, "Action Section", section.c_str());
            (*iter_group)[_kb_columns.shortcut] = "";
            (*iter_group)[_kb_columns.description] = "";
            (*iter_group)[_kb_columns.shortcutkey] = Gtk::AccelKey();
            (*iter_group)[_kb_columns.id] = "";
            (*iter_group)[_kb_columns.user_set] = 0;
            old_section = section;
        }

        // Find accelerators
        auto const &accels = shortcuts.get_triggers(action);
        Glib::ustring shortcut_label;
        for (auto const &accel : accels) {
            // Convert to more user friendly notation.
            // ::get_label shows key pad and numeric keys identically.
            // TODO: Results in labels like "Numpad Alt+5"
            if (accel.find("KP") != Glib::ustring::npos) {
                shortcut_label += _("Numpad");
                shortcut_label += " ";
            }
            unsigned int key = 0;
            Gdk::ModifierType mod = Gdk::ModifierType(0);
            Gtk::Accelerator::parse(accel, key, mod);
            shortcut_label += Gtk::Accelerator::get_label(key, mod) + ", ";
        }

        if (shortcut_label.size() > 1) {
            shortcut_label.erase(shortcut_label.size()-2);
        }

        // Find primary (i.e. first) shortcut.
        Gtk::AccelKey shortcut_key;
        if (accels.size() > 0) {
            unsigned int key = 0;
            Gdk::ModifierType mod = Gdk::ModifierType(0);
            Gtk::Accelerator::parse(accels[0], key, mod);
            shortcut_key = Gtk::AccelKey(key, mod);
        }

        // Add the action to the group
        Gtk::TreeStore::iterator row = _kb_store->append(iter_group->children());
        (*row)[_kb_columns.name] = action_data.get_label_for_action(action);
        (*row)[_kb_columns.shortcut] = shortcut_label;
        (*row)[_kb_columns.description] = action_data.get_tooltip_for_action(action);
        (*row)[_kb_columns.shortcutkey] = shortcut_key;
        (*row)[_kb_columns.id] =  action;
        (*row)[_kb_columns.user_set] = shortcuts.is_user_set(action);

        if (selected_id == action) {
            Gtk::TreeStore::Path sel_path = _kb_filter->convert_child_path_to_path(_kb_store->get_path(row));
            _kb_tree.expand_to_path(sel_path);
            _kb_tree.get_selection()->select(sel_path);
        }
    }

    std::string old_mod_group;
    Gtk::TreeStore::iterator iter_mod_group;

    // Modifiers (mouse specific keys)
    for(auto modifier: Inkscape::Modifiers::Modifier::getList()) {
        auto cat_name = modifier->get_category();
        if (cat_name != old_mod_group) {
            iter_mod_group = _mod_store->append();
            (*iter_mod_group)[_mod_columns.name] = cat_name.empty() ? "" : _(cat_name.c_str());
            (*iter_mod_group)[_mod_columns.id] = "";
            (*iter_mod_group)[_mod_columns.description] = "";
            (*iter_mod_group)[_mod_columns.and_modifiers] = "";
            (*iter_mod_group)[_mod_columns.user_set] = 0;
            old_mod_group = cat_name;
        }

        // Find accelerators
        Gtk::TreeStore::iterator iter_modifier = _mod_store->append(iter_mod_group->children());
        (*iter_modifier)[_mod_columns.name] = (modifier->get_name() && strlen(modifier->get_name())) ? _(modifier->get_name()) : "";
        (*iter_modifier)[_mod_columns.id] = modifier->get_id();
        (*iter_modifier)[_mod_columns.description] = (modifier->get_description() && strlen(modifier->get_description())) ? _(modifier->get_description()) : "";
        (*iter_modifier)[_mod_columns.and_modifiers] = modifier->get_label();
        (*iter_modifier)[_mod_columns.user_set] = modifier->is_set_user();
    }

    // re-order once after updating (then disable ordering again to increase performance)
    _kb_store->set_sort_column (_kb_columns.id, Gtk::SortType::ASCENDING );
    _kb_store->set_sort_column ( GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID, Gtk::SortType::ASCENDING );

    if (selected_id.empty()) {
        _kb_tree.expand_to_path(_kb_store->get_path(_kb_store->get_iter("0:1")));
    }

    // Update all GUI text that includes shortcuts.
    for (auto win : gapp->get_windows()) {
        shortcuts.update_gui_text_recursive(win);
    }
}

void InkscapePreferences::initPageSpellcheck()
{
#if WITH_LIBSPELLING
    _spell_ignorenumbers.init(_("Ignore words with digits"), "/dialogs/spellcheck/ignorenumbers", true);
    _page_spellcheck.add_line(false, "", _spell_ignorenumbers, "", _("Ignore words containing digits, such as \"R2D2\""), true);

    _spell_ignoreallcaps.init(_("Ignore words in ALL CAPITALS"), "/dialogs/spellcheck/ignoreallcaps", false);
    _page_spellcheck.add_line(false, "", _spell_ignoreallcaps, "", _("Ignore words in all capitals, such as \"IUPAC\""), true);

    AddPage(_page_spellcheck, _("Spellcheck"), PREFS_PAGE_SPELLCHECK);
#endif
}

template <typename string_type>
static void appendList(Glib::ustring& tmp, const std::vector<string_type> &listing)
{
    for (auto const & str : listing) {
        tmp += str;
        tmp += "\n";
    }
}


void InkscapePreferences::initPageSystem()
{
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    _sys_shared_path.init("/options/resources/sharedpath", true);
    auto const box = Gtk::make_managed<Gtk::Box>();
    UI::pack_start(*box, _sys_shared_path);
    box->set_size_request(300, -1);
    _page_system.add_line( false, _("Shared default resources folder:"), *box, "",
                            _("A folder structured like a user's Inkscape preferences directory. This makes it possible to share a set of resources, such as extensions, fonts, icon sets, keyboard shortcuts, patterns/hatches, palettes, symbols, templates, themes and user interface definition files, between multiple users who have access to that folder (on the same computer or in the network). Requires a restart of Inkscape to work when changed."), false, reset_icon());
    _page_system.add_group_header( _("System info"));

    _sys_user_prefs.set_text(prefs->getPrefsFilename());
    _sys_user_prefs.set_editable(false);

    auto const hbox = Gtk::make_managed<Gtk::Box>();

    auto const reset_prefs = Gtk::make_managed<Gtk::Button>(_("Reset"));
    reset_prefs->set_tooltip_text(_("Reset the preferences to default"));
    reset_prefs->signal_clicked().connect(sigc::mem_fun(*this, &InkscapePreferences::on_reset_prefs_clicked));
    hbox->append(*reset_prefs);

    auto const save_prefs = Gtk::make_managed<Gtk::Button>(_("Save"));
    save_prefs->set_tooltip_text(_("Save the preferences to disk"));
    save_prefs->set_action_name("app.save-preferences");
    hbox->append(*save_prefs);
    hbox->set_hexpand(false);

    _page_system.add_line(true, _("User preferences:"), _sys_user_prefs, "",
                          _("Location of the user’s preferences file"), true, hbox);
    auto profilefolder = Inkscape::IO::Resource::profile_path();
    _sys_user_config.init(profilefolder.c_str(), _("Open preferences folder"));
    _page_system.add_line(true, _("User config:"), _sys_user_config, "", _("Location of users configuration"), true);

    auto extensions_folder = IO::Resource::get_path_string(IO::Resource::USER, IO::Resource::EXTENSIONS);
    _sys_user_extension_dir.init(extensions_folder,
                                 _("Open extensions folder"));
    _page_system.add_line(true, _("User extensions:"), _sys_user_extension_dir, "",
                          _("Location of the user’s extensions"), true);

    _sys_user_fonts_dir.init((char const *)IO::Resource::get_path(IO::Resource::USER, IO::Resource::FONTS, ""),
                             _("Open fonts folder"));
    _page_system.add_line(true, _("User fonts:"), _sys_user_fonts_dir, "", _("Location of the user’s fonts"), true);

    _sys_user_themes_dir.init(g_build_filename(g_get_user_data_dir(), "themes", nullptr), _("Open themes folder"));
    _page_system.add_line(true, _("User themes:"), _sys_user_themes_dir, "", _("Location of the user’s themes"), true);

    _sys_user_icons_dir.init((char const *)IO::Resource::get_path(IO::Resource::USER, IO::Resource::ICONS, ""),
                             _("Open icons folder"));
    _page_system.add_line(true, _("User icons:"), _sys_user_icons_dir, "", _("Location of the user’s icons"), true);

    _sys_user_templates_dir.init((char const *)IO::Resource::get_path(IO::Resource::USER, IO::Resource::TEMPLATES, ""),
                                 _("Open templates folder"));
    _page_system.add_line(true, _("User templates:"), _sys_user_templates_dir, "",
                          _("Location of the user’s templates"), true);

    _sys_user_symbols_dir.init((char const *)IO::Resource::get_path(IO::Resource::USER, IO::Resource::SYMBOLS, ""),
                               _("Open symbols folder"));

    _page_system.add_line(true, _("User symbols:"), _sys_user_symbols_dir, "", _("Location of the user’s symbols"),
                          true);

    _sys_user_paint_servers_dir.init((char const *)IO::Resource::get_path(IO::Resource::USER, IO::Resource::PAINT, ""),
                                _("Open paint servers folder"));

    _page_system.add_line(true, _("User paint servers:"), _sys_user_paint_servers_dir, "",
                        _("Location of the user’s paint servers"), true);

    _sys_user_palettes_dir.init((char const *)IO::Resource::get_path(IO::Resource::USER, IO::Resource::PALETTES, ""),
                                _("Open palettes folder"));
    _page_system.add_line(true, _("User palettes:"), _sys_user_palettes_dir, "", _("Location of the user’s palettes"),
                          true);

    _sys_user_keys_dir.init((char const *)IO::Resource::get_path(IO::Resource::USER, IO::Resource::KEYS, ""),
                            _("Open keyboard shortcuts folder"));
    _page_system.add_line(true, _("User keys:"), _sys_user_keys_dir, "",
                          _("Location of the user’s keyboard mapping files"), true);

    _sys_user_ui_dir.init((char const *)IO::Resource::get_path(IO::Resource::USER, IO::Resource::UIS, ""),
                          _("Open user interface folder"));
    _page_system.add_line(true, _("User UI:"), _sys_user_ui_dir, "",
                          _("Location of the user’s user interface description files"), true);

    _sys_user_cache.set_text(g_get_user_cache_dir());
    _sys_user_cache.set_editable(false);
    _page_system.add_line(true, _("User cache:"), _sys_user_cache, "", _("Location of user’s cache"), true);

    Glib::ustring tmp_dir = prefs->getString("/options/autosave/path");
    if (tmp_dir.empty()) {
        tmp_dir = Glib::build_filename(Glib::get_user_cache_dir(), "inkscape");
    }
    _sys_tmp_files.set_text(tmp_dir);
    _sys_tmp_files.set_editable(false);
    _page_system.add_line(true, _("Temporary files:"), _sys_tmp_files, "", _("Location of the temporary files used for autosave"), true);

    _sys_data.set_text(get_inkscape_datadir());
    _sys_data.set_editable(false);
    _page_system.add_line(true, _("Inkscape data:"), _sys_data, "", _("Location of Inkscape data"), true);

    extensions_folder = IO::Resource::get_path_string(IO::Resource::SYSTEM, IO::Resource::EXTENSIONS);
    _sys_extension_dir.set_text(extensions_folder);
    _sys_extension_dir.set_editable(false);
    _page_system.add_line(true, _("Inkscape extensions:"), _sys_extension_dir, "", _("Location of the Inkscape extensions"), true);

    Glib::ustring tmp;
    auto system_data_dirs = Glib::get_system_data_dirs();
    appendList(tmp, system_data_dirs);
    _sys_systemdata.get_buffer()->insert(_sys_systemdata.get_buffer()->end(), tmp);
    _sys_systemdata.set_editable(false);
    _sys_systemdata_scroll.set_child(_sys_systemdata);
    _sys_systemdata_scroll.set_size_request(100, 80);
    _sys_systemdata_scroll.set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
    _sys_systemdata_scroll.set_has_frame(true);
    _page_system.add_line(true,  _("System data:"), _sys_systemdata_scroll, "", _("Locations of system data"), true);

    _sys_fontdirs_custom.init("/options/font/custom_fontdirs", 50);
    _page_system.add_line(true, _("Custom Font directories"), _sys_fontdirs_custom, "", _("Load additional fonts from custom locations (one path per line)"), true);

    tmp = "";

    auto const icon_theme = Gtk::IconTheme::get_for_display(Gdk::Display::get_default());
    auto paths = icon_theme->get_search_path();
    appendList( tmp, paths );
    _sys_icon.get_buffer()->insert(_sys_icon.get_buffer()->end(), tmp);
    _sys_icon.set_editable(false);
    _sys_icon_scroll.set_child(_sys_icon);
    _sys_icon_scroll.set_size_request(100, 80);
    _sys_icon_scroll.set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
    _sys_icon_scroll.set_has_frame(true);
    _page_system.add_line(true,  _("Icon theme:"), _sys_icon_scroll, "", _("Locations of icon themes"), true);

    this->AddPage(_page_system, _("System"), PREFS_PAGE_SYSTEM);
}

bool InkscapePreferences::GetSizeRequest(const Gtk::TreeModel::iterator& iter)
{
    Gtk::TreeModel::Row row = *iter;
    DialogPage* page = row[_page_list_columns._col_page];

    _page_frame.set_child(*page);
    int minimum_width{}, natural_width{}, minimum_height{}, natural_height{}, ignore{};
    measure(Gtk::Orientation::HORIZONTAL, -1, minimum_width , natural_width , ignore, ignore);
    measure(Gtk::Orientation::VERTICAL  , -1, minimum_height, natural_height, ignore, ignore);
    _minimum_width  = std::max(_minimum_width,  minimum_width );
    _minimum_height = std::max(_minimum_height, minimum_height);
    _natural_width  = std::max(_natural_width,  natural_width );
    _natural_height = std::max(_natural_height, natural_height);
    _page_frame.unset_child();

    return false;
}

// Check if iter points to page indicated in preferences.
bool InkscapePreferences::matchPage(Gtk::TreeModel::const_iterator const &iter)
{
    auto const &row = *iter;
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    int desired_page = prefs->getInt("/dialogs/preferences/page", 0);
    _init = false;
    if (desired_page == row[_page_list_columns._col_id])
    {
        auto const path = _page_list.get_model()->get_path(iter);
        _page_list.expand_to_path(path);
        _page_list.get_selection()->select(iter);
        if (desired_page == PREFS_PAGE_UI_THEME)
            symbolicThemeCheck();
        return true;
    }
    return false;
}

void InkscapePreferences::on_reset_open_recent_clicked()
{
    Glib::RefPtr<Gtk::RecentManager> manager = Gtk::RecentManager::get_default();
    std::vector< Glib::RefPtr< Gtk::RecentInfo > > recent_list = manager->get_items();

    // Remove only elements that were added by Inkscape
    // TODO: This should likely preserve items that were also accessed by other apps.
    //       However there does not seem to be straightforward way to delete only an application from an item.
    for (auto e : recent_list) {
        if (e->has_application(g_get_prgname())
            || e->has_application("org.inkscape.Inkscape")
            || e->has_application("inkscape")
#ifdef _WIN32
            || e->has_application("inkscape.exe")
#endif
           ) {
            manager->remove_item(e->get_uri());
        }
    }
}

void InkscapePreferences::on_reset_prefs_clicked()
{
    Inkscape::Preferences::get()->reset();
}

void InkscapePreferences::show_not_found()
{
    if (_current_page)
        _page_frame.unset_child();
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    _current_page = &_page_notfound;
    _page_title.set_markup(_("<span size='large'><b>No Results</b></span>"));
    _page_frame.set_child(*_current_page);
    _current_page->set_visible(true);
    if (prefs->getInt("/dialogs/preferences/page", 0) == PREFS_PAGE_UI_THEME) {
        symbolicThemeCheck();
    }
}

void InkscapePreferences::show_nothing_on_page()
{
    _page_frame.unset_child();
    _page_title.set_text("");
}

void InkscapePreferences::on_pagelist_selection_changed()
{
    // show new selection
    Glib::RefPtr<Gtk::TreeSelection> selection = _page_list.get_selection();
    Gtk::TreeModel::iterator iter = selection->get_selected();
    if(iter)
    {
        if (_current_page)
            _page_frame.unset_child();
        Gtk::TreeModel::Row row = *iter;
        _current_page = row[_page_list_columns._col_page];
        Inkscape::Preferences *prefs = Inkscape::Preferences::get();
        if (!_init) {
            prefs->setInt("/dialogs/preferences/page", row[_page_list_columns._col_id]);
        }
        Glib::ustring col_name_escaped = Glib::Markup::escape_text( row[_page_list_columns._col_name] );
        _page_title.set_markup("<span size='large'><b>" + col_name_escaped + "</b></span>");
        _page_frame.set_child(*_current_page);
        _current_page->set_visible(true);
        if (prefs->getInt("/dialogs/preferences/page", 0) == PREFS_PAGE_UI_THEME) {
            symbolicThemeCheck();
        }
    }
}

// Show page indicated in preferences file.
void InkscapePreferences::showPage()
{
    _search.set_text("");
    _page_list.get_model()->foreach_iter(sigc::mem_fun(*this, &InkscapePreferences::matchPage));
}

} // namespace Inkscape::UI::Dialog

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
