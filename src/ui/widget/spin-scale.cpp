// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Author:
 *
 * Copyright (C) 2007 Nicholas Bishop <nicholasbishop@gmail.com>
 *               2008 Felipe C. da S. Sanches <juca@members.fsf.org>
 *               2017 Tavmjong Bah
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

/*
 * Derived from and replaces SpinSlider
 */

#include "spin-scale.h"

#include <glibmm/i18n.h>
#include <glibmm/stringutils.h>
#include <gtkmm/enums.h>

namespace Inkscape {
namespace UI {
namespace Widget {

SpinScale::SpinScale(const Glib::ustring label, double value,
                     double lower, double upper,
                     double step_increment, double page_increment, int digits,
                     const SPAttr a, const Glib::ustring tip_text)
    : AttrWidget(a, value)
    , _inkspinscale(value, lower, upper, step_increment, page_increment, 0)
{
    set_name("SpinScale");
    _inkspinscale.drag_dest_unset();
    _inkspinscale.set_label (label);
    _inkspinscale.set_digits (digits);
    _inkspinscale.set_tooltip_text (tip_text);

    _adjustment = _inkspinscale.get_adjustment();

    signal_value_changed().connect(signal_attr_changed().make_slot());

    pack_start(_inkspinscale);

    show_all_children();
}

SpinScale::SpinScale(const Glib::ustring label,
                     Glib::RefPtr<Gtk::Adjustment> adjustment, int digits,
                     const SPAttr a, const Glib::ustring tip_text)
    : AttrWidget(a, 0.0)
    , _inkspinscale(adjustment)
{
    set_name("SpinScale");

    _inkspinscale.set_label (label);
    _inkspinscale.set_digits (digits);
    _inkspinscale.set_tooltip_text (tip_text);

    _adjustment = _inkspinscale.get_adjustment();

    signal_value_changed().connect(signal_attr_changed().make_slot());

    pack_start(_inkspinscale);

    show_all_children();
}

Glib::ustring SpinScale::get_as_attribute() const
{
    const double val = _adjustment->get_value();

    if( _inkspinscale.get_digits() == 0)
        return Glib::Ascii::dtostr((int)val);
    else
        return Glib::Ascii::dtostr(val);
}

void SpinScale::set_from_attribute(SPObject* o)
{
    const gchar* val = attribute_value(o);
    if (val)
        _adjustment->set_value(Glib::Ascii::strtod(val));
    else
        _adjustment->set_value(get_default()->as_double());
}

Glib::SignalProxy<void> SpinScale::signal_value_changed()
{
    return _adjustment->signal_value_changed();
}

double SpinScale::get_value() const
{
    return _adjustment->get_value();
}

void SpinScale::set_value(const double val)
{
    _adjustment->set_value(val);
}

void SpinScale::set_focuswidget(GtkWidget *widget)
{
    _inkspinscale.set_focus_widget(widget);
}

const decltype(SpinScale::_adjustment) SpinScale::get_adjustment() const
{
    return _adjustment;
}

decltype(SpinScale::_adjustment) SpinScale::get_adjustment()
{
    return _adjustment;
}


DualSpinScale::DualSpinScale(const Glib::ustring label1, const Glib::ustring label2,
                             double value, double lower, double upper,
                             double step_increment, double page_increment, int digits,
                             const SPAttr a,
                             const Glib::ustring tip_text1, const Glib::ustring tip_text2)
    : AttrWidget(a),
      _s1(label1, value, lower, upper, step_increment, page_increment, digits, SPAttr::INVALID, tip_text1),
      _s2(label2, value, lower, upper, step_increment, page_increment, digits, SPAttr::INVALID, tip_text2)
{
    set_name("DualSpinScale");
    signal_value_changed().connect(signal_attr_changed().make_slot());

    _s1.get_adjustment()->signal_value_changed().connect(_signal_value_changed.make_slot());
    _s2.get_adjustment()->signal_value_changed().connect(_signal_value_changed.make_slot());
    _s1.get_adjustment()->signal_value_changed().connect(sigc::mem_fun(*this, &DualSpinScale::update_linked));

    _link.set_relief(Gtk::RELIEF_NONE);
    _link.set_focus_on_click(false);
    _link.set_can_focus(false);
    _link.get_style_context()->add_class("link-edit-button");
    _link.set_valign(Gtk::ALIGN_CENTER);
    _link.signal_clicked().connect(sigc::mem_fun(*this, &DualSpinScale::link_toggled));

    auto const vb = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL);
    vb->add(_s1);
    _s1.set_margin_bottom(3);
    vb->add(_s2);
    pack_start(*vb);
    pack_start(_link, false, false);
    set_link_active(true);
    _s2.set_sensitive(false);

    show_all();
}

void DualSpinScale::set_link_active(bool link) {
    _linked = link;
    _link.set_image_from_icon_name(_linked ? "entries-linked" : "entries-unlinked", Gtk::ICON_SIZE_LARGE_TOOLBAR);
}

Glib::ustring DualSpinScale::get_as_attribute() const
{
    if (_linked) {
        return _s1.get_as_attribute();
    }
    else {
        return _s1.get_as_attribute() + " " + _s2.get_as_attribute();
    }
}

void DualSpinScale::set_from_attribute(SPObject* o)
{
    const gchar* val = attribute_value(o);
    if(val) {
        // Split val into parts
        gchar** toks = g_strsplit(val, " ", 2);

        if(toks) {
            double v1 = 0.0, v2 = 0.0;
            if(toks[0])
                v1 = v2 = Glib::Ascii::strtod(toks[0]);
            if(toks[1])
                v2 = Glib::Ascii::strtod(toks[1]);

            set_link_active(toks[1] == nullptr);

            _s1.get_adjustment()->set_value(v1);
            _s2.get_adjustment()->set_value(v2);

            g_strfreev(toks);
        }
    }
}

sigc::signal<void ()>& DualSpinScale::signal_value_changed()
{
    return _signal_value_changed;
}

const SpinScale& DualSpinScale::get_SpinScale1() const
{
    return _s1;
}

SpinScale& DualSpinScale::get_SpinScale1()
{
    return _s1;
}

const SpinScale& DualSpinScale::get_SpinScale2() const
{
    return _s2;
}

SpinScale& DualSpinScale::get_SpinScale2()
{
    return _s2;
}

void DualSpinScale::link_toggled()
{
    _linked = !_linked;
    set_link_active(_linked);
    _s2.set_sensitive(!_linked);
    update_linked();
}

void DualSpinScale::update_linked()
{
    if (_linked) {
        _s2.set_value(_s1.get_value());
    }
}


} // namespace Widget
} // namespace UI
} // namespace Inkscape

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
