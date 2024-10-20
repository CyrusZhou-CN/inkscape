// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * Erasor aux toolbar
 */
/* Authors:
 *   MenTaLguY <mental@rydia.net>
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   bulia byak <buliabyak@users.sf.net>
 *   Frank Felfe <innerspace@iname.com>
 *   John Cliff <simarilius@yahoo.com>
 *   David Turner <novalis@gnu.org>
 *   Josh Andler <scislac@scislac.com>
 *   Jon A. Cruz <jon@joncruz.org>
 *   Maximilian Albert <maximilian.albert@gmail.com>
 *   Tavmjong Bah <tavmjong@free.fr>
 *   Abhishek Sharma
 *   Kris De Gussem <Kris.DeGussem@gmail.com>
 *   Vaibhav Malik <vaibhavmalik2018@gmail.com>
 *
 * Copyright (C) 2004 David Turner
 * Copyright (C) 2003 MenTaLguY
 * Copyright (C) 1999-2011 authors
 * Copyright (C) 2001-2002 Ximian, Inc.
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "eraser-toolbar.h"

#include <glibmm/i18n.h>
#include <gtkmm/adjustment.h>
#include <gtkmm/togglebutton.h>

#include "desktop.h"
#include "document-undo.h"
#include "ui/builder-utils.h"
#include "ui/simple-pref-pusher.h"
#include "ui/tools/eraser-tool.h"
#include "ui/util.h"
#include "ui/widget/canvas.h"  // Focus widget
#include "ui/widget/spinbutton.h"

using Inkscape::DocumentUndo;

namespace Inkscape::UI::Toolbar {

EraserToolbar::EraserToolbar(SPDesktop *desktop)
    : Toolbar(desktop)
    , _builder(create_builder("toolbar-eraser.ui"))
    , _width_item(get_derived_widget<UI::Widget::SpinButton>(_builder, "_width_item"))
    , _thinning_item(get_derived_widget<UI::Widget::SpinButton>(_builder, "_thinning_item"))
    , _cap_rounding_item(get_derived_widget<UI::Widget::SpinButton>(_builder, "_cap_rounding_item"))
    , _tremor_item(get_derived_widget<UI::Widget::SpinButton>(_builder, "_tremor_item"))
    , _mass_item(get_derived_widget<UI::Widget::SpinButton>(_builder, "_mass_item"))
    , _usepressure_btn(&get_widget<Gtk::ToggleButton>(_builder, "_usepressure_btn"))
    , _split_btn(get_widget<Gtk::ToggleButton>(_builder, "_split_btn"))
{
    auto prefs = Inkscape::Preferences::get();
    int const eraser_mode = prefs->getInt("/tools/eraser/mode", _modeAsInt(Tools::DEFAULT_ERASER_MODE));

    _toolbar = &get_widget<Gtk::Box>(_builder, "eraser-toolbar");

    // Setup the spin buttons.
    setup_derived_spin_button(_width_item, "width", 15, &EraserToolbar::width_value_changed);
    setup_derived_spin_button(_thinning_item, "thinning", 10, &EraserToolbar::velthin_value_changed);
    setup_derived_spin_button(_cap_rounding_item, "cap_rounding", 0.0, &EraserToolbar::cap_rounding_value_changed);
    setup_derived_spin_button(_tremor_item, "tremor", 0.0, &EraserToolbar::tremor_value_changed);
    setup_derived_spin_button(_mass_item, "mass", 10, &EraserToolbar::mass_value_changed);

     _width_item.set_custom_numeric_menu_data({
        {  0, _("(no width)")},
        {  1, _("(hairline)")},
        {  3, ""},
        {  5, ""},
        { 10, ""},
        { 15, _("(default)")},
        { 20, ""},
        { 30, ""},
        { 50, ""},
        { 75, ""},
        {100, _("(broad stroke)")}
    });

     _thinning_item.set_custom_numeric_menu_data({
        {-100, _("(speed blows up stroke)")},
        { -40, ""},
        { -20, ""},
        { -10, _("(slight widening)")},
        {   0, _("(constant width)")},
        {  10, _("(slight thinning, default)")},
        {  20, ""},
        {  40, ""},
        { 100, _("(speed deflates stroke)")}
    });

    _cap_rounding_item.set_custom_numeric_menu_data({
        {  0, _("(blunt caps, default)")},
        {0.3, _("(slightly bulging)")},
        {0.5, ""},
        {1.0, ""},
        {1.4, _("(approximately round)")},
        {5.0, _("(long protruding caps)")}
    });

     _tremor_item.set_custom_numeric_menu_data({
        {  0, _("(smooth line)")},
        { 10, _("(slight tremor)")},
        { 20, _("(noticeable tremor)")},
        { 40, ""},
        { 60, ""},
        {100, _("(maximum tremor)")}
    });

     _mass_item.set_custom_numeric_menu_data({
        {  0, _("(no inertia)")},
        {  2, _("(slight smoothing, default)")},
        { 10, _("(noticeable lagging)")},
        { 20, ""},
        { 50, ""},
        {100, _("(maximum inertia)")}
    });

    // Configure mode buttons
    int btn_index = 0;
    for_each_child(get_widget<Gtk::Box>(_builder, "mode_buttons_box"), [&](Gtk::Widget &item){
        auto &btn = dynamic_cast<Gtk::ToggleButton &>(item);
        btn.set_active(btn_index == eraser_mode);
        btn.signal_clicked().connect(sigc::bind(sigc::mem_fun(*this, &EraserToolbar::mode_changed), btn_index++));
        return ForEachResult::_continue;
    });

    // Pressure button
    _pressure_pusher.reset(new UI::SimplePrefPusher(_usepressure_btn, "/tools/eraser/usepressure"));

    // Split button
    _split_btn.set_active(prefs->getBool("/tools/eraser/break_apart", false));

    set_child(*_toolbar);
    init_menu_btns();

    // Signals.
    _usepressure_btn->signal_toggled().connect(sigc::mem_fun(*this, &EraserToolbar::usepressure_toggled));
    _split_btn.signal_toggled().connect(sigc::mem_fun(*this, &EraserToolbar::toggle_break_apart));

    set_eraser_mode_visibility(eraser_mode);
}

EraserToolbar::~EraserToolbar() = default;

void EraserToolbar::setup_derived_spin_button(UI::Widget::SpinButton &btn, Glib::ustring const &name,
                                              double default_value, ValueChangedMemFun const value_changed_mem_fun)
{
    const Glib::ustring path = "/tools/eraser/" + name;
    auto const val = Preferences::get()->getDouble(path, default_value);

    auto adj = btn.get_adjustment();
    adj->set_value(val);
    adj->signal_value_changed().connect(sigc::mem_fun(*this, value_changed_mem_fun));

    btn.set_defocus_widget(_desktop->getCanvas());
}

/**
 * @brief Computes the integer value representing eraser mode
 * @param mode A mode of the eraser tool, from the enum EraserToolMode
 * @return the integer to be stored in the prefs as the selected mode
 */
