// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Generic object attribute editor
 *//*
 * Authors:
 * see git history
 * Kris De Gussem <Kris.DeGussem@gmail.com>
 * Michael Kowalski
 *
 * Copyright (C) 2018 Authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <gtkmm/entry.h>
#include <gtkmm/eventcontrollerkey.h>
#include <gtkmm/menubutton.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/textview.h>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <glibmm/i18n.h>
#include <glibmm/markup.h>
#include <glibmm/ustring.h>
#include <gtkmm/button.h>
#include <gtkmm/entry.h>
#include <gtkmm/enums.h>
#include <gtkmm/grid.h>
#include <gtkmm/label.h>
#include <gtkmm/spinbutton.h>
#include <gtkmm/togglebutton.h>
#include <gtkmm/treemodel.h>
#include <2geom/rect.h>

#include "desktop.h"
#include "document-undo.h"
#include "mod360.h"
#include "preferences.h"
#include "selection.h"
#include "actions/actions-tools.h"
#include "live_effects/effect-enum.h"
#include "live_effects/effect.h"
#include "live_effects/lpeobject.h"
#include "object/sp-anchor.h"
#include "object/sp-ellipse.h"
#include "object/sp-image.h"
#include "object/sp-item.h"
#include "object/sp-lpe-item.h"
#include "object/sp-namedview.h"
#include "object/sp-object.h"
#include "object/sp-path.h"
#include "object/sp-rect.h"
#include "object/sp-star.h"
#include "ui/builder-utils.h"
#include "ui/controller.h"
#include "ui/dialog/object-attributes.h"
#include "ui/icon-names.h"
#include "ui/pack.h"
#include "ui/tools/object-picker-tool.h"
#include "ui/syntax.h"
#include "ui/util.h"
#include "ui/widget/image-properties.h"
#include "ui/widget/spinbutton.h"
#include "ui/widget/style-swatch.h"
#include "widgets/sp-attribute-widget.h"
#include "xml/href-attribute-helper.h"

