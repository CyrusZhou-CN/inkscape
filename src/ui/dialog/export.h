/**
 * \brief Export dialog
 *
 * Authors:
 *   Bryce W. Harrington <bryce@bryceharrington.org>
 *
 * Copyright (C) 2004 Authors
 *
 * Released under GNU GPL.  Read the file 'COPYING' for more information.
 */

#ifndef INKSCAPE_UI_DIALOG_EXPORT_H
#define INKSCAPE_UI_DIALOG_EXPORT_H

#include <gtkmm/notebook.h>
#include <glibmm/i18n.h>

#include "dialog.h"
#include "ui/widget/notebook-page.h"

using namespace Inkscape::UI::Widget;

namespace Inkscape {
namespace UI {
namespace Dialog {

class Export : public Dialog {
public:
    Export();
    virtual ~Export();

    Glib::ustring getName() const { return _("Export"); }
    Glib::ustring getDesc() const { return _("Export Dialog"); }

protected:
    Gtk::Notebook  _notebook;

    NotebookPage   _page_export;

private:
    Export(Export const &d);
    Export& operator=(Export const &d);
};

} // namespace Dialog
} // namespace UI
} // namespace Inkscape

#endif // INKSCAPE_UI_DIALOG_EXPORT_H

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
