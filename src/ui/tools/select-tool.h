// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef __SP_SELECT_CONTEXT_H__
#define __SP_SELECT_CONTEXT_H__

/*
 * Select tool
 *
 * Authors:
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *
 * Copyright (C) 1999-2002 authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "display/control/canvas-item.h"
#include "ui/tools/tool-base.h"

#define SP_SELECT_CONTEXT(obj) (dynamic_cast<Inkscape::UI::Tools::SelectTool*>((Inkscape::UI::Tools::ToolBase*)obj))
#define SP_IS_SELECT_CONTEXT(obj) (dynamic_cast<const Inkscape::UI::Tools::SelectTool*>((const Inkscape::UI::Tools::ToolBase*)obj) != NULL)

namespace Inkscape {
  class SelTrans;
  class SelectionDescriber;
  class Selection;
}

namespace Inkscape {
namespace UI {
namespace Tools {

class SelectTool : public ToolBase
{
public:
    SelectTool(SPDesktop *desktop);
    ~SelectTool() override;

    bool dragging;
    bool moved;
    unsigned button_press_state;

    std::vector<SPItem *> cycling_items;
    std::vector<SPItem *> cycling_items_cmp;
    SPItem *cycling_cur_item;
    bool cycling_wrap;

    SPItem *item;
    CanvasItem *grabbed = nullptr;
    SelTrans *_seltrans;
    SelectionDescriber *_describer;
    char *no_selection_msg = nullptr;

    void set(Preferences::Entry const &val) override;
    bool root_handler(CanvasEvent const &event) override;
    bool item_handler(SPItem *item, CanvasEvent const &event) override;

    void updateDescriber(Selection *sel);

private:
    bool sp_select_context_abort();
    void sp_select_context_cycle_through_items(Selection *selection, GdkEventScroll *scroll_event);
    void sp_select_context_reset_opacities();

    bool _alt_on;
    bool _force_dragging;

    std::string _default_cursor;
};

} // namespace Tools
} // namespace UI
} // namespace Inkscape

#endif

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
