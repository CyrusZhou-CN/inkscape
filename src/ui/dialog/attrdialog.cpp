// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * @brief A dialog for XML attributes
 */
/* Authors:
 *   Martin Owens
 *
 * Copyright (C) Martin Owens 2018 <doctormo@gmail.com>
 *
 * Released under GNU GPLv2 or later, read the file 'COPYING' for more information
 */

#include "attrdialog.h"

#include "verbs.h"
#include "selection.h"
#include "document-undo.h"

#include "message-context.h"
#include "message-stack.h"
#include "style.h"
#include "ui/icon-loader.h"
#include "ui/widget/iconrenderer.h"

#include "xml/node-event-vector.h"
#include "xml/attribute-record.h"

#include <glibmm/i18n.h>
#include <gdk/gdkkeysyms.h>

static void on_attr_changed (Inkscape::XML::Node * repr,
                         const gchar * name,
                         const gchar * /*old_value*/,
                         const gchar * new_value,
                         bool /*is_interactive*/,
                         gpointer data)
{
    ATTR_DIALOG(data)->onAttrChanged(repr, name, new_value);
}

static void on_content_changed (Inkscape::XML::Node * repr,
                                gchar const * oldcontent,
                                gchar const * newcontent,
                                gpointer data)
{
    ATTR_DIALOG(data)->onAttrChanged(repr, "content", repr->content());
}

Inkscape::XML::NodeEventVector _repr_events = {
    nullptr, /* child_added */
    nullptr, /* child_removed */
    on_attr_changed,
    on_content_changed, /* content_changed */
    nullptr  /* order_changed */
};

namespace Inkscape {
namespace UI {
namespace Dialog {

/**
 * Constructor
 * A treeview whose each row corresponds to an XML attribute of a selected node
 * New attribute can be added by clicking '+' at bottom of the attr pane. '-'
 */
AttrDialog::AttrDialog()
    : UI::Widget::Panel("/dialogs/attr", SP_VERB_DIALOG_ATTR)
    , _desktop(nullptr)
    , _repr(nullptr)
{
    set_size_request(20, 15);
    _mainBox.pack_start(_scrolledWindow, Gtk::PACK_EXPAND_WIDGET);
    _treeView.set_headers_visible(true);
    _treeView.set_hover_selection(true);
    _treeView.set_activate_on_single_click(true);
    _treeView.set_can_focus(false);
    _scrolledWindow.add(_treeView);
    _scrolledWindow.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);

    _store = Gtk::ListStore::create(_attrColumns);
    _treeView.set_model(_store);

    Inkscape::UI::Widget::IconRenderer * addRenderer = manage(new Inkscape::UI::Widget::IconRenderer());
    addRenderer->add_icon("edit-delete");

    _treeView.append_column("", *addRenderer);
    Gtk::TreeViewColumn *col = _treeView.get_column(0);
    if (col) {
        auto add_icon = Gtk::manage(sp_get_icon_image("list-add", Gtk::ICON_SIZE_SMALL_TOOLBAR));
        col->set_clickable(true);
        col->set_widget(*add_icon);
        add_icon->set_tooltip_text(_("Add a new attribute"));
        add_icon->show();
        // This gets the GtkButton inside the GtkBox, inside the GtkAlignment, inside the GtkImage icon.
        auto button = add_icon->get_parent()->get_parent()->get_parent();
        // Assign the button event so that create happens BEFORE delete. If this code
        // isn't in this exact way, the onAttrDelete is called when the header lines are pressed.
        button->signal_button_release_event().connect(sigc::mem_fun(*this, &AttrDialog::onAttrCreate), false);
    }
    addRenderer->signal_activated().connect(sigc::mem_fun(*this, &AttrDialog::onAttrDelete));
    _treeView.signal_key_press_event().connect(sigc::mem_fun(*this, &AttrDialog::onKeyPressed));
    _treeView.set_search_column(-1);