namespace Inkscape::UI::Dialog {

struct SPAttrDesc {
    char const *label;
    char const *attribute;
};

static const SPAttrDesc anchor_desc[] = {
    { N_("Href:"), "xlink:href"},
    { N_("Target:"), "target"},
    { N_("Type:"), "xlink:type"},
    // TRANSLATORS: for info, see http://www.w3.org/TR/2000/CR-SVG-20000802/linking.html#AElementXLinkRoleAttribute
    // Identifies the type of the related resource with an absolute URI
    { N_("Role:"), "xlink:role"},
    // TRANSLATORS: for info, see http://www.w3.org/TR/2000/CR-SVG-20000802/linking.html#AElementXLinkArcRoleAttribute
    // For situations where the nature/role alone isn't enough, this offers an additional URI defining the purpose of the link.
    { N_("Arcrole:"), "xlink:arcrole"},
    // TRANSLATORS: for info, see http://www.w3.org/TR/2000/CR-SVG-20000802/linking.html#AElementXLinkTitleAttribute
    { N_("Title:"), "xlink:title"},
    { N_("Show:"), "xlink:show"},
    // TRANSLATORS: for info, see http://www.w3.org/TR/2000/CR-SVG-20000802/linking.html#AElementXLinkActuateAttribute
    { N_("Actuate:"), "xlink:actuate"},
    { nullptr, nullptr}
};

const auto dlg_pref_path = Glib::ustring("/dialogs/object-properties/");

ObjectAttributes::ObjectAttributes()
    : DialogBase(dlg_pref_path.c_str(), "ObjectProperties"),
    _builder(create_builder("object-attributes.glade")),
    _main_panel(get_widget<Gtk::Box>(_builder, "main-panel")),
    _obj_title(get_widget<Gtk::Label>(_builder, "main-obj-name")),
    _style_swatch(nullptr, _("Item's fill, stroke and opacity"), Gtk::Orientation::HORIZONTAL),
    _obj_properties(*Gtk::make_managed<ObjectProperties>())
{
    auto& main = get_widget<Gtk::Box>(_builder, "main-widget");
    main.append(_obj_properties);

    _obj_title.set_text("");
    _style_swatch.set_hexpand(false);
    _style_swatch.set_valign(Gtk::Align::CENTER);
    get_widget<Gtk::Box>(_builder, "main-header").append(_style_swatch);
    append(main);
    create_panels();
    _style_swatch.set_visible(false);
}

void ObjectAttributes::widget_setup() {
    if (_update.pending() || !getDesktop()) return;

    auto selection = getDesktop()->getSelection();
    auto item = selection->singleItem();

    auto scoped(_update.block());

    auto panel = get_panel(item);
    if (panel != _current_panel && _current_panel) {
        _current_panel->update_panel(nullptr, nullptr);
        _main_panel.remove(_current_panel->widget());
        _obj_title.set_text("");
    }

    _current_panel = panel;
    _current_item = nullptr;
    bool enable_props = panel != nullptr;

    Glib::ustring title = panel ? panel->get_title() : "";
    if (!panel) {
        if (item) {
            if (auto name = item->displayName()) {
                title = name;
            }
            // show properties for element without dedicated attributes panel
            enable_props = true;
        }
        else if (selection->size() > 1) {
            title = _("Multiple objects selected");
            // current "object properties" subdialog doesn't handle multiselection
            enable_props = false;
        }
        else {
            title = _("No selection");
        }
    }
    _obj_title.set_markup("<b>" + Glib::Markup::escape_text(title) + "</b>");

    if (!panel) {
        _style_swatch.set_visible(false);
    }
    else {
        if (_main_panel.get_children().empty()) {
            UI::pack_start(_main_panel, panel->widget(), true, true);
        }
        bool show_style = false;
        if (panel->supports_fill_stroke()) {
            if (auto style = item ? item->style : nullptr) {
                _style_swatch.setStyle(style);
                show_style = true;
            }
        }
        _style_swatch.set_visible(show_style);
        panel->update_panel(item, getDesktop());
        panel->widget().set_visible(true);
    }

    _current_item = item;

    // TODO
    // show no of LPEs?
    // show locked status?
}

void ObjectAttributes::update_panel(SPObject* item) {
    if (!_current_panel) return;

    if (_current_panel->supports_fill_stroke()) {
        if (auto style = item ? item->style : nullptr) {
            _style_swatch.setStyle(style);
        }
    }
    _current_panel->update_panel(item, getDesktop());
}

void ObjectAttributes::desktopReplaced() {
    _obj_properties.update_entries();
}

void ObjectAttributes::selectionChanged(Selection* selection) {
    widget_setup();
    _obj_properties.update_entries();
}

void ObjectAttributes::selectionModified(Selection* _selection, guint flags) {
    if (_update.pending() || !getDesktop() || !_current_panel) return;

    auto selection = getDesktop()->getSelection();
    if (flags & (SP_OBJECT_MODIFIED_FLAG |
                 SP_OBJECT_PARENT_MODIFIED_FLAG |
                 SP_OBJECT_STYLE_MODIFIED_FLAG)) {

        auto item = selection->singleItem();
        if (item == _current_item) {
            update_panel(item);
        }
        else {
            g_warning("ObjectAttributes: missed selection change?");
        }
    }
}

///////////////////////////////////////////////////////////////////////////////

std::tuple<bool, double, double> round_values(double x, double y) {
    auto a = std::round(x);
    auto b = std::round(y);
    return std::make_tuple(a != x || b != y, a, b);
}

std::tuple<bool, double, double> round_values(Gtk::SpinButton& x, Gtk::SpinButton& y) {
    return round_values(x.get_adjustment()->get_value(), y.get_adjustment()->get_value());
}

const LivePathEffectObject* find_lpeffect(SPLPEItem* item, LivePathEffect::EffectType etype) {
    if (!item) return nullptr;

    auto lpe = item->getFirstPathEffectOfType(Inkscape::LivePathEffect::FILLET_CHAMFER);
    if (!lpe) return nullptr;
    return lpe->getLPEObj();
}

void remove_lpeffect(SPLPEItem* item, LivePathEffect::EffectType type) {
    if (auto effect = find_lpeffect(item, type)) {
        item->setCurrentPathEffect(effect);
        auto document = item->document;
        item->removeCurrentPathEffect(false);
        DocumentUndo::done(document, _("Removed live path effect"), INKSCAPE_ICON("dialog-path-effects"));
    }
}

std::optional<double> get_number(SPItem* item, const char* attribute) {
    if (!item) return {};

    auto val = item->getAttribute(attribute);
    if (!val) return {};

    return item->getRepr()->getAttributeDouble(attribute);
}

void align_star_shape(SPStar* path) {
    if (!path || !path->sides) return;

    auto arg1 = path->arg[0];
    auto arg2 = path->arg[1];
    auto delta = arg2 - arg1;
    auto top = -M_PI / 2;
    auto odd = path->sides & 1;
    if (odd) {
        arg1 = top;
    }
    else {
        arg1 = top - M_PI / path->sides;
    }
    arg2 = arg1 + delta;

    path->setAttributeDouble("sodipodi:arg1", arg1);
    path->setAttributeDouble("sodipodi:arg2", arg2);
    path->updateRepr();
}

///////////////////////////////////////////////////////////////////////////////

details::AttributesPanel::AttributesPanel() {
    _tracker = std::make_unique<UI::Widget::UnitTracker>(Inkscape::Util::UNIT_TYPE_LINEAR);
    //todo:
    // auto init_units = desktop->getNamedView()->display_units;
    // _tracker->setActiveUnit(init_units);
}

void details::AttributesPanel::update_panel(SPObject* object, SPDesktop* desktop) {
    if (object && object->document) {
        auto scoped(_update.block());
        auto units = object->document->getNamedView() ? object->document->getNamedView()->display_units : nullptr;
        // auto init_units = desktop->getNamedView()->display_units;
        if (units) _tracker->setActiveUnit(units);
    }

    _desktop = desktop;

    if (!_update.pending()) {
        update(object);
    }
}

void details::AttributesPanel::change_value_px(SPObject* object, const Glib::RefPtr<Gtk::Adjustment>& adj, const char* attr, std::function<void (double)>&& setter) {
    if (_update.pending() || !object) return;

    auto scoped(_update.block());

    const auto unit = _tracker->getActiveUnit();
    auto value = Util::Quantity::convert(adj->get_value(), unit, "px");
    if (value != 0 || attr == nullptr) {
        setter(value);
    }
    else if (attr) {
        object->removeAttribute(attr);
    }

    DocumentUndo::done(object->document, _("Change object attribute"), ""); //TODO INKSCAPE_ICON("draw-rectangle"));
}

void details::AttributesPanel::change_angle(SPObject* object, const Glib::RefPtr<Gtk::Adjustment>& adj, std::function<void (double)>&& setter) {
    if (_update.pending() || !object) return;

    auto scoped(_update.block());

    auto value = degree_to_radians_mod2pi(adj->get_value());
    setter(value);

    DocumentUndo::done(object->document, _("Change object attribute"), ""); //TODO INKSCAPE_ICON("draw-rectangle"));
}

void details::AttributesPanel::change_value(SPObject* object, const Glib::RefPtr<Gtk::Adjustment>& adj, std::function<void (double)>&& setter) {
    if (_update.pending() || !object) return;

    auto scoped(_update.block());

    auto value = adj ? adj->get_value() : 0;
    setter(value);

    DocumentUndo::done(object->document, _("Change object attribute"), ""); //TODO INKSCAPE_ICON("draw-rectangle"));
}

///////////////////////////////////////////////////////////////////////////////

class ImagePanel : public details::AttributesPanel {
public:
    ImagePanel() {
        _title = _("Image");
        _show_fill_stroke = false;
        _panel = std::make_unique<Inkscape::UI::Widget::ImageProperties>();
        _widget = _panel.get();
    }
    ~ImagePanel() override = default;