unsigned EraserToolbar::_modeAsInt(Tools::EraserToolMode const mode)
{
    using namespace Inkscape::UI::Tools;

    if (mode == EraserToolMode::DELETE) {
        return 0;
    } else if (mode == EraserToolMode::CUT) {
        return 1;
    } else if (mode == EraserToolMode::CLIP) {
        return 2;
    } else {
        return _modeAsInt(DEFAULT_ERASER_MODE);
    }
}

void EraserToolbar::mode_changed(int mode)
{
    if (DocumentUndo::getUndoSensitive(_desktop->getDocument())) {
        Preferences::get()->setInt("/tools/eraser/mode", mode);
    }

    set_eraser_mode_visibility(mode);

    // only take action if run by the attr_changed listener
    if (!_freeze) {
        // in turn, prevent listener from responding
        _freeze = true;

        /*
        if ( eraser_mode != ERASER_MODE_DELETE ) {
        } else {
        }
        */
        // TODO finish implementation

        _freeze =  false;
    }
}

void EraserToolbar::set_eraser_mode_visibility(unsigned const eraser_mode)
{
    using namespace Inkscape::UI::Tools;

    bool const visibility = eraser_mode != _modeAsInt(EraserToolMode::DELETE);
    auto children = UI::get_children(*_toolbar);
    const int visible_children_count = 2;

    // Set all the children except the modes as invisible.
    int child_index = 0;
    for (auto child : children) {
        if (child_index++ < visible_children_count) {
            continue;
        }

        child->set_visible(visibility);
    }

    _split_btn.set_visible(eraser_mode == _modeAsInt(EraserToolMode::CUT));
}

void EraserToolbar::width_value_changed()
{
    Preferences::get()->setDouble("/tools/eraser/width", _width_item.get_adjustment()->get_value());
}

void EraserToolbar::mass_value_changed()
{
    Preferences::get()->setDouble("/tools/eraser/mass", _mass_item.get_adjustment()->get_value());
}

void EraserToolbar::velthin_value_changed()
{
    Preferences::get()->setDouble("/tools/eraser/thinning", _thinning_item.get_adjustment()->get_value());
}

void EraserToolbar::cap_rounding_value_changed()
{
    Preferences::get()->setDouble("/tools/eraser/cap_rounding", _cap_rounding_item.get_adjustment()->get_value());
}

void EraserToolbar::tremor_value_changed()
{
    Preferences::get()->setDouble("/tools/eraser/tremor", _tremor_item.get_adjustment()->get_value());
}

void EraserToolbar::toggle_break_apart()
{
    bool active = _split_btn.get_active();
    Preferences::get()->setBool("/tools/eraser/break_apart", active);
}

void EraserToolbar::usepressure_toggled()
{
    Preferences::get()->setBool("/tools/eraser/usepressure", _usepressure_btn->get_active());
}

} // namespace Inkscape::UI::Toolbar

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
