// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Author:
 *   Johan B. C. Engelen
 *
 * Copyright (C) 2011 Author
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "spinbutton.h"

#include <sigc++/functors/mem_fun.h>

#include "scroll-utils.h"
#include "ui/controller.h"
#include "ui/tools/tool-base.h"
#include "unit-menu.h"
#include "unit-tracker.h"
#include "util/expression-evaluator.h"

namespace Inkscape::UI::Widget {

MathSpinButton::MathSpinButton(BaseObjectType *cobject, const Glib::RefPtr<Gtk::Builder> &refGlade)
    : Gtk::SpinButton(cobject)
{
}

int MathSpinButton::on_input(double *newvalue)
{
    try {
        auto eval = Inkscape::Util::ExpressionEvaluator(get_text().c_str(), nullptr);
        auto result = eval.evaluate();
        *newvalue = result.value;
    } catch (Inkscape::Util::EvaluatorException const &e) {
        g_message ("%s", e.what());
        return false;
    }
    return true;
}

void SpinButton::_construct()
{
    Controller::add_key<&SpinButton::on_key_pressed>(*this, *this);

    property_has_focus().signal_changed().connect(
        sigc::mem_fun(*this, &SpinButton::on_has_focus_changed));
}

int SpinButton::on_input(double* newvalue)
{
    if (_dont_evaluate) return false;

    try {
        Inkscape::Util::EvaluatorQuantity result;
        if (_unit_menu || _unit_tracker) {
            Unit const *unit = nullptr;
            if (_unit_menu) {
                unit = _unit_menu->getUnit();
            } else {
                unit = _unit_tracker->getActiveUnit();
            }
            Inkscape::Util::ExpressionEvaluator eval = Inkscape::Util::ExpressionEvaluator(get_text().c_str(), unit);
            result = eval.evaluate();
            // check if output dimension corresponds to input unit
            if (result.dimension != (unit->isAbsolute() ? 1 : 0) ) {
                throw Inkscape::Util::EvaluatorException("Input dimensions do not match with parameter dimensions.","");
            }
        } else {
            Inkscape::Util::ExpressionEvaluator eval = Inkscape::Util::ExpressionEvaluator(get_text().c_str(), nullptr);
            result = eval.evaluate();
        }
        *newvalue = result.value;
    } catch (Inkscape::Util::EvaluatorException const &e) {
        g_message ("%s", e.what());
        return false;
    }

    return true;
}

void SpinButton::on_has_focus_changed()
{
    if (has_focus()) {
        _on_focus_in_value = get_value();
    }
}

bool SpinButton::on_key_pressed(GtkEventControllerKey const * const controller,
                                unsigned const keyval, unsigned const keycode,
                                GdkModifierType const state)
{
    switch (Inkscape::UI::Tools::get_latin_keyval(controller, keyval, keycode, state)) {
        case GDK_KEY_Escape: // defocus
            undo();
            defocus();
            break;

        case GDK_KEY_Return: // defocus
        case GDK_KEY_KP_Enter:
            defocus();
            break;

        case GDK_KEY_Tab:
        case GDK_KEY_ISO_Left_Tab:
            // set the flag meaning "do not leave toolbar when changing value"
            _stay = true;
            break;

        case GDK_KEY_z:
        case GDK_KEY_Z:
            if (Controller::has_flag(state, GDK_CONTROL_MASK)) {
                _stay = true;
                undo();
                return true; // I consumed the event
            }
            break;

        default:
            break;
    }

    return false;
}

void SpinButton::undo()
{
    set_value(_on_focus_in_value);
}

void SpinButton::defocus()
{
    // defocus spinbutton by moving focus to the canvas, unless "stay" is on
    if (_stay) {
        _stay = false;
    } else {
        Gtk::Widget *widget = _defocus_widget ? _defocus_widget : get_scrollable_ancestor(this);
        if (widget) {
            widget->grab_focus();
        }
    }
}

} // namespace Inkscape::UI::Widget

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