    void update(SPObject* object) override { _panel->update(cast<SPImage>(object)); }

private:
    std::unique_ptr<Inkscape::UI::Widget::ImageProperties> _panel;
};

///////////////////////////////////////////////////////////////////////////////

class AnchorPanel : public details::AttributesPanel {
public:
    AnchorPanel() {
        _title = _("Anchor");
        _show_fill_stroke = false;
        _table = std::make_unique<SPAttributeTable>();
        _table->set_visible(true);
        _table->set_hexpand();
        _table->set_vexpand(false);
        _widget = _table.get();

        std::vector<Glib::ustring> labels;
        std::vector<Glib::ustring> attrs;
        int len = 0;
        while (anchor_desc[len].label) {
            labels.emplace_back(anchor_desc[len].label);
            attrs.emplace_back(anchor_desc[len].attribute);
            len += 1;
        }
        _table->create(labels, attrs);
    }

    ~AnchorPanel() override = default;

    void update(SPObject* object) override {
        auto anchor = cast<SPAnchor>(object);
        auto changed = _anchor != anchor;
        _anchor = anchor;
        if (!anchor) {
            _picker.disconnect();
            return;
        }

        if (changed) {
            _table->change_object(anchor);

            if (auto grid = dynamic_cast<Gtk::Grid*>(_table->get_first_child())) {
                auto op_button = Gtk::make_managed<Gtk::ToggleButton>();
                op_button->set_active(false);
                op_button->set_tooltip_markup(_("<b>Picker Tool</b>\nSelect objects on canvas"));
                op_button->set_margin_start(4);
                op_button->set_image_from_icon_name("object-pick");

                op_button->signal_toggled().connect([=, this] {
                    // Use operation blocker to block the toggle signal
                    // emitted when the object has been picked and the
                    // button is toggled.
                    if (!_desktop || _update.pending()) {
                        return;
                    }

                    // Disconnect the picker signal if the button state is
                    // toggled to inactive.
                    if (!op_button->get_active()) {
                        _picker.disconnect();
                        set_active_tool(_desktop, _desktop->getTool()->get_last_active_tool());
                        return;
                    }

                    // activate object picker tool
                    set_active_tool(_desktop, "Picker");

                    if (auto tool = dynamic_cast<Inkscape::UI::Tools::ObjectPickerTool*>(_desktop->getTool())) {
                        _picker = tool->signal_object_picked.connect([grid, this](SPObject* item){
                            // set anchor href
                            auto edit = dynamic_cast<Gtk::Entry*>(grid->get_child_at(1, 0));
                            if (edit && item) {
                                Glib::ustring id = "#";
                                edit->set_text(id + item->getId());
                            }
                            _picker.disconnect();
                            return false; // no more object picking
                        });

                        _tool_switched = tool->signal_tool_switched.connect([=, this] {
                            if (op_button->get_active()) {
                                auto scoped(_update.block());
                                op_button->set_active(false);
                            }
                            _tool_switched.disconnect();
                        });
                    }
                });
                grid->attach(*op_button, 2, 0);
            }
        }
        else {
            _table->reread_properties();
        }
    }

private:
    std::unique_ptr<SPAttributeTable> _table;
    SPAnchor* _anchor = nullptr;
    sigc::scoped_connection _picker;
    sigc::scoped_connection _tool_switched;
    bool _first_time_update = true;
};

///////////////////////////////////////////////////////////////////////////////

class RectPanel : public details::AttributesPanel {
public:
    RectPanel(Glib::RefPtr<Gtk::Builder> builder) :
        _main(get_widget<Gtk::Grid>(builder, "rect-main")),
        _width(get_derived_widget<Inkscape::UI::Widget::SpinButton>(builder, "rect-width")),
        _height(get_derived_widget<Inkscape::UI::Widget::SpinButton>(builder, "rect-height")),
        _rx(get_derived_widget<Inkscape::UI::Widget::SpinButton>(builder, "rect-rx")),
        _ry(get_derived_widget<Inkscape::UI::Widget::SpinButton>(builder, "rect-ry")),
        _sharp(get_widget<Gtk::Button>(builder, "rect-sharp")),
        _round(get_widget<Gtk::Button>(builder, "rect-corners"))
    {
        _title = _("Rectangle");
        _widget = &_main;

        _width.get_adjustment()->signal_value_changed().connect([this](){
            change_value_px(_rect, _width.get_adjustment(), "width", [this](double w){ _rect->setVisibleWidth(w); });
        });
        _height.get_adjustment()->signal_value_changed().connect([this](){
            change_value_px(_rect, _height.get_adjustment(), "height", [this](double h){ _rect->setVisibleHeight(h); });
        });
        _rx.get_adjustment()->signal_value_changed().connect([this](){
            change_value_px(_rect, _rx.get_adjustment(), "rx", [this](double rx){ _rect->setVisibleRx(rx); });
        });
        _ry.get_adjustment()->signal_value_changed().connect([this](){
            change_value_px(_rect, _ry.get_adjustment(), "ry", [this](double ry){ _rect->setVisibleRy(ry); });
        });
        get_widget<Gtk::Button>(builder, "rect-round").signal_clicked().connect([this](){
            auto [changed, x, y] = round_values(_width, _height);
            if (changed) {
                _width.get_adjustment()->set_value(x);
                _height.get_adjustment()->set_value(y);
            }
        });
        _sharp.signal_clicked().connect([this](){
            if (!_rect) return;

            // remove rounded corners if LPE is there (first one found)
            remove_lpeffect(_rect, LivePathEffect::FILLET_CHAMFER);
            _rx.get_adjustment()->set_value(0);
            _ry.get_adjustment()->set_value(0);
        });
        _round.signal_clicked().connect([this](){
            if (!_rect || !_desktop) return;

            // switch to node tool to show handles
            set_active_tool(_desktop, "Node");
            // rx/ry need to be reset first, LPE doesn't handle them too well
            _rx.get_adjustment()->set_value(0);
            _ry.get_adjustment()->set_value(0);
            // add flexible corners effect if not yet present
            if (!find_lpeffect(_rect, LivePathEffect::FILLET_CHAMFER)) {
                LivePathEffect::Effect::createAndApply("fillet_chamfer", _rect->document, _rect);
                DocumentUndo::done(_rect->document, _("Add fillet/chamfer effect"), INKSCAPE_ICON("dialog-path-effects"));
            }
        });
    }