    _nameRenderer = Gtk::manage(new Gtk::CellRendererText());
    _nameRenderer->property_editable() = true;
    _nameRenderer->property_placeholder_text().set_value(_("Attribute Name"));
    _nameRenderer->signal_edited().connect(sigc::mem_fun(*this, &AttrDialog::nameEdited));
    _nameRenderer->signal_editing_started().connect(sigc::mem_fun(*this, &AttrDialog::startNameEdit));
    _treeView.append_column(_("Name"), *_nameRenderer);
    _nameCol = _treeView.get_column(1);
    if (_nameCol) {
        _nameCol->set_resizable(true);
        _nameCol->add_attribute(_nameRenderer->property_text(), _attrColumns._attributeName);
    }
    status.set_halign(Gtk::ALIGN_START);
    status.set_valign(Gtk::ALIGN_CENTER);
    status.set_size_request(1, -1);
    status.set_markup("");
    status.set_line_wrap(true);
    status.get_style_context()->add_class("inksmall");
    status_box.pack_start(status, TRUE, TRUE, 0);
    _getContents()->pack_end(status_box, false, false, 2);

    _message_stack = std::make_shared<Inkscape::MessageStack>();
    _message_context = std::unique_ptr<Inkscape::MessageContext>(new Inkscape::MessageContext(_message_stack));
    _message_changed_connection =
        _message_stack->connectChanged(sigc::bind(sigc::ptr_fun(_set_status_message), GTK_WIDGET(status.gobj())));

    _valueRenderer = Gtk::manage(new Gtk::CellRendererText());
    _valueRenderer->property_editable() = true;
    _valueRenderer->property_placeholder_text().set_value(_("Attribute Value"));
    _valueRenderer->property_ellipsize().set_value(Pango::ELLIPSIZE_MIDDLE);
    _valueRenderer->signal_edited().connect(sigc::mem_fun(*this, &AttrDialog::valueEdited));
    _valueRenderer->signal_editing_started().connect(sigc::mem_fun(*this, &AttrDialog::startValueEdit));
    _treeView.append_column(_("Value"), *_valueRenderer);
    _valueCol = _treeView.get_column(2);
    if (_valueCol) {
      _valueCol->add_attribute(_valueRenderer->property_text(), _attrColumns._attributeValue);
    }
    _popover = Gtk::manage(new Gtk::Popover());
    Gtk::Box *vbox = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL));
    Gtk::Box *hbox = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL));
    _textview = Gtk::manage(new Gtk::TextView());
    _textview->set_wrap_mode(Gtk::WrapMode::WRAP_CHAR);
    _textview->set_editable(true);
    _textview->set_monospace(true);
    _textview->set_border_width(6);
    Glib::RefPtr<Gtk::TextBuffer> textbuffer = Gtk::TextBuffer::create();
    textbuffer->set_text("");
    _textview->set_buffer(textbuffer);
    _scrolled_text_view.add(*_textview);
    _scrolled_text_view.set_max_content_height(450);
    _scrolled_text_view.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
    _scrolled_text_view.set_propagate_natural_width(true);
    _update = Gtk::manage(new Gtk::Button(_("Update")));
    _update->signal_clicked().connect(sigc::mem_fun(*this, &AttrDialog::valueEditedPop));
    hbox->pack_end(*_update, Gtk::PACK_EXPAND_WIDGET, 3);
    vbox->pack_start(_scrolled_text_view, Gtk::PACK_EXPAND_WIDGET, 3);
    vbox->pack_start(*hbox, Gtk::PACK_EXPAND_WIDGET, 3);
    _popover->add(*vbox);
    _popover->hide();
    _popover->set_relative_to(_treeView);
    _popover->set_position(Gtk::PositionType::POS_BOTTOM);
    _popover->signal_closed().connect(sigc::mem_fun(*this, &AttrDialog::popClosed));
    _popover->get_style_context()->add_class("inverted");
    attr_reset_context(0);
    _getContents()->pack_start(_mainBox, Gtk::PACK_EXPAND_WIDGET);
    setDesktop(getDesktop());
    _updating = false;
}

/**
 * @brief AttrDialog::~AttrDialog
 * Class destructor
 */
AttrDialog::~AttrDialog()
{
    setDesktop(nullptr);
    _message_changed_connection.disconnect();
    _message_context = nullptr;
    _message_stack = nullptr;
    _message_changed_connection.~connection();
}

