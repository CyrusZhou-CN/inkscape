// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * This file contains the definition of the FontCollectionSelector class. This widget
 * defines a treeview to provide the interface to create, read, update and delete font
 * collections and their respective fonts. This class contains all the code related to
 * population of collections and their fonts in the TreeStore.
 *
 * Author:
 *   Vaibhav Malik <vaibhavmalik2018@gmail.com> 
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 *
 */

#ifndef INKSCAPE_UI_WIDGET_FONT_COLLECTION_SELECTOR_H
#define INKSCAPE_UI_WIDGET_FONT_COLLECTION_SELECTOR_H

#include <gtkmm/grid.h>
#include <gtkmm/frame.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/treeview.h>
#include <gtkmm/label.h>
#include <gtkmm/comboboxtext.h>

#include "ui/tools/tool-base.h"
#include "ui/widget/iconrenderer.h"
#include "ui/widget/scrollprotected.h"
#include "util/font-collections.h"
#include "util/document-fonts.h"
#include "util/recently-used-fonts.h"

namespace Inkscape {
namespace UI {
namespace Widget {

/**
 * A container of widgets for selecting font faces.
 */
class FontCollectionSelector : public Gtk::Grid
{
public:

    enum {TEXT_COLUMN, ICON_COLUMN, N_COLUMNS};
    enum SelectionStates {SYSTEM_COLLECTION = -1, USER_COLLECTION, USER_COLLECTION_FONT};

    FontCollectionSelector();

    // Basic setup.
    void setup_tree_view(Gtk::TreeView*);
    void change_frame_name(const Glib::ustring&);
    void setup_signals();

    Glib::ustring get_text_cell_markup(Gtk::TreeIter const &iter);

    // Custom renderers.
    void text_cell_data_func(Gtk::CellRenderer*, Gtk::TreeIter const&);
    void icon_cell_data_func(Gtk::CellRenderer*, Gtk::TreeIter const&);
    void check_button_cell_data_func(Gtk::CellRenderer*, Gtk::TreeIter const&);
    bool row_separator_func(const Glib::RefPtr<Gtk::TreeModel>&, const Gtk::TreeModel::iterator&);

    void populate_collections();

    void populate_system_collections();
    void populate_document_fonts();
    void populate_recently_used_fonts();

    void populate_user_collections();
    void populate_fonts(const Glib::ustring&);

    // Signal handlers
    void on_delete_icon_clicked(Glib::ustring const&);
    void on_create_collection();
    void on_rename_collection(const Glib::ustring&, const Glib::ustring&);
    void on_delete_button_pressed();
    void on_edit_button_pressed();

    void deletion_warning_message_dialog(Glib::ustring const &collection_name, sigc::slot<void(int)> onresponse);
    bool on_key_pressed(GdkEventKey*);

    bool on_drag_motion(const Glib::RefPtr<Gdk::DragContext> &, int, int, guint) override;
    void on_drag_data_received(const Glib::RefPtr<Gdk::DragContext>, int, int, const Gtk::SelectionData&, guint, guint);
    bool on_drag_drop(const Glib::RefPtr<Gdk::DragContext> &, int, int, guint) override;
    // bool on_drag_failed(const Glib::RefPtr<Gdk::DragContext> &, const Gtk::DragResult);
    void on_drag_leave(const Glib::RefPtr<Gdk::DragContext> &, guint) override;
    // void on_drag_start(const Glib::RefPtr<Gdk::DragContext> &); 
    void on_drag_end(const Glib::RefPtr<Gdk::DragContext> &) override;
    void on_selection_changed();

    sigc::connection connect_signal_changed(sigc::slot <void (int)> slot) {
        return signal_changed.connect(slot);
    }

protected:

    class FontCollectionClass : public Gtk::TreeModelColumnRecord
    {
    public:
        Gtk::TreeModelColumn<Glib::ustring> name;
        Gtk::TreeModelColumn<bool> is_editable;

        FontCollectionClass()
        {
            add(name);
            add(is_editable);
        }
    };
    FontCollectionClass FontCollection;

    Gtk::TreeView *treeview;
    Gtk::Frame frame;
    Gtk::ScrolledWindow scroll;
    Gtk::TreeViewColumn text_column;
    Gtk::TreeViewColumn del_icon_column;
    Gtk::CellRendererText *cell_text;
    Inkscape::UI::Widget::IconRenderer *del_icon_renderer;

    Glib::RefPtr<Gtk::TreeStore> store;

    // What type of object can be dropped.
    std::vector<Gtk::TargetEntry> target_entries;
    sigc::signal <void (int)> signal_changed;
};

} // namespace Widget
} // namespace UI
} // namespace Inkscape

#endif // INKSCAPE_UI_WIDGET_FONT_COLLECTION_SELECTOR_H

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:fileencoding=utf-8 :