    ~RectPanel() override = default;

    void update(SPObject* object) override {
        _rect = cast<SPRect>(object);
        if (!_rect) return;

        auto scoped(_update.block());
        _width.set_value(_rect->width.value);
        _height.set_value(_rect->height.value);
        _rx.set_value(_rect->rx.value);
        _ry.set_value(_rect->ry.value);
        auto lpe = find_lpeffect(_rect, LivePathEffect::FILLET_CHAMFER);
        _sharp.set_sensitive(_rect->rx.value > 0 || _rect->ry.value > 0 || lpe);
        _round.set_sensitive(!lpe);
    }

private:
    SPRect* _rect = nullptr;
    Gtk::Widget& _main;
    Inkscape::UI::Widget::SpinButton& _width;
    Inkscape::UI::Widget::SpinButton& _height;
    Inkscape::UI::Widget::SpinButton& _rx;
    Inkscape::UI::Widget::SpinButton& _ry;
    Gtk::Button& _sharp;
    Gtk::Button& _round;
};

///////////////////////////////////////////////////////////////////////////////

class EllipsePanel : public details::AttributesPanel {
public:
    EllipsePanel(Glib::RefPtr<Gtk::Builder> builder) :
        _main(get_widget<Gtk::Grid>(builder, "ellipse-main")),
        _rx(get_derived_widget<Inkscape::UI::Widget::SpinButton>(builder, "el-rx")),
        _ry(get_derived_widget<Inkscape::UI::Widget::SpinButton>(builder, "el-ry")),
        _start(get_derived_widget<Inkscape::UI::Widget::SpinButton>(builder, "el-start")),
        _end(get_derived_widget<Inkscape::UI::Widget::SpinButton>(builder, "el-end")),
        _slice(get_widget<Gtk::ToggleButton>(builder, "el-slice")),
        _arc(get_widget<Gtk::ToggleButton>(builder, "el-arc")),
        _chord(get_widget<Gtk::ToggleButton>(builder, "el-chord")),
        _whole(get_widget<Gtk::Button>(builder, "el-whole"))
    {
        _title = _("Ellipse");
        _widget = &_main;

        _type[0] = &_slice;
        _type[1] = &_arc;
        _type[2] = &_chord;

        int type = 0;
        for (auto btn : _type) {
            btn->signal_toggled().connect([type, this](){ set_type(type); });
            type++;
        }

        _whole.signal_clicked().connect([this](){
            _start.get_adjustment()->set_value(0);
            _end.get_adjustment()->set_value(0);
        });

        auto normalize = [this](){
            _ellipse->normalize();
            _ellipse->updateRepr();
            _ellipse->requestDisplayUpdate(SP_OBJECT_MODIFIED_FLAG);
        };

        _rx.get_adjustment()->signal_value_changed().connect([=, this](){
            change_value_px(_ellipse, _rx.get_adjustment(), nullptr, [=, this](double rx){ _ellipse->setVisibleRx(rx); normalize(); });
        });
        _ry.get_adjustment()->signal_value_changed().connect([=, this](){
            change_value_px(_ellipse, _ry.get_adjustment(), nullptr, [=, this](double ry){ _ellipse->setVisibleRy(ry); normalize(); });
        });
        _start.get_adjustment()->signal_value_changed().connect([=, this](){
            change_angle(_ellipse, _start.get_adjustment(), [=, this](double s){ _ellipse->start = s; normalize(); });
        });
        _end.get_adjustment()->signal_value_changed().connect([=, this](){
            change_angle(_ellipse, _end.get_adjustment(), [=, this](double e){ _ellipse->end = e; normalize(); });
        });

        get_widget<Gtk::Button>(builder, "el-round").signal_clicked().connect([this](){
            auto [changed, x, y] = round_values(_rx, _ry);
            if (changed && x > 0 && y > 0) {
                _rx.get_adjustment()->set_value(x);
                _ry.get_adjustment()->set_value(y);
            }
        });
    }

