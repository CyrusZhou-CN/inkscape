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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include "fill-and-stroke.h"

namespace Inkscape {
namespace UI {
namespace Dialog {

FillAndStroke::FillAndStroke() 
    : _page_fill("Fill", 1, 1),
      _page_stroke_paint("Stroke Paint", 1, 1),
      _page_stroke_style("Stroke Style", 1, 1)
{
    set_title(getName());
    set_default_size(200, 200);

    transientize();

    // Top level vbox
    Gtk::VBox *vbox = get_vbox();
    vbox->set_spacing(4);

    // Notebook for individual transformations
    vbox->pack_start(_notebook, true, true);

    _notebook.append_page(_page_fill,         _("Fill"));
    _notebook.append_page(_page_stroke_paint, _("Stroke Paint"));
    _notebook.append_page(_page_stroke_style, _("Stroke Style"));

    // TODO:  Insert widgets

    show_all_children();
}

FillAndStroke::~FillAndStroke() 
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
