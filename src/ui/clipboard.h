// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * @brief System-wide clipboard management - class declaration
 *//*
 * Authors: see git history
 *   Krzysztof Kosiński <tweenk@o2.pl>
 *   Jon A. Cruz <jon@joncruz.org>
 *
 *
 * Copyright (C) 2018 Authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef INKSCAPE_UI_CLIPBOARD_H
#define INKSCAPE_UI_CLIPBOARD_H

#include <glibmm/ustring.h>
#include <vector>
#include <2geom/point.h>
#include <2geom/rect.h>

class SPDesktop;
class SPDocument;

namespace Inkscape {

class ObjectSet;
namespace XML { class Node; }
namespace LivePathEffect { class PathParam; }

namespace UI {

/**
 * @brief System-wide clipboard manager
 *
 * ClipboardManager takes care of manipulating the system clipboard in response
 * to user actions. It holds a complete SPDocument as the contents. This document
 * is exported using output extensions when other applications request data.
 * Copying to another instance of Inkscape is special-cased, because of the extra
 * data required (i.e. style, size, Live Path Effects parameters, etc.)
 */

class ClipboardManager
{
public:
    virtual void copy(ObjectSet *set) = 0;
    virtual void copyPathParameter(Inkscape::LivePathEffect::PathParam *) = 0;
    virtual bool copyString(Glib::ustring str) = 0;
    virtual void copySymbol(Inkscape::XML::Node* symbol, gchar const* style, SPDocument *source, const char* symbol_set, Geom::Rect const &bbox, bool set_clipboard) = 0;
    virtual void insertSymbol(SPDesktop *desktop, Geom::Point const &shift_dt, bool read_clipboard) = 0;
    virtual bool paste(SPDesktop *desktop, bool in_place = false, bool on_page = false) = 0;
    virtual bool pasteStyle(ObjectSet *set) = 0;
    virtual bool pasteSize(ObjectSet *set, bool separately, bool apply_x, bool apply_y) = 0;
    virtual bool pastePathEffect(ObjectSet *set) = 0;
    virtual Glib::ustring getPathParameter(SPDesktop* desktop) = 0;
    virtual Glib::ustring getShapeOrTextObjectId(SPDesktop *desktop) = 0;
    virtual std::vector<Glib::ustring> getElementsOfType(SPDesktop *desktop, gchar const* type = "*", gint maxdepth = -1) = 0;
    virtual Glib::ustring getFirstObjectID() = 0;

    static ClipboardManager *get();

protected:
    ClipboardManager() = default;
    ClipboardManager(ClipboardManager const &) = delete;
    ClipboardManager &operator=(ClipboardManager const &) = delete;
};

} // namespace UI
} // namespace Inkscape

#endif // INKSCAPE_UI_CLIPBOARD_H
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
