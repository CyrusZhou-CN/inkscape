// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef SEEN_PATTERN_EDITOR_H
#define SEEN_PATTERN_EDITOR_H

#include <unordered_map>
#include <vector>
#include <gtkmm/box.h>
#include <gtkmm/combobox.h>
#include <gtkmm/entry.h>
#include <gtkmm/flowbox.h>
#include <gtkmm/grid.h>
#include <gtkmm/button.h>
#include <gtkmm/togglebutton.h>
#include <gtkmm/scale.h>
#include <gtkmm/searchentry.h>
#include <gtkmm/spinbutton.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/liststore.h>
#include <gtkmm/paned.h>
#include <gtkmm/builder.h>
#include <optional>
#include <2geom/point.h>
#include <2geom/transforms.h>
#include "color.h"
#include "object/sp-pattern.h"
#include "pattern-manager.h"
#include "spin-scale.h"
#include "ui/operation-blocker.h"
#include "ui/widget/color-picker.h"
#include "ui/widget/pattern-store.h"

class SPDocument;
class ColorPicker;

namespace Inkscape {
namespace UI {
namespace Widget {

class PatternEditor : public Gtk::Box {
public:
    PatternEditor(const char* prefs, PatternManager& manager);
    ~PatternEditor() noexcept override;

    // pass current document to extract patterns
    void set_document(SPDocument* document);
    // set selected pattern
    void set_selected(SPPattern* pattern);
    // selected pattern ID if any plus stock pattern collection document (or null)
    std::pair<std::string, SPDocument*> get_selected();
    // and its color
    std::optional<unsigned int> get_selected_color();
    // return combined scale and rotation
    Geom::Affine get_selected_transform();
    // return pattern offset
    Geom::Point get_selected_offset();
    // is scale uniform?
    bool is_selected_scale_uniform();
    // return gap size for pattern tiles
    Geom::Scale get_selected_gap();
    // get pattern label
    Glib::ustring get_label();

private:
    sigc::signal<void> _signal_changed;
    sigc::signal<void, unsigned int> _signal_color_changed;
    sigc::signal<void> _signal_edit;

public:
    decltype(_signal_changed) signal_changed() const { return _signal_changed; }
    decltype(_signal_color_changed) signal_color_changed() const { return _signal_color_changed; }
    decltype(_signal_edit) signal_edit() const { return _signal_edit; }

private:
    void bind_store(Gtk::FlowBox& list, PatternStore& store);
    void update_store(const std::vector<Glib::RefPtr<PatternItem>>& list, Gtk::FlowBox& gallery, PatternStore& store);
    Glib::RefPtr<PatternItem> get_active(Gtk::FlowBox& gallery, PatternStore& pat);
    std::pair<Glib::RefPtr<PatternItem>, SPDocument*> get_active();
    void set_active(Gtk::FlowBox& gallery, PatternStore& pat, Glib::RefPtr<PatternItem> item);
    void update_widgets_from_pattern(Glib::RefPtr<PatternItem>& pattern);
    void update_scale_link();
    void update_ui(Glib::RefPtr<PatternItem> pattern);
    std::vector<Glib::RefPtr<PatternItem>> update_doc_pattern_list(SPDocument* document);
    void set_stock_patterns(const std::vector<SPPattern*>& patterns);
    void select_pattern_set(int index);
    void apply_filter(bool stock);
    void update_pattern_tiles();

    Glib::RefPtr<Gtk::Builder> _builder;
    Gtk::Paned& _paned;
    Gtk::Box& _main_grid;
    Gtk::Grid& _input_grid;
    Gtk::SpinButton& _offset_x;
    Gtk::SpinButton& _offset_y;
    Gtk::SpinButton& _scale_x;
    Gtk::SpinButton& _scale_y;
    Gtk::SpinButton& _angle_btn;
    Gtk::Scale& _orient_slider;
    Gtk::Scale& _gap_x_slider;
    Gtk::Scale& _gap_y_slider;
    Gtk::Button& _edit_btn;
    Gtk::Label& _color_label;
    Gtk::Button& _color_btn;
    Gtk::Button& _link_scale;
    Gtk::Image& _preview_img;
    Gtk::Viewport& _preview;
    Gtk::FlowBox& _doc_gallery;
    Gtk::FlowBox& _stock_gallery;
    Gtk::Entry& _name_box;
    Gtk::ComboBoxText& _combo_set;
    Gtk::SearchEntry& _search_box;
    Gtk::Scale& _tile_slider;
    Gtk::CheckButton& _show_names;
    Glib::RefPtr<Gtk::TreeModel> _categories;
    bool _scale_linked = true;
    Glib::ustring _prefs;
    PatternStore _doc_pattern_store;
    PatternStore _stock_pattern_store;
    std::unique_ptr<ColorPicker> _color_picker;
    OperationBlocker _update;
    std::unordered_map<std::string, Glib::RefPtr<PatternItem>> _cached_items; // cached current document patterns
    Inkscape::PatternManager& _manager;
    Glib::ustring _filter_text;
    int _tile_size = 0;
    SPDocument* _current_document = nullptr;
    // pattern being currently edited: id for a root pattern, and link id of a pattern with href set
    // plus current translation offset, so we can preserve it
    struct { Glib::ustring id; Glib::ustring link_id; Geom::Point offset; } _current_pattern;
};


} // namespace Widget
} // namespace UI
} // namespace Inkscape

#endif