void AttrDialog::startNameEdit(Gtk::CellEditable *cell, const Glib::ustring &path)
{
    Gtk::Entry *entry = dynamic_cast<Gtk::Entry *>(cell);
    entry->signal_key_press_event().connect(sigc::bind(sigc::mem_fun(*this, &AttrDialog::onNameKeyPressed), entry));
}

gboolean sp_show_attr_pop(gpointer data)
{
    AttrDialog *attrdialog = reinterpret_cast<AttrDialog *>(data);
    auto vscroll = attrdialog->_scrolled_text_view.get_vadjustment();
    int height = vscroll->get_upper() + 12; // padding 6+6
    if (height < 450) {
        attrdialog->_scrolled_text_view.set_min_content_height(height);
    } else {
        attrdialog->_scrolled_text_view.set_min_content_height(450);
    }
    return FALSE;
}

void AttrDialog::startValueEdit(Gtk::CellEditable *cell, const Glib::ustring &path)
{
    Gtk::Entry *entry = dynamic_cast<Gtk::Entry *>(cell);
    entry->signal_key_press_event().connect(sigc::bind(sigc::mem_fun(*this, &AttrDialog::onValueKeyPressed), entry));
    int width = 0;
    int height = 0;
    int colwidth = _valueCol->get_width();
    _textview->set_size_request(colwidth - 6, -1);
    _popover->set_size_request(colwidth, -1);
    valuepath = path;
    entry->get_layout()->get_pixel_size(width, height);
    Gtk::TreeIter iter = *_store->get_iter(path);
    Gtk::TreeModel::Row row = *iter;
    if (row && this->_repr) {
        Glib::ustring name = row[_attrColumns._attributeName];
        if (colwidth < width || name == "content") {
            Gtk::TreeIter iter = *_store->get_iter(path);
            Gdk::Rectangle rect;
            _treeView.get_cell_area((Gtk::TreeModel::Path)iter, *_valueCol, rect);
            if (_popover->get_position() == Gtk::PositionType::POS_BOTTOM) {
                rect.set_y(rect.get_y() + 20);
            }
            _popover->set_pointing_to(rect);
            Glib::RefPtr<Gtk::TextBuffer> textbuffer = Gtk::TextBuffer::create();
            textbuffer->set_text(entry->get_text());
            _textview->set_buffer(textbuffer);
            cell->editing_done();
            cell->remove_widget();
            int scrolledcontentheight = 20;
            if (name == "content") {
                scrolledcontentheight = 450;
            }
            _scrolled_text_view.set_min_content_height(scrolledcontentheight);
            _popover->show_all();
            _popover->check_resize();
            g_timeout_add(50, &sp_show_attr_pop, this);
        }
    }
}

void AttrDialog::popClosed()
{
    Glib::RefPtr<Gtk::TextBuffer> textbuffer = Gtk::TextBuffer::create();
    textbuffer->set_text("");
    _textview->set_buffer(textbuffer);
}

/**
 * @brief AttrDialog::setDesktop
 * @param desktop
 * This function sets the 'desktop' for the CSS pane.
 */
void AttrDialog::setDesktop(SPDesktop* desktop)
{
    _desktop = desktop;
}

/**
 * @brief AttrDialog::setRepr
 * Set the internal xml object that I'm working on right now.
 */
void AttrDialog::setRepr(Inkscape::XML::Node * repr)
{
    if ( repr == _repr ) return;
    if (_repr) {
        _store->clear();
        _repr->removeListenerByData(this);
        Inkscape::GC::release(_repr);
        _repr = nullptr;
    }
    _repr = repr;
    if (repr) {
        Inkscape::GC::anchor(_repr); 
        _repr->addListener(&_repr_events, this);
        _repr->synthesizeEvents(&_repr_events, this);
    }
}

/**
 * @brief AttrDialog::onKeyPressed
 * @param event_description
 * @return
 * Send an undo message and mark this point for undo
 */
void AttrDialog::setUndo(Glib::ustring const &event_description)
{
    SPDocument *document = this->_desktop->doc();
    DocumentUndo::done(document, SP_VERB_DIALOG_XML_EDITOR, event_description);
}

void AttrDialog::_set_status_message(Inkscape::MessageType /*type*/, const gchar *message, GtkWidget *widget)
{
    if (widget) {
        gtk_label_set_markup(GTK_LABEL(widget), message ? message : "");
    }
}

