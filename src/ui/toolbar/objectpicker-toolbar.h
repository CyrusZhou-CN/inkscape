// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Object picker toolbar
 *//*
 * Authors:
 * see git history
 * Vaibhav Malik <vaibhavmalik2018@gmail.com>
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef SEEN_OBJECTPICKER_TOOLBAR_H
#define SEEN_OBJECTPICKER_TOOLBAR_H

#include <gtkmm/builder.h>

#include "toolbar.h"

namespace Inkscape::UI::Toolbar {

class ObjectPickerToolbar final : public Toolbar
{
public:
    ObjectPickerToolbar(SPDesktop *desktop);

private:
    Glib::RefPtr<Gtk::Builder> _builder;
};

} // namespace Inkscape::UI::Toolbar

#endif