    ~EllipsePanel() override = default;

    void update(SPObject* object) override {
        _ellipse = cast<SPGenericEllipse>(object);
        if (!_ellipse) return;

        auto scoped(_update.block());
        _rx.set_value(_ellipse->rx.value);
        _ry.set_value(_ellipse->ry.value);
        _start.set_value(radians_to_degree_mod360(_ellipse->start));
        _end.set_value(radians_to_degree_mod360(_ellipse->end));

        _slice.set_active(_ellipse->arc_type == SP_GENERIC_ELLIPSE_ARC_TYPE_SLICE);
        _arc.set_active(_ellipse->arc_type == SP_GENERIC_ELLIPSE_ARC_TYPE_ARC);
        _chord.set_active(_ellipse->arc_type == SP_GENERIC_ELLIPSE_ARC_TYPE_CHORD);

        auto slice = !_ellipse->is_whole();
        _whole.set_sensitive(slice);
        for (auto btn : _type) {
            btn->set_sensitive(slice);
        }
    }

    void set_type(int type) {
        if (!_ellipse) return;

        auto scoped(_update.block());

        Glib::ustring arc_type = "slice";
        bool open = false;
        switch (type) {
            case 0:
                arc_type = "slice";
                open = false;
                break;
            case 1:
                arc_type = "arc";
                open = true;
                break;
            case 2:
                arc_type = "chord";
                open = true; // For backward compat, not truly open but chord most like arc.
                break;
            default:
                std::cerr << "Ellipse type change - bad arc type: " << type << std::endl;
                break;
        }
        _ellipse->setAttribute("sodipodi:open", open ? "true" : nullptr);
        _ellipse->setAttribute("sodipodi:arc-type", arc_type.c_str());
        _ellipse->updateRepr();
        DocumentUndo::done(_ellipse->document, _("Change arc type"), INKSCAPE_ICON("draw-ellipse"));
    }

private:
    SPGenericEllipse* _ellipse = nullptr;
    Gtk::Widget& _main;
    Inkscape::UI::Widget::SpinButton& _rx;
    Inkscape::UI::Widget::SpinButton& _ry;
    Inkscape::UI::Widget::SpinButton& _start;
    Inkscape::UI::Widget::SpinButton& _end;
    Gtk::ToggleButton &_slice;
    Gtk::ToggleButton &_arc;
    Gtk::ToggleButton &_chord;
    Gtk::Button& _whole;
    Gtk::ToggleButton *_type[3];
};

///////////////////////////////////////////////////////////////////////////////

class StarPanel : public details::AttributesPanel {
public:
    StarPanel(Glib::RefPtr<Gtk::Builder> builder) :
        _main(get_widget<Gtk::Grid>(builder, "star-main")),
        _corners(get_derived_widget<Inkscape::UI::Widget::SpinButton>(builder, "star-corners")),
        _ratio(get_derived_widget<Inkscape::UI::Widget::SpinButton>(builder, "star-ratio")),
        _rounded(get_derived_widget<Inkscape::UI::Widget::SpinButton>(builder, "star-rounded")),
        _rand(get_derived_widget<Inkscape::UI::Widget::SpinButton>(builder, "star-rand")),
        _poly(get_widget<Gtk::ToggleButton>(builder, "star-poly")),
        _star(get_widget<Gtk::ToggleButton>(builder, "star-star")),
        _align(get_widget<Gtk::Button>(builder, "star-align")),
        _clear_rnd(get_widget<Gtk::Button>(builder, "star-rnd-clear")),
        _clear_round(get_widget<Gtk::Button>(builder, "star-round-clear")),
        _clear_ratio(get_widget<Gtk::Button>(builder, "star-ratio-clear"))
    {
        _title = _("Star");
        _widget = &_main;

        _corners.get_adjustment()->signal_value_changed().connect([this](){
            change_value(_path, _corners.get_adjustment(), [this](double sides) {
                _path->setAttributeDouble("sodipodi:sides", (int)sides);
                auto arg1 = get_number(_path, "sodipodi:arg1").value_or(0.5);
                _path->setAttributeDouble("sodipodi:arg2", arg1 + M_PI / sides);
                _path->updateRepr();
            });
        });
        _rounded.get_adjustment()->signal_value_changed().connect([this](){
            change_value(_path, _rounded.get_adjustment(), [this](double rounded) {
                _path->setAttributeDouble("inkscape:rounded", rounded);
                _path->updateRepr();
            });
        });
        _ratio.get_adjustment()->signal_value_changed().connect([this](){
            change_value(_path, _ratio.get_adjustment(), [this](double ratio){
                auto r1 = get_number(_path, "sodipodi:r1").value_or(1.0);
                auto r2 = get_number(_path, "sodipodi:r2").value_or(1.0);
                if (r2 < r1) {
                    _path->setAttributeDouble("sodipodi:r2", r1 * ratio);
                } else {
                    _path->setAttributeDouble("sodipodi:r1", r2 * ratio);
                }
                _path->updateRepr();
            });
        });
        _rand.get_adjustment()->signal_value_changed().connect([this](){
            change_value(_path, _rand.get_adjustment(), [this](double rnd){
                _path->setAttributeDouble("inkscape:randomized", rnd);
                _path->updateRepr();
            });
        });

        _clear_rnd.signal_clicked().connect([this](){ _rand.get_adjustment()->set_value(0); });
        _clear_round.signal_clicked().connect([this](){ _rounded.get_adjustment()->set_value(0); });
        _clear_ratio.signal_clicked().connect([this](){ _ratio.get_adjustment()->set_value(0.5); });

        _poly.signal_toggled().connect([this](){ set_flat(true); });
        _star.signal_toggled().connect([this](){ set_flat(false); });
        _align.signal_clicked().connect([this](){
            change_value(_path, {}, [this](double) { align_star_shape(_path); });
        });
    }

