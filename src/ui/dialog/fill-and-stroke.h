/**
 * \brief Fill and Stroke dialog
 *
 * Authors:
 *   Bryce W. Harrington <bryce@bryceharrington.org>
 *
 * Copyright (C) 2004 Authors
 *
 * Released under GNU GPL.  Read the file 'COPYING' for more information.
 */

#ifndef INKSCAPE_UI_DIALOG_FILL_AND_STROKE_H
#define INKSCAPE_UI_DIALOG_FILL_AND_STROKE_H

#include <gtkmm/notebook.h>
#include <glibmm/i18n.h>

#include "dialog.h"
#include "ui/widget/notebook-page.h"

using namespace Inkscape::UI::Widget;

namespace Inkscape {
namespace UI {
namespace Dialog {

class FillAndStroke : public Dialog {
public:
    FillAndStroke();
    virtual ~FillAndStroke();

    Glib::ustring getName() const { return _("Fill and Stroke"); }
    Glib::ustring getDesc() const { return _("Fill and Stroke Dialog"); }

protected:
    Gtk::Notebook  _notebook;

    NotebookPage   _page_fill;
    NotebookPage   _page_stroke_paint;
    NotebookPage   _page_stroke_style;

private:
    FillAndStroke(FillAndStroke const &d);
    FillAndStroke& operator=(FillAndStroke const &d);
};

} // namespace Dialog
} // namespace UI
} // namespace Inkscape

#endif // INKSCAPE_UI_DIALOG_FILL_AND_STROKE_H

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