/**
 * Sets the AttrDialog status bar, depending on which attr is selected.
 */
void AttrDialog::attr_reset_context(gint attr)
{
    if (attr == 0) {
        _message_context->set(Inkscape::NORMAL_MESSAGE, _("<b>Click</b> attribute to edit."));
    } else {
        const gchar *name = g_quark_to_string(attr);
        _message_context->setF(
            Inkscape::NORMAL_MESSAGE,
            _("Attribute <b>%s</b> selected. Press <b>Ctrl+Enter</b> when done editing to commit changes."), name);
    }
}

/**
 * @brief AttrDialog::onAttrChanged
 * This is called when the XML has an updated attribute
 */
void AttrDialog::onAttrChanged(Inkscape::XML::Node *repr, const gchar * name, const gchar * new_value)
{
    if (_updating) {
        return;
    }
    for(auto iter: this->_store->children())
    {
        Gtk::TreeModel::Row row = *iter;
        Glib::ustring col_name = row[_attrColumns._attributeName];
        if(name == col_name) {
            if(new_value) {
                row[_attrColumns._attributeValue] = new_value;
                new_value = nullptr; // Don't make a new one
            } else {
                _store->erase(iter);
            }
            break;
        }
    }
    if (new_value && strcmp(new_value, "") != 0) {
        if ((repr->type() == Inkscape::XML::TEXT_NODE || repr->type() == Inkscape::XML::COMMENT_NODE) &&
             strcmp(name, "content") != 0)
        {
            return;   
        } else {
            Gtk::TreeModel::Row row = *(_store->prepend());
            row[_attrColumns._attributeName] = name;
            row[_attrColumns._attributeValue] = new_value;
        }
    }
}

/**
 * @brief AttrDialog::onAttrCreate
 * This function is a slot to signal_clicked for '+' button panel.
 */
bool AttrDialog::onAttrCreate(GdkEventButton *event)
{
    if(event->type == GDK_BUTTON_RELEASE && event->button == 1 && this->_repr) {
        Gtk::TreeIter iter = _store->prepend();
        Gtk::TreeModel::Path path = (Gtk::TreeModel::Path)iter;
        _treeView.set_cursor(path, *_nameCol, true);
        grab_focus();
        return true;
    }
    return false;
}

/**
 * @brief AttrDialog::onAttrDelete
 * @param event
 * @return true
 * Delete the attribute from the xml
 */
void AttrDialog::onAttrDelete(Glib::ustring path)
{
    Gtk::TreeModel::Row row = *_store->get_iter(path);
    if (row) {
        Glib::ustring name = row[_attrColumns._attributeName];
        if (name == "content") {
            return;
        } else {
            this->_store->erase(row);
            this->_repr->setAttribute(name.c_str(), nullptr, false);
            this->setUndo(_("Delete attribute"));
        }
    }
}

/**
 * @brief AttrDialog::onKeyPressed
 * @param event
 * @return true
 * Delete or create elements based on key presses
 */
bool AttrDialog::onKeyPressed(GdkEventKey *event)
{
    if(this->_repr) {
        auto selection = this->_treeView.get_selection();
        Gtk::TreeModel::Row row = *(selection->get_selected());
        Gtk::TreeIter iter = *(selection->get_selected());
        bool ret = false;
        switch (event->keyval)
        {
            case GDK_KEY_Delete:
            case GDK_KEY_KP_Delete: {
                // Create new attribute (repeat code, fold into above event!)
                Glib::ustring name = row[_attrColumns._attributeName];
                if (name != "content") {
                    this->_store->erase(row);
                    this->_repr->setAttribute(name.c_str(), nullptr, false);
                    this->setUndo(_("Delete attribute"));
                }
                ret = true;
            } break;
            case GDK_KEY_plus:
            case GDK_KEY_Insert:
              {
                // Create new attribute (repeat code, fold into above event!)
                Gtk::TreeIter iter = this->_store->prepend();
                Gtk::TreeModel::Path path = (Gtk::TreeModel::Path)iter;
                this->_treeView.set_cursor(path, *this->_nameCol, true);
                grab_focus();
                ret = true;
            } break;
        }
    }
    return false;
}

