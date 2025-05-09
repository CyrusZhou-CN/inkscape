// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2005-2007 Authors:
 *   Ted Gould <ted@gould.cx>
 *   Johan Engelen <johan@shouraizou.nl> *
 *   Jon A. Cruz <jon@joncruz.org>
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "parameter-float.h"

#include <iomanip>

#include <gtkmm/adjustment.h>
#include <gtkmm/box.h>
#include <gtkmm/label.h>

#include "extension/extension.h"
#include "preferences.h"
#include "ui/pack.h"
#include "ui/widget/spinbutton.h"
#include "ui/widget/spin-scale.h"
#include "util-string/ustring-format.h"
#include "xml/node.h"

namespace Inkscape::Extension {

ParamFloat::ParamFloat(Inkscape::XML::Node *xml, Inkscape::Extension::Extension *ext)
    : InxParameter(xml, ext)
{
    // get value
    if (xml->firstChild()) {
        const char *value = xml->firstChild()->content();
        if (value)
            string_to_value(value);
    }

    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    _value = prefs->getDouble(pref_name(), _value);

    // parse and apply limits
    const char *min = xml->attribute("min");
    if (min) {
        _min = g_ascii_strtod(min, nullptr);
    }

    const char *max = xml->attribute("max");
    if (max) {
        _max = g_ascii_strtod(max, nullptr);
    }

    if (_value < _min) {
        _value = _min;
    }

    if (_value > _max) {
        _value = _max;
    }

    // parse precision
    const char *precision = xml->attribute("precision");
    if (precision != nullptr) {
        _precision = strtol(precision, nullptr, 0);
    }


    // parse appearance
    if (_appearance) {
        if (!strcmp(_appearance, "full")) {
            _mode = FULL;
        } else {
            g_warning("Invalid value ('%s') for appearance of parameter '%s' in extension '%s'",
                      _appearance, _name, _extension->get_id());
        }
    }
}

/**
 * A function to set the \c _value.
 *
 * This function sets the internal value, but it also sets the value
 * in the preferences structure.  To put it in the right place \c pref_name() is used.
 *
 * @param  in   The value to set to.
 */
double ParamFloat::set(double in)
{
    _value = in;
    if (_value > _max) {
        _value = _max;
    }
    if (_value < _min) {
        _value = _min;
    }

    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    prefs->setDouble(pref_name(), _value);

    return _value;
}

std::string ParamFloat::value_to_string() const
{
    return Inkscape::ustring::format_classic(std::setprecision(_precision), std::fixed, _value);
}

void ParamFloat::string_to_value(const std::string &in)
{
    _value = g_ascii_strtod(in.c_str(), nullptr);
}

/** A class to make an adjustment that uses Extension params. */
class ParamFloatAdjustment : public Gtk::Adjustment {
    /** The parameter to adjust. */
    ParamFloat *_pref;
    sigc::signal<void ()> *_changeSignal;
protected:
    /** Make the adjustment using an extension and the string
                describing the parameter. */
    ParamFloatAdjustment(ParamFloat *param, sigc::signal<void ()> *changeSignal)
        : Gtk::Adjustment(0.0, param->min(), param->max(), 0.1, 1.0, 0)
        , _pref(param)
        , _changeSignal(changeSignal) {
        this->set_value(_pref->get());
        this->signal_value_changed().connect(sigc::mem_fun(*this, &ParamFloatAdjustment::val_changed));
        return;
    };

    void val_changed ();

public:
    static Glib::RefPtr<ParamFloatAdjustment> create(ParamFloat* param, sigc::signal<void ()>* changeSignal) {
        return Glib::make_refptr_for_instance(new ParamFloatAdjustment(param, changeSignal));
    }

}; /* class ParamFloatAdjustment */

/**
 * A function to respond to the value_changed signal from the adjustment.
 *
 * This function just grabs the value from the adjustment and writes
 * it to the parameter.  Very simple, but yet beautiful.
 */
void ParamFloatAdjustment::val_changed()
{
    _pref->set(this->get_value());
    if (_changeSignal != nullptr) {
        _changeSignal->emit();
    }
    return;
}

/**
 * Creates a Float Adjustment for a float parameter.
 *
 * Builds a hbox with a label and a float adjustment in it.
 */
Gtk::Widget *ParamFloat::get_widget(sigc::signal<void ()> *changeSignal)
{
    if (_hidden) {
        return nullptr;
    }

    auto const hbox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, GUI_PARAM_WIDGETS_SPACING);

    auto fadjust = ParamFloatAdjustment::create(this, changeSignal);

    if (_mode == FULL) {

        Glib::ustring text;
        if (_text != nullptr)
            text = _text;
        auto const scale = Gtk::make_managed<UI::Widget::SpinScale>(text, fadjust, _precision);
        scale->set_size_request(400, -1);
        scale->set_visible(true);
        UI::pack_start(*hbox, *scale, true, true);

    }
    else if (_mode == DEFAULT) {

        auto const label = Gtk::make_managed<Gtk::Label>(_text, Gtk::Align::START);
        label->set_visible(true);
        UI::pack_start(*hbox, *label, true, true);

        auto const spin = Gtk::make_managed<Inkscape::UI::Widget::SpinButton>(fadjust, 0.1, _precision);
        spin->set_visible(true);
        UI::pack_start(*hbox, *spin, false, false);
    }

    hbox->set_visible(true);
    return hbox;
}

} // namespace Inkscape::Extension

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4 :