    ~StarPanel() override = default;

    void update(SPObject* object) override {
        _path = cast<SPStar>(object);
        if (!_path) return;

        auto scoped(_update.block());
        _corners.set_value(_path->sides);
        double r1 = get_number(_path, "sodipodi:r1").value_or(0.5);
        double r2 = get_number(_path, "sodipodi:r2").value_or(0.5);
        if (r2 < r1) {
            _ratio.set_value(r1 > 0 ? r2 / r1 : 0.5);
        } else {
            _ratio.set_value(r2 > 0 ? r1 / r2 : 0.5);
        }
        _rounded.set_value(_path->rounded);
        _rand.set_value(_path->randomized);

        _clear_rnd  .set_visible(_path->randomized != 0);
        _clear_round.set_visible(_path->rounded != 0);
        _clear_ratio.set_visible(std::abs(_ratio.get_value() - 0.5) > 0.0005);

        _poly.set_active(_path->flatsided);
        _star.set_active(!_path->flatsided);
    }

    void set_flat(bool flat) {
        change_value(_path, {}, [flat, this](double){
            _path->setAttribute("inkscape:flatsided", flat ? "true" : "false");
            _path->updateRepr();
        });
        // adjust corners/sides
        _corners.get_adjustment()->set_lower(flat ? 3 : 2);
        if (flat && _corners.get_value() < 3) {
            _corners.get_adjustment()->set_value(3);
        }
    }

private:
    SPStar* _path = nullptr;
    Gtk::Widget& _main;
    Inkscape::UI::Widget::SpinButton& _corners;
    Inkscape::UI::Widget::SpinButton& _ratio;
    Inkscape::UI::Widget::SpinButton& _rounded;
    Inkscape::UI::Widget::SpinButton& _rand;
    Gtk::Button& _clear_rnd;
    Gtk::Button& _clear_round;
    Gtk::Button& _clear_ratio;
    Gtk::Button& _align;
    Gtk::ToggleButton &_poly;
    Gtk::ToggleButton &_star;
};

///////////////////////////////////////////////////////////////////////////////

class TextPanel : public details::AttributesPanel {
public:
    TextPanel(Glib::RefPtr<Gtk::Builder> builder) :
        _main(get_widget<Gtk::Grid>(builder, "text-main"))
    {
        // TODO - text panel
    }

private:
    Gtk::Widget& _main;

};

///////////////////////////////////////////////////////////////////////////////

class PathPanel : public details::AttributesPanel {
public:
    PathPanel(Glib::RefPtr<Gtk::Builder> builder) :
        _main(get_widget<Gtk::Grid>(builder, "path-main")),
        _width(get_derived_widget<Inkscape::UI::Widget::SpinButton>(builder, "path-width")),
        _height(get_derived_widget<Inkscape::UI::Widget::SpinButton>(builder, "path-height")),
        _x(get_derived_widget<Inkscape::UI::Widget::SpinButton>(builder, "path-x")),
        _y(get_derived_widget<Inkscape::UI::Widget::SpinButton>(builder, "path-y")),
        _info(get_widget<Gtk::Label>(builder, "path-info")),
        _data(_svgd_edit->getTextView())
    {
        _title = _("Path");
        _widget = &_main;

        //TODO: do we need to duplicate x/y/w/h toolbar widgets here?
        /*
        _width.get_adjustment()->signal_value_changed().connect([=](){
        });
        _height.get_adjustment()->signal_value_changed().connect([=](){
        });
        _x.get_adjustment()->signal_value_changed().connect([=](){
        });
        _y.get_adjustment()->signal_value_changed().connect([=](){
        });
        */
        auto pref_path = dlg_pref_path + "path-panel/";

        auto theme = Inkscape::Preferences::get()->getString("/theme/syntax-color-theme", "-none-");
        _svgd_edit->setStyle(theme);
        _data.set_wrap_mode(Gtk::WrapMode::WORD);

        auto const key = Gtk::EventControllerKey::create();
        key->signal_key_pressed().connect(sigc::mem_fun(*this, &PathPanel::on_key_pressed), true);
        _data.add_controller(key);

        auto& wnd = get_widget<Gtk::ScrolledWindow>(builder, "path-data-wnd");
        wnd.set_child(_data);

        auto set_precision = [=, this](int const n) {
            _precision = n;
            auto& menu_button = get_widget<Gtk::MenuButton>(builder, "path-menu");
            auto const menu = menu_button.get_menu_model();
            auto const section = menu->get_item_link(0, Gio::MenuModel::Link::SECTION);
            auto const type = Glib::VariantType{g_variant_type_new("s")};
            auto const variant = section->get_item_attribute(n, Gio::MenuModel::Attribute::LABEL, type);
            auto const label = ' ' + static_cast<Glib::Variant<Glib::ustring> const &>(variant).get();
            get_widget<Gtk::Label>(builder, "path-precision").set_label(label);
            Inkscape::Preferences::get()->setInt(pref_path + "precision", n);
            menu_button.set_active(false);
        };

        const int N = 5;
        _precision = Inkscape::Preferences::get()->getIntLimited(pref_path + "precision", 2, 0, N);
        set_precision(_precision);
        auto group = Gio::SimpleActionGroup::create();
        auto action = group->add_action_radio_integer("precision", _precision);
        action->property_state().signal_changed().connect([=, this]{ int n; action->get_state(n);
                                                                    set_precision(n); });
        _main.insert_action_group("attrdialog", std::move(group));

        get_widget<Gtk::Button>(builder, "path-data-round").signal_clicked().connect([this]{
            truncate_digits(_data.get_buffer(), _precision);
            commit_d();
        });
        get_widget<Gtk::Button>(builder, "path-enter").signal_clicked().connect([this](){ commit_d(); });
    }

