// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2005-2007 Authors:
 *   Ted Gould <ted@gould.cx>
 *   Johan Engelen <johan@shouraizou.nl> *
 *   Jon A. Cruz <jon@joncruz.org>
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef SEEN_INK_EXTENSION_PARAMCOLOR_H
#define SEEN_INK_EXTENSION_PARAMCOLOR_H

#include <memory>
#include <string>
#include <sigc++/signal.h>
#include "parameter.h"
#include "ui/selected-color.h"

namespace Gtk {
class ColorButton;
class Widget;
} // namespace Gtk

namespace Inkscape {

namespace XML {
class Node;
} // namespace XML

namespace Extension {

class ParamColor : public InxParameter {
public:
    enum AppearanceMode {
        DEFAULT, COLOR_BUTTON
    };

    ParamColor(Inkscape::XML::Node *xml, Inkscape::Extension::Extension *ext);
    ~ParamColor() override;

    /** Returns \c _value, with a \i const to protect it. */
    unsigned int get() const { return _color.value(); }
    unsigned int set(unsigned int in);

    Gtk::Widget *get_widget(sigc::signal<void ()> *changeSignal) override;

    std::string value_to_string() const override;
    void string_to_value(const std::string &in) override;

    std::unique_ptr<sigc::signal<void ()>> _changeSignal;

private:
    void _onColorChanged();
    void _onColorButtonChanged();

    /** Internal value of this parameter */
    Inkscape::UI::SelectedColor _color;

    sigc::connection _color_changed;
    sigc::connection _color_released;

    Gtk::ColorButton *_color_button;

    /** appearance mode **/
    AppearanceMode _mode = DEFAULT;
}; // class ParamColor

}  // namespace Extension

}  // namespace Inkscape

#endif // SEEN_INK_EXTENSION_PARAMCOLOR_H

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
