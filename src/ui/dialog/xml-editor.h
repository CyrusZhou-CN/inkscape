/**
 * \brief XML Editor dialog
 *
 * Authors:
 *   Bryce W. Harrington <bryce@bryceharrington.org>
 *
 * Copyright (C) 2004 Authors
 *
 * Released under GNU GPL.  Read the file 'COPYING' for more information.
 */

#ifndef INKSCAPE_DIALOG_XML_EDITOR_H
#define INKSCAPE_DIALOG_XML_EDITOR_H

#include "dialog.h"

#include <glibmm/i18n.h>

namespace Inkscape {
namespace UI {
namespace Dialog {

class XmlEditor : public Dialog {
public:
    XmlEditor();
    virtual ~XmlEditor();

    Glib::ustring getName() const { return _("XML Editor"); }
    Glib::ustring getDesc() const { return _("XML Editor Dialog"); }

protected:

private:
    XmlEditor(XmlEditor const &d);
    XmlEditor& operator=(XmlEditor const &d);
};

} // namespace Dialog
} // namespace UI
} // namespace Inkscape

#endif // INKSCAPE_UI_DIALOG_XML_EDITOR_H

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