    ~PathPanel() override = default;

    void update(SPObject* object) override {
        _path = cast<SPPath>(object);
        if (!_path) return;

        auto scoped(_update.block());

        auto d = _path->getAttribute("inkscape:original-d");
        if (d && _path->hasPathEffect()) {
            _original = true;
        }
        else {
            _original = false;
            d = _path->getAttribute("d");
        }
        _svgd_edit->setText(d ? d : "");

        auto curve = _path->curveBeforeLPE();
        if (!curve) curve = _path->curve();
        size_t node_count = 0;
        if (curve) {
            node_count = curve->get_segment_count();
        }
        _info.set_text(_("Nodes: ") + std::to_string(node_count));

        //TODO: we can consider adding more stats, like perimeter, area, etc.
    }

private:
    bool on_key_pressed(unsigned keyval, unsigned keycode, Gdk::ModifierType state) {
        switch (keyval) {
        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter:
            return Controller::has_flag(state, Gdk::ModifierType::SHIFT_MASK) ? commit_d() : false;
        }
        return false;
    }

    bool commit_d() {
        if (!_path || !_data.is_visible()) return false;

        auto scoped(_update.block());
        auto d = _svgd_edit->getText();
        _path->setAttribute(_original ? "inkscape:original-d" : "d", d);
        DocumentUndo::maybeDone(_path->document, "path-data", _("Change path"), INKSCAPE_ICON(""));
        return true;
    }

