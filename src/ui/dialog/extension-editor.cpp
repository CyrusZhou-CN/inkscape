/**
 * \brief Extension editor
 *
 * Authors:
 *   Bryce W. Harrington <bryce@bryceharrington.org>
 *
 * Copyright (C) 2004 Authors
 *
 * Released under GNU GPL.  Read the file 'COPYING' for more information.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include "extension-editor.h"

namespace Inkscape {
namespace UI {
namespace Dialog {

ExtensionEditor::ExtensionEditor()
{
    set_title(getName());
    set_default_size(400, 200);

    transientize();

    // Top level vbox
//    Gtk::VBox *vbox = get_vbox();

    // TODO:  Insert widgets

    show_all_children();
}

ExtensionEditor::~ExtensionEditor()
{
}

} // namespace Dialog
} // namespace UI
} // namespace Inkscape

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=c++:expandtab:shiftwidth=4:tabstop=8:softtabstop=4 :
