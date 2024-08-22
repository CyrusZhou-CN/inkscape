// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * @brief Symbols dialog
 */
/* Authors:
 *   Tavmjong Bah, Martin Owens
 *
 * Copyright (C) 2012 Tavmjong Bah
 *               2013 Martin Owens
 *               2017 Jabiertxo Arraiza
 *               2023 Mike Kowalski
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef INKSCAPE_UI_DIALOG_SYMBOLS_H
#define INKSCAPE_UI_DIALOG_SYMBOLS_H

#include <cstddef>
#include <string>
#include <vector>
#include <glibmm/refptr.h>
#include <glibmm/ustring.h>
#include <gtkmm.h>
#include <gtkmm/builder.h>
#include <gtkmm/cellrendererpixbuf.h>
#include <gtkmm/treeiter.h>
#include <gtkmm/treemodel.h>
#include <gtkmm/treemodelcolumn.h>
#include <boost/compute/detail/lru_cache.hpp>

#include "desktop.h"
#include "display/drawing.h"
#include "document.h"
#include "helper/auto-connection.h"
#include "selection.h"
#include "ui/dialog/dialog-base.h"
#include "ui/operation-blocker.h"

namespace Gtk {
class CheckButton;
class IconView;
class Label;
class MenuButton;
}

class SPObject;
class SPSymbol;
class SPUse;

namespace Inkscape {
namespace UI {
namespace Dialog {

/**
 * A dialog that displays selectable symbols and allows users to drag or paste
 * those symbols from the dialog into the document.
 *
 * Symbol documents are loaded from the preferences paths and displayed in a
 * drop-down list to the user. The user then selects which of the symbols
 * documents they want to get symbols from. The first document in the list is
 * always the current document.
 *
 * This then updates an icon-view with all the symbols available. Selecting one
 * puts it onto the clipboard. Dragging it or pasting it onto the canvas copies
 * the symbol from the symbol document, into the current document and places a
 * new <use> element at the correct location on the canvas.
 *
 * Selected groups on the canvas can be added to the current document's symbols
 * table, and symbols can be removed from the current document. This allows
 * new symbols documents to be constructed and if saved in the prefs folder will
 * make those symbols available for all future documents.
 */
class SymbolsDialog : public DialogBase
{
public:
    SymbolsDialog(char const *prefsPath = "/dialogs/symbols");
    ~SymbolsDialog() override;

private:
    void documentReplaced() override;
    void selectionChanged(Inkscape::Selection *selection) override;
    void rebuild();
    void rebuild(Gtk::TreeIter current);
    void insertSymbol();
    void revertSymbol();
    void iconChanged();
    void sendToClipboard(const Gtk::TreeIter& symbol_iter, Geom::Rect const &bbox);
    Glib::ustring getSymbolId(const std::optional<Gtk::TreeIter>& it) const;
    Geom::Point getSymbolDimensions(const std::optional<Gtk::TreeIter>& it) const;
    SPDocument* get_symbol_document(const std::optional<Gtk::TreeIter>& it) const;
    void iconDragDataGet(const Glib::RefPtr<Gdk::DragContext>& context, Gtk::SelectionData& selection_data, guint info, guint time);
    void onDragStart();
    void addSymbol(SPSymbol* symbol, Glib::ustring doc_title, SPDocument* document);
    SPDocument* symbolsPreviewDoc();
    void useInDoc(SPObject *r, std::vector<SPUse*> &l);
    std::vector<SPUse*> useInDoc( SPDocument* document);
    void addSymbols();
    void showOverlay();
    void hideOverlay();
    gchar const* styleFromUse( gchar const* id, SPDocument* document);
    Cairo::RefPtr<Cairo::Surface> drawSymbol(SPSymbol *symbol);
    Cairo::RefPtr<Cairo::Surface> draw_symbol(SPSymbol* symbol);
    Glib::RefPtr<Gdk::Pixbuf> getOverlay(gint width, gint height);
    void set_info();
    void set_info(const Glib::ustring& text);
    std::optional<Gtk::TreeIter> get_current_set() const;
    Glib::ustring get_current_set_id() const;
    std::optional<Gtk::TreeModel::Path> get_selected_symbol_path() const;
    std::optional<Gtk::TreeIter> get_selected_symbol() const;
    void load_all_symbols();
    void update_tool_buttons();
    size_t total_symbols() const;
    size_t visible_symbols() const;
    void get_cell_data_func(Gtk::CellRenderer* cell_renderer, Gtk::TreeModel::Row row, bool visible);
    void refresh_on_idle(int delay = 100);

    auto_connection _idle_search;
    Glib::RefPtr<Gtk::Builder> _builder;
    Gtk::Scale& _zoom;
    // Index into sizes which is selected
    int pack_size;
    // Scale factor
    int scale_factor;
    bool sensitive = false;
    OperationBlocker _update;
    double previous_height;
    double previous_width;
    Glib::RefPtr<Gtk::ListStore> _store;
    Gtk::MenuButton& _symbols_popup;
    Gtk::SearchEntry& _set_search;
    Gtk::IconView& _symbol_sets_view;
    Gtk::Label& _cur_set_name;
    Gtk::SearchEntry& _search;
    Gtk::IconView* icon_view;
    Gtk::Button* add_symbol;
    Gtk::Button* remove_symbol;
    Gtk::Box* tools;
    Gtk::Overlay* overlay;
    Gtk::Image* overlay_icon;
    Gtk::Image* overlay_opacity;
    Gtk::Label* overlay_title;
    Gtk::Label* overlay_desc;
    Gtk::ScrolledWindow *scroller;
    Gtk::CheckButton* fit_symbol;
    Gtk::CellRendererPixbuf _renderer;
    Gtk::CellRendererPixbuf _renderer2;
    SPDocument *preview_document = nullptr; /* Document to render single symbol */
    Glib::RefPtr<Gtk::ListStore> _symbol_sets;

    struct Store {
        Glib::RefPtr<Gtk::ListStore> _store;
        Glib::RefPtr<Gtk::TreeModelFilter> _filtered;
        Glib::RefPtr<Gtk::TreeModelSort> _sorted;

        Gtk::TreeIter path_to_child_iter(Gtk::TreeModel::Path path) const {
            if (_sorted) path = _sorted->convert_path_to_child_path(path);
            if (_filtered) path = _filtered->convert_path_to_child_path(path);
            return _store->get_iter(path);
        }
        void refilter() {
            if (_filtered) _filtered->refilter();
        }
    } _symbols, _sets;

    /* For rendering the template drawing */
    unsigned key;
    Inkscape::Drawing renderDrawing;
    auto_connection _defs_modified;
    auto_connection _doc_resource_changed;
    auto_connection _idle_refresh;
    boost::compute::detail::lru_cache<std::string, Cairo::RefPtr<Cairo::Surface>> _image_cache;
};

} //namespace Dialogs
} //namespace UI
} //namespace Inkscape

#endif // INKSCAPE_UI_DIALOG_SYMBOLS_H

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
