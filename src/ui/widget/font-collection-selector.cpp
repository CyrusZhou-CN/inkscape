// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Author:
 *   Vaibhav Malik <vaibhavmalik2018@gmail.com>
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <glibmm/i18n.h>
#include <glibmm/markup.h>

#include "font-collection-selector.h"

#include "libnrtype/font-lister.h"

#include "ui/dialog-run.h"

// For updating from selection
#include "util/document-fonts.h"

namespace Inkscape {
namespace UI {
namespace Widget {

FontCollectionSelector::FontCollectionSelector()
{
    // Step 1: Initialize the treeview.
    treeview = Gtk::make_managed<Gtk::TreeView>();

    // Step 2: Setup the treeview.
    setup_tree_view(treeview);

    // Step 3: Intialize the model.
    store = Gtk::TreeStore::create(FontCollection);
    // Step 4: Populate the ListStore.
    treeview->set_model(store);

    // Signals.
    setup_signals();

    show_all_children();
}

// Setup the treeview of the widget.
void FontCollectionSelector::setup_tree_view(Gtk::TreeView *tv)
{
    cell_text = new Gtk::CellRendererText();
    del_icon_renderer = manage(new Inkscape::UI::Widget::IconRenderer());
    del_icon_renderer->add_icon("edit-delete");

    text_column.pack_start (*cell_text, true);
    text_column.add_attribute (*cell_text, "text", TEXT_COLUMN);
    text_column.set_expand(true);

    del_icon_column.pack_start (*del_icon_renderer, false);

    // Attach the cell data functions.
    text_column.set_cell_data_func(*cell_text, sigc::mem_fun(*this, &FontCollectionSelector::text_cell_data_func));

    treeview->enable_model_drag_dest (Gdk::ACTION_MOVE);
    treeview->set_headers_visible (false);

    // Target entries for Drag and Drop.
    target_entries.emplace_back(Gtk::TargetEntry("STRING", (Gtk::TargetFlags)0, 0));
    target_entries.emplace_back(Gtk::TargetEntry("text/plain", (Gtk::TargetFlags)0, 0));

    treeview->drag_dest_set(target_entries, Gtk::DEST_DEFAULT_ALL, Gdk::ACTION_COPY);

    // Append the columns to the treeview.
    treeview->append_column(text_column);
    treeview->append_column(del_icon_column);

    scroll.set_policy (Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
    scroll.set_overlay_scrolling(false);
    scroll.add (*treeview);

    frame.set_hexpand (true);
    frame.set_vexpand (true);
    frame.add (scroll);

    // Grid
    set_name("FontCollection");
    set_row_spacing(4);
    set_column_spacing(1);

    // Add extra columns to the "frame" to change space distribution
    attach (frame,  0, 0, 1, 2);
}

void FontCollectionSelector::change_frame_name(const Glib::ustring& name)
{
    frame.set_label(name);
}

void FontCollectionSelector::setup_signals()
{
    cell_text->signal_edited().connect(sigc::mem_fun(*this, &FontCollectionSelector::on_rename_collection));
    del_icon_renderer->signal_activated().connect(sigc::mem_fun(*this, &FontCollectionSelector::on_delete_icon_clicked));
    treeview->signal_key_press_event().connect([=](GdkEventKey *ev){ return on_key_pressed(ev); });
    treeview->set_row_separator_func(sigc::mem_fun(*this, &FontCollectionSelector::row_separator_func));
    treeview->get_column(ICON_COLUMN)->set_cell_data_func(*del_icon_renderer, sigc::mem_fun(*this, &FontCollectionSelector::icon_cell_data_func));

    // Signals for drag and drop.
    treeview->signal_drag_motion().connect(sigc::mem_fun(*this, &FontCollectionSelector::on_drag_motion), false);
    treeview->signal_drag_data_received().connect(sigc::mem_fun(*this, &FontCollectionSelector::on_drag_data_received), false);
    treeview->signal_drag_drop().connect(sigc::mem_fun(*this, &FontCollectionSelector::on_drag_drop), false);
    // treeview->signal_drag_failed().connect(sigc::mem_fun(*this, &FontCollectionSelector::on_drag_failed), false);
    treeview->signal_drag_leave().connect(sigc::mem_fun(*this, &FontCollectionSelector::on_drag_leave), false);
    treeview->signal_drag_end().connect(sigc::mem_fun(*this, &FontCollectionSelector::on_drag_end), false);
    treeview->get_selection()->signal_changed().connect([=](){ on_selection_changed(); });
    Inkscape::RecentlyUsedFonts::get()->connectUpdate(sigc::mem_fun(*this, &FontCollectionSelector::populate_system_collections));
}

// To distinguish the collection name and the font name.
Glib::ustring FontCollectionSelector::get_text_cell_markup(Gtk::TreeIter const &iter)
{
    Glib::ustring markup;
    auto parent = (*iter)->parent();

    if(parent) {
        // It is a font.
        markup = "<span alpha='50%'>";
        markup += (*iter)[FontCollection.name];
        markup += "</span>";
    }
    else {
        // It is a collection.
        markup = "<span>";
        markup += (*iter)[FontCollection.name];
        markup += "</span>";
    }

    return markup;
}

// This function will TURN OFF the visibility of the delete icon for system collections.
void FontCollectionSelector::text_cell_data_func(Gtk::CellRenderer *renderer, Gtk::TreeIter const &iter)
{
    // Add the delete icon only if the collection is editable(user-collection).
    Glib::ustring markup = get_text_cell_markup(iter);
    renderer->set_property("markup", markup);
}

// This function will TURN OFF the visibility of the delete icon for system collections.
void FontCollectionSelector::icon_cell_data_func(Gtk::CellRenderer *renderer, Gtk::TreeIter const &iter)
{
    // Add the delete icon only if the collection is editable(user-collection).
    Gtk::TreeModel::Row row = *iter;
    auto parent = (*iter)->parent();

    if(parent) {
        // Case: It is a font.
        bool is_user = (*parent)[FontCollection.is_editable];
        del_icon_renderer->set_visible(is_user);
        cell_text->property_editable() = false;
    } else if((*iter)[FontCollection.is_editable]) {
        // Case: User font collection.
        del_icon_renderer->set_visible(true);
        cell_text->property_editable() = true;
    } else {
        // Case: System font collection.
        del_icon_renderer->set_visible(false);
        cell_text->property_editable() = false;
    }
}

// This function will TURN OFF the visibility of checkbuttons for children in the TreeStore.
void FontCollectionSelector::check_button_cell_data_func(Gtk::CellRenderer *renderer, Gtk::TreeIter const &iter)
{
    renderer->set_visible(false);
    /*
    // Append the checkbutton column only if the iterator have some children.
    Gtk::TreeModel::Row row = *iter;
    auto parent = row->parent();

    if(parent) {
        renderer->set_visible(false);
    }
    else {
        renderer->set_visible(true);
    }
    */
}

bool FontCollectionSelector::row_separator_func(const Glib::RefPtr<Gtk::TreeModel>& model, const Gtk::TreeModel::iterator& iter)
{
    return (*iter)[FontCollection.name] == "#";
}

void FontCollectionSelector::populate_collections()
{
    store->clear();
    populate_system_collections();
    populate_user_collections();
}

// This function will keep the populate the system collections and their fonts.
void FontCollectionSelector::populate_system_collections()
{
    FontCollections *font_collections = Inkscape::FontCollections::get();
    std::vector <Glib::ustring> system_collections = font_collections->get_collections(true);

    // Erase the previous collections.
    store->freeze_notify();
    Gtk::TreePath path;
    path.push_back(0);
    Gtk::TreeModel::iterator iter;
    bool row_0 = false, row_1 = false;

    for(int i = 0; i < 3; i++) {
        iter = store->get_iter(path);
        if(iter) {
            if(treeview->row_expanded(path)) {
                if(i == 0) {
                    row_0 = true;
                } else if(i == 1) {
                    row_1 = true;
                }
            }
            store->erase(iter);
        }
    }

    // Insert a separator.
    iter = store->prepend();
    (*iter)[FontCollection.name] = "#";
    (*iter)[FontCollection.is_editable] = false;
    iter = store->children();

    for(auto const &col: system_collections) {
        iter = store->prepend();
        (*iter)[FontCollection.name] = col;
        (*iter)[FontCollection.is_editable] = false;
    }

    populate_document_fonts();
    populate_recently_used_fonts();
    store->thaw_notify();

    if(row_0) {
        treeview->expand_row(Gtk::TreePath("0"), true);
    }
    if(row_1) {
        treeview->expand_row(Gtk::TreePath("1"), true);
    }
}

void FontCollectionSelector::populate_document_fonts()
{
    // The position of the recently used collection is hardcoded for now.
    Gtk::TreePath path;
    path.push_back(1);
    Gtk::TreeModel::iterator iter = store->get_iter(path);

    for(auto const& font: Inkscape::DocumentFonts::get()->get_fonts()) {
        Gtk::TreeModel::iterator child = store->append((*iter).children());
        (*child)[FontCollection.name] = font;
        (*child)[FontCollection.is_editable] = false;
    }
}

void FontCollectionSelector::populate_recently_used_fonts()
{
    // The position of the recently used collection is hardcoded for now.
    Gtk::TreePath path;
    path.push_back(0);
    Gtk::TreeModel::iterator iter = store->get_iter(path);

    for(auto const& font: Inkscape::RecentlyUsedFonts::get()->get_fonts()) {
        Gtk::TreeModel::iterator child = store->append((*iter).children());
        (*child)[FontCollection.name] = font;
        (*child)[FontCollection.is_editable] = false;
    }
}

// This function will keep the collections_list updated after any event.
void FontCollectionSelector::populate_user_collections()
{
    // Get the list of all the user collections.
    auto collections = Inkscape::FontCollections::get()->get_collections();

    // Now insert these collections one by one into the treeview.
    store->freeze_notify();
    Gtk::TreeModel::iterator iter;

    for(const auto &col: collections) {
        iter = store->append();
        (*iter)[FontCollection.name] = col;

        // User collections are editable.
        (*iter)[FontCollection.is_editable] = true;

        // Alright, now populate the fonts of this collection.
        populate_fonts(col);
    }
    store->thaw_notify();
}

void FontCollectionSelector::populate_fonts(const Glib::ustring& collection_name)
{
    // Get the FontLister instance to get the list of all the collections.
    FontCollections *font_collections = Inkscape::FontCollections::get();
    std::set <Glib::ustring> fonts = font_collections->get_fonts(collection_name);

    // First find the location of this collection_name in the map.
    // +1 for the separator.
    int index = font_collections->get_user_collection_location(collection_name) + 1;

    store->freeze_notify();

    // Generate the iterator path.
    Gtk::TreePath path;
    path.push_back(index);
    Gtk::TreeModel::iterator iter = store->get_iter(path);

    // auto child_iter = iter->children();
    auto size = iter->children().size();

    // Clear the previously stored fonts at this path.
    while(size--) {
        Gtk::TreeModel::iterator child = iter->children().begin();
        store->erase(child);
    }

    for(auto const &font: fonts) {
        Gtk::TreeModel::iterator child = store->append((*iter).children());
        (*child)[FontCollection.name] = font;
        (*child)[FontCollection.is_editable] = false;
    }

    store->thaw_notify();
}

void FontCollectionSelector::on_delete_icon_clicked(Glib::ustring const &path)
{
    auto collections = Inkscape::FontCollections::get();
    auto iter = store->get_iter(path);
    auto parent = (*iter)->parent();

    if (!parent) {
        // It is a collection.

        // No need to confirm in case of empty collections.
        if (collections->get_fonts((*iter)[FontCollection.name]).empty()) {
            collections->remove_collection((*iter)[FontCollection.name]);
            store->erase(iter);
            return;
        }

        // Warn the user and then proceed.
        deletion_warning_message_dialog((*iter)[FontCollection.name], [this, iter] (int response) {
            if (response == Gtk::RESPONSE_YES) {
                auto collections = Inkscape::FontCollections::get();
                collections->remove_collection((*iter)[FontCollection.name]);
                store->erase(iter);
            }
        });
    }
    else {
        // It is a font.
        collections->remove_font((*parent)[FontCollection.name], (*iter)[FontCollection.name]);
        store->erase(iter);
    }
}

void FontCollectionSelector::on_create_collection()
{
    Gtk::TreeModel::iterator iter = store->append();
    (*iter)[FontCollection.is_editable] = true;

    Gtk::TreeModel::Path path = (Gtk::TreeModel::Path)iter;
    treeview->set_cursor(path, text_column, true);
    grab_focus();
}

void FontCollectionSelector::on_rename_collection(const Glib::ustring& path, const Glib::ustring& new_text)
{
    // Fetch the collections.
    FontCollections *collections = Inkscape::FontCollections::get();

    // Check if the same collection is already present.
    bool is_system = collections->find_collection(new_text, true);
    bool is_user = collections->find_collection(new_text, false);

    // Return if the new name is empty.
    // Do not allow user collections to be named as system collections.
    if (new_text == "" || is_system || is_user) {
        return;
    }

    Gtk::TreeModel::iterator iter = store->get_iter(path);

    // Return if it is not a valid iter.
    if(!iter) {
        return;
    }

    // To check if it's a font-collection or a font.
    auto parent = (*iter)->parent();

    if(!parent) {
        // Call the rename_collection function
        collections->rename_collection((*iter)[FontCollection.name], new_text);
    }
    else {
        collections->rename_font((*parent)[FontCollection.name], (*iter)[FontCollection.name], new_text);
    }

    (*iter)[FontCollection.name] = new_text;
    populate_collections();
}

void FontCollectionSelector::on_delete_button_pressed()
{
    // Get the current collection.
    Glib::RefPtr<Gtk::TreeSelection> selection = treeview->get_selection();
    Gtk::TreeModel::iterator iter = selection->get_selected();
    Gtk::TreeModel::Row row = *iter;
    auto parent = row->parent();

    auto collections = Inkscape::FontCollections::get();

    if (!parent) {
        // It is a collection.
        // Check if it is a system collection.
        bool is_system = collections->find_collection((*iter)[FontCollection.name], true);
        if (is_system) {
            return;
        }

        // Warn the user and then proceed.
        deletion_warning_message_dialog((*iter)[FontCollection.name], [this, iter] (int response) {
            if (response == Gtk::RESPONSE_YES) {
                auto collections = Inkscape::FontCollections::get();
                collections->remove_collection((*iter)[FontCollection.name]);
                store->erase(iter);
            }
        });
    } else {
        // It is a font.
        // Check if it belongs to a system collection.
        bool is_system = collections->find_collection((*parent)[FontCollection.name], true);
        if (is_system) {
            return;
        }

        collections->remove_font((*parent)[FontCollection.name], row[FontCollection.name]);

        store->erase(iter);
    }
}

// Function to edit the name of the collection or font.
void FontCollectionSelector::on_edit_button_pressed()
{
    Glib::RefPtr<Gtk::TreeSelection> selection = treeview->get_selection();

    if(selection) {
        Gtk::TreeModel::iterator iter = selection->get_selected();
        if(!iter) {
            return;
        }

        Gtk::TreeModel::Row row = *iter;
        auto parent = row->parent();
        bool is_system = Inkscape::FontCollections::get()->find_collection((*iter)[FontCollection.name], true);

        if(!parent && !is_system) {
            // It is a collection.
            treeview->set_cursor(Gtk::TreePath(iter), text_column, true);
        }
    }
}

void FontCollectionSelector::deletion_warning_message_dialog(Glib::ustring const &collection_name, sigc::slot<void(int)> onresponse)
{
    auto message = Glib::ustring::compose(_("Are you sure want to delete the \"%1\" font collection?\n"), collection_name);
    auto dialog = std::make_unique<Gtk::MessageDialog>(message, false, Gtk::MESSAGE_WARNING, Gtk::BUTTONS_YES_NO, true);
    dialog->signal_response().connect(onresponse);
    dialog_show_modal_and_selfdestruct(std::move(dialog), get_toplevel());
}

bool FontCollectionSelector::on_key_pressed(GdkEventKey *event)
{
    if (event->type == GDK_KEY_PRESS && frame.get_label() == "Collections")
    {
        // std::cout << "Key pressed" << std::endl;
        switch (Inkscape::UI::Tools::get_latin_keyval (event)) {
            case GDK_KEY_Delete:
                on_delete_button_pressed();
                break;
        }
        // We handled this event.
        return true;
    }
    // We did not handle this event.
    return false;
}

bool FontCollectionSelector::on_drag_motion(const Glib::RefPtr<Gdk::DragContext> &context,
                                            int x,
                                            int y,
                                            guint time)
{
    Gtk::TreeModel::Path path;
    Gtk::TreeViewDropPosition pos;

    treeview->get_dest_row_at_pos(x, y, path, pos);
    treeview->drag_unhighlight();

    if (path) {
        context->drag_status(Gdk::ACTION_COPY, time);
        return false;
    }

    // remove drop highlight
    context->drag_refuse(time);
    return true;
}

void FontCollectionSelector::on_drag_data_received(const Glib::RefPtr<Gdk::DragContext> context,
                                                   int x,
                                                   int y,
                                                   const Gtk::SelectionData &selection_data,
                                                   guint info, guint time)
{
    // std::cout << "FontCollectionSelector::on_drag_data_received()" << std::endl;
    // 1. Get the row at which the data is dropped.
    Gtk::TreePath path;
    treeview->get_path_at_pos(x, y, path);
    Gtk::TreeModel::iterator iter = store->get_iter(path);
    bool is_expanded = false;

    // Case when the font is dragged in the empty space.
    if(!iter) {
        return;
    }

    Glib::ustring collection_name = (*iter)[FontCollection.name];
    auto font_name = Inkscape::FontLister::get_instance()->get_dragging_family();

    FontCollections *collections = Inkscape::FontCollections::get();
    std::vector <Glib::ustring> system_collections = collections->get_collections(true);
    auto parent = (*iter)->parent();

    if(parent) {
        is_expanded = true;
        collection_name = (*parent)[FontCollection.name];
        bool is_system = collections->find_collection(collection_name, true);

        if(is_system) {
            // The font is dropped in a system collection.
            return;
        }
    } else {
        if (treeview->row_expanded(path)) {
            is_expanded = true;
        }

        bool is_system = collections->find_collection(collection_name, true);

        if(is_system) {
            // The font is dropped in a system collection.
            return;
        }
    }

    // 2. Get the data that is sent by the source.
    // std::cout << "Received: " << selection_data.get_data() << std::endl;
    // std::cout << (*iter)[FontCollection.name] << std::endl;
    // Add the font into the collection.
    collections->add_font(collection_name, font_name);

    // Re-populate the collection.
    populate_fonts(collection_name);

    // Re-expand this row after re-population.
    if(is_expanded) {
        treeview->expand_to_path(path);
    }

    // Call gtk_drag_finish(context, success, del = false, time)
    gtk_drag_finish(context->gobj(), TRUE, FALSE, time);
}

bool FontCollectionSelector::on_drag_drop(const Glib::RefPtr<Gdk::DragContext> &context,
                                          int x,
                                          int y,
                                          guint time)
{
    // std::cout << "FontCollectionSelector::on_drag_drop()" << std::endl;
    Gtk::TreeModel::Path path;
    Gtk::TreeViewDropPosition pos;
    treeview->get_dest_row_at_pos(x, y, path, pos);

    if (!path) {
        // std::cout << "Not on target\n";
        return false;
    }

    on_drag_end(context);
    return true;
}

/*
bool FontCollectionSelector::on_drag_failed(const Glib::RefPtr<Gdk::DragContext> &context,
                                            const Gtk::DragResult result)
{
    std::cout << "Drag Failed\n";
    return true;
}
*/

void FontCollectionSelector::on_drag_leave(const Glib::RefPtr<Gdk::DragContext> &context,
                                           guint time)
{
    // std::cout << "Drag Leave\n";
}

/*
void FontCollectionSelector::on_drag_start(const Glib::RefPtr<Gdk::DragContext> &context)
{
    // std::cout << "FontCollectionSelector::on_drag_start()" << std::endl;
}
*/

void FontCollectionSelector::on_drag_end(const Glib::RefPtr<Gdk::DragContext> &context)
{
    // std::cout << "FontCollection::on_drag_end()" << std::endl;
    treeview->drag_unhighlight();
}

void FontCollectionSelector::on_selection_changed()
{
    Glib::RefPtr <Gtk::TreeSelection> selection = treeview->get_selection();
    if(selection) {
        FontCollections *font_collections = Inkscape::FontCollections::get();
        Gtk::TreeModel::iterator iter = selection->get_selected();
        auto parent = iter->parent();

        // We use 3 states to adjust the sensitivity of the edit and
        // delete buttons in the font collections manager dialog.
        int state = 0;

        // State -1: Selection is a system collection or a system
        // collection font.(Neither edit nor delete)

        // State 0: It's not a system collection or it's font. But it is
        // a user collection.(Both edit and delete).

        // State 1: It is a font that belongs to a user collection.
        // (Only delete)

        if(parent) {
            // It is a font, and thus it is not editable.
            // Now check if it's parent is a system collection.
            bool is_system = font_collections->find_collection((*parent)[FontCollection.name], true);
            state = (is_system) ? SYSTEM_COLLECTION: USER_COLLECTION_FONT;
        } else {
            // Check if it is a system collection.
            bool is_system = font_collections->find_collection((*iter)[FontCollection.name], true);
            state = (is_system) ? SYSTEM_COLLECTION: USER_COLLECTION;
        }

        signal_changed.emit(state);
    }
}

} // namespace Widget
} // namespace UI
} // namespace Inkscape

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