    SPPath* _path = nullptr;
    bool _original = false;
    Gtk::Widget& _main;
    Inkscape::UI::Widget::SpinButton& _width;
    Inkscape::UI::Widget::SpinButton& _height;
    Inkscape::UI::Widget::SpinButton& _x;
    Inkscape::UI::Widget::SpinButton& _y;
    Gtk::Label& _info;
    std::unique_ptr<Syntax::TextEditView> _svgd_edit = Syntax::TextEditView::create(Syntax::SyntaxMode::SvgPathData);
    Gtk::TextView& _data;
    int _precision = 2;
};

///////////////////////////////////////////////////////////////////////////////

std::string get_key(SPObject* object) {
    if (!object) return {};

    return typeid(*object).name();
}

details::AttributesPanel* ObjectAttributes::get_panel(SPObject* object) {
    if (!object) return nullptr;

    std::string name = get_key(object);
    auto it = _panels.find(name);
    return it == _panels.end() ? nullptr : it->second.get();
}

void ObjectAttributes::create_panels() {
    _panels[typeid(SPImage).name()] = std::make_unique<ImagePanel>();
    _panels[typeid(SPRect).name()] = std::make_unique<RectPanel>(_builder);
    _panels[typeid(SPGenericEllipse).name()] = std::make_unique<EllipsePanel>(_builder);
    _panels[typeid(SPStar).name()] = std::make_unique<StarPanel>(_builder);
    _panels[typeid(SPAnchor).name()] = std::make_unique<AnchorPanel>();
    _panels[typeid(SPPath).name()] = std::make_unique<PathPanel>(_builder);
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
