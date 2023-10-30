// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Drag and drop of drawings onto canvas.
 */

/* Authors:
 *
 * Copyright (C) Tavmjong Bah 2019
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "drag-and-drop.h"

#include <array>
#include <glibmm/i18n.h>  // Internationalization

#include "desktop-style.h"
#include "document.h"
#include "document-undo.h"
#include "gradient-drag.h"
#include "file.h"
#include "selection.h"
#include "style.h"
#include "layer-manager.h"

#include "extension/db.h"
#include "extension/find_extension_by_mime.h"

#include "object/sp-shape.h"
#include "object/sp-text.h"
#include "object/sp-flowtext.h"

#include "path/path-util.h"

#include "svg/svg-color.h" // write color

#include "ui/clipboard.h"
#include "ui/interface.h"
#include "ui/tools/tool-base.h"
#include "ui/widget/canvas.h"  // Target, canvas to world transform.
#include "ui/widget/desktop-widget.h"

#include "widgets/paintdef.h"

using Inkscape::DocumentUndo;

/* Drag and Drop */
enum ui_drop_target_info {
    URI_LIST,
    SVG_XML_DATA,
    SVG_DATA,
    PNG_DATA,
    JPEG_DATA,
    IMAGE_DATA,
    APP_X_INKY_COLOR,
    APP_X_COLOR,
    APP_OSWB_COLOR,
    APP_X_INK_PASTE
};

/** Convert screen (x, y) coordinates to desktop coordinates. */
inline Geom::Point world2desktop(SPDesktop *desktop, int x, int y)
{
    g_assert(desktop);
    return (Geom::Point(x, y) + desktop->getCanvas()->get_area_world().min()) * desktop->w2d();
}

#if 0
static
void ink_drag_motion( GtkWidget */*widget*/,
                        GdkDragContext */*drag_context*/,
                        gint /*x*/, gint /*y*/,
                        GtkSelectionData */*data*/,
                        guint /*info*/,
                        guint /*event_time*/,
                        gpointer /*user_data*/)
{
//     SPDocument *doc = SP_ACTIVE_DOCUMENT;
//     SPDesktop *desktop = SP_ACTIVE_DESKTOP;


//     g_message("drag-n-drop motion (%4d, %4d)  at %d", x, y, event_time);
}

static void ink_drag_leave( GtkWidget */*widget*/,
                              GdkDragContext */*drag_context*/,
                              guint /*event_time*/,
                              gpointer /*user_data*/ )
{
//     g_message("drag-n-drop leave                at %d", event_time);
}
#endif

void ink_drag_setup(SPDesktopWidget *dtw)
{
}

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