bool AttrDialog::onNameKeyPressed(GdkEventKey *event, Gtk::Entry *entry)
{
    g_debug("StyleDialog::_onNameKeyPressed");
    bool ret = false;
    switch (event->keyval) {
        case GDK_KEY_Tab:
        case GDK_KEY_KP_Tab:
            entry->editing_done();
            ret = true;
            break;
    }
    return ret;
}


bool AttrDialog::onValueKeyPressed(GdkEventKey *event, Gtk::Entry *entry)
{
    g_debug("StyleDialog::_onValueKeyPressed");
    bool ret = false;
    switch (event->keyval) {
        case GDK_KEY_Tab:
        case GDK_KEY_KP_Tab:
            entry->editing_done();
            ret = true;
            break;
    }
    return ret;
}

gboolean sp_attrdialog_store_move_to_next(gpointer data)
{
    AttrDialog *attrdialog = reinterpret_cast<AttrDialog *>(data);
    auto selection = attrdialog->_treeView.get_selection();
    Gtk::TreeIter iter = *(selection->get_selected());
    Gtk::TreeModel::Path model = (Gtk::TreeModel::Path)iter;
    if (model == attrdialog->_modelpath) {
        attrdialog->_treeView.set_cursor(attrdialog->_modelpath, *attrdialog->_valueCol, true);
    }
    return FALSE;
}

/**
 *
 *
 * @brief AttrDialog::nameEdited
 * @param event
 * @return
 * Called when the name is edited in the TreeView editable column
 */
void AttrDialog::nameEdited (const Glib::ustring& path, const Glib::ustring& name)
{
    Gtk::TreeIter iter = *_store->get_iter(path);
    _modelpath = (Gtk::TreeModel::Path)iter;
    Gtk::TreeModel::Row row = *iter;
    if(row && this->_repr) {
        Glib::ustring old_name = row[_attrColumns._attributeName];
        if (old_name == name) {
            g_timeout_add(50, &sp_attrdialog_store_move_to_next, this);
            grab_focus();
            return;
        }
        if (old_name == "content") {
            return;
        }
        Glib::ustring value = row[_attrColumns._attributeValue];
        // Move to editing value, we set the name as a temporary store value
        if (!old_name.empty()) {
            // Remove old named value
            _updating = true;
            _repr->setAttribute(old_name.c_str(), nullptr, false);
            _updating = false;
        }
        if (!name.empty()) {
            row[_attrColumns._attributeName] = name;
            grab_focus();
            _updating = true;
            _repr->setAttribute(name.c_str(), value, false);
            _updating = false;
            g_timeout_add(50, &sp_attrdialog_store_move_to_next, this);
        }
        this->setUndo(_("Rename attribute"));
    }
}

void AttrDialog::valueEditedPop()
{
    Glib::ustring value = _textview->get_buffer()->get_text();
    valueEdited(valuepath, value);
}

/**
 * @brief AttrDialog::valueEdited
 * @param event
 * @return
 * Called when the value is edited in the TreeView editable column
 */
void AttrDialog::valueEdited (const Glib::ustring& path, const Glib::ustring& value)
{
    Gtk::TreeModel::Row row = *_store->get_iter(path);
    if(row && this->_repr) {
        Glib::ustring name = row[_attrColumns._attributeName];
        Glib::ustring old_value = row[_attrColumns._attributeValue];
        if (old_value == value) {
            return;
        }
        if(name.empty()) return;
        if (name == "content") {
            _repr->setContent(value.c_str());
        } else {
            _repr->setAttribute(name.c_str(), value, false);
        }
        if(!value.empty()) {
            row[_attrColumns._attributeValue] = value;
        }
        Inkscape::Selection *selection = _desktop->getSelection();
        SPObject *obj = nullptr;
        if (selection->objects().size() == 1) {
            obj = selection->objects().back();

            obj->style->readFromObject(obj);
            obj->requestDisplayUpdate(SP_OBJECT_MODIFIED_FLAG | SP_OBJECT_STYLE_MODIFIED_FLAG);
        }
        this->setUndo(_("Change attribute value"));
    }
}

} // namespace Dialog
} // namespace UI
} // namespace Inkscape
