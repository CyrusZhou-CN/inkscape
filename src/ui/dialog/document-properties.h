// SPDX-License-Identifier: GPL-2.0-or-later
/** \file
 * \brief  Document Properties dialog
 */
/* Authors:
 *   Ralf Stephan <ralf@ark.in-berlin.de>
 *   Bryce W. Harrington <bryce@bryceharrington.org>
 *
 * Copyright (C) 2006-2008 Johan Engelen <johan@shouraizou.nl>
 * Copyright (C) 2004, 2005 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef INKSCAPE_UI_DIALOG_DOCUMENT_PREFERENCES_H
#define INKSCAPE_UI_DIALOG_DOCUMENT_PREFERENCES_H

#ifdef HAVE_CONFIG_H
#include "config.h" // only include where actually required!
#endif

#include <cstddef>
#include <gtkmm/comboboxtext.h>
#include <gtkmm/liststore.h>
#include <gtkmm/notebook.h>
#include <gtkmm/textview.h>
#include <sigc++/sigc++.h>

#include "helper/auto-connection.h"
#include "object/sp-grid.h"
#include "ui/dialog/dialog-base.h"
#include "ui/widget/licensor.h"
#include "ui/widget/registered-widget.h"
#include "ui/widget/registry.h"
#include "ui/widget/tolerance-slider.h"
#include "xml/helper-observer.h"
#include "xml/node-observer.h"

namespace Inkscape {
namespace XML { class Node; }
namespace UI {

namespace Widget {
class AlignmentSelector;
class EntityEntry;
class NotebookPage;
class PageProperties;

class GridWidget : public Gtk::Box
{
public:
    GridWidget(SPGrid *obj);
    ~GridWidget() override;

    void update();
    SPGrid *getGrid() { return grid; }
    XML::Node *getGridRepr() { return repr; }
    Gtk::Box *getTabWidget() { return _tab; }
private:
    SPGrid *grid = nullptr;
    XML::Node *repr = nullptr;

    Gtk::Box *_tab = nullptr;
    Gtk::Image *_tab_img = nullptr;
    Gtk::Label *_tab_lbl = nullptr;

    Gtk::Label *_name_label = nullptr;

    UI::Widget::Registry _wr;
    RegisteredCheckButton *_grid_rcb_enabled = nullptr;
    RegisteredCheckButton *_grid_rcb_snap_visible_only = nullptr;
    RegisteredCheckButton *_grid_rcb_visible = nullptr;
    RegisteredCheckButton *_grid_rcb_dotted = nullptr;
    AlignmentSelector     *_grid_as_alignment = nullptr;

    RegisteredUnitMenu *_rumg = nullptr;
    RegisteredScalarUnit *_rsu_ox = nullptr;
    RegisteredScalarUnit *_rsu_oy = nullptr;
    RegisteredScalarUnit *_rsu_sx = nullptr;
    RegisteredScalarUnit *_rsu_sy = nullptr;
    RegisteredScalar *_rsu_ax = nullptr;
    RegisteredScalar *_rsu_az = nullptr;
    RegisteredColorPicker *_rcp_gcol = nullptr;
    RegisteredColorPicker *_rcp_gmcol = nullptr;
    RegisteredSuffixedInteger *_rsi = nullptr;
    RegisteredScalarUnit* _rsu_gx = nullptr;
    RegisteredScalarUnit* _rsu_gy = nullptr;
    RegisteredScalarUnit* _rsu_mx = nullptr;
    RegisteredScalarUnit* _rsu_my = nullptr;

    Inkscape::auto_connection _modified_signal;
};

} // namespace Widget

namespace Dialog {

using RDEList = std::vector<UI::Widget::EntityEntry *>;

class DocumentProperties : public DialogBase
{
public:
    DocumentProperties();
    ~DocumentProperties() override;

    void  update_widgets();
    static DocumentProperties &getInstance();
    static void destroy();

    void documentReplaced() override;

    void update() override;
    void rebuild_gridspage();

protected:
    void  build_page();
    void  build_grid();
    void  build_guides();
    void  build_snap();
    void  build_gridspage();

    void  build_cms();
    void  build_scripting();
    void  build_metadata();

    void add_grid_widget(SPGrid *grid, bool select = false);
    void remove_grid_widget(XML::Node &node);

    virtual void  on_response (int);
    void  populate_available_profiles();
    void  populate_linked_profiles_box();
    void  linkSelectedProfile();
    void  removeSelectedProfile();
    void  onColorProfileSelectRow();
    void  linked_profiles_list_button_release(GdkEventButton* event);
    void  cms_create_popup_menu(Gtk::Widget& parent, sigc::slot<void ()> rem);

    void  external_scripts_list_button_release(GdkEventButton* event);
    void  embedded_scripts_list_button_release(GdkEventButton* event);
    void  populate_script_lists();
    void  addExternalScript();
    void  browseExternalScript();
    void  addEmbeddedScript();
    void  removeExternalScript();
    void  removeEmbeddedScript();
    void  changeEmbeddedScript();
    void  onExternalScriptSelectRow();
    void  onEmbeddedScriptSelectRow();
    void  editEmbeddedScript();
    void  external_create_popup_menu(Gtk::Widget& parent, sigc::slot<void ()> rem);
    void  embedded_create_popup_menu(Gtk::Widget& parent, sigc::slot<void ()> rem);
    void  load_default_metadata();
    void  save_default_metadata();
    void update_viewbox(SPDesktop* desktop);
    void update_scale_ui(SPDesktop* desktop);
    void update_viewbox_ui(SPDesktop* desktop);
    void set_document_scale(SPDesktop* desktop, double scale_x);
    void set_viewbox_pos(SPDesktop* desktop, double x, double y);
    void set_viewbox_size(SPDesktop* desktop, double width, double height);

    Inkscape::XML::SignalObserver _emb_profiles_observer, _scripts_observer;
    Gtk::Notebook  _notebook;

    UI::Widget::NotebookPage   *_page_page;
    UI::Widget::NotebookPage   *_page_guides;
    UI::Widget::NotebookPage   *_page_cms;
    UI::Widget::NotebookPage   *_page_scripting;

    Gtk::Notebook _scripting_notebook;
    UI::Widget::NotebookPage *_page_external_scripts;
    UI::Widget::NotebookPage *_page_embedded_scripts;

    UI::Widget::NotebookPage  *_page_metadata1;
    UI::Widget::NotebookPage  *_page_metadata2;

    Gtk::Box      _grids_vbox;

    UI::Widget::Registry _wr;
    //---------------------------------------------------------------
    UI::Widget::RegisteredCheckButton _rcb_sgui;
    UI::Widget::RegisteredCheckButton _rcb_lgui;
    UI::Widget::RegisteredColorPicker _rcp_gui;
    UI::Widget::RegisteredColorPicker _rcp_hgui;
    Gtk::Button                       _create_guides_btn;
    Gtk::Button                       _delete_guides_btn;
    //---------------------------------------------------------------
    UI::Widget::PageProperties* _page;
    //---------------------------------------------------------------
    Gtk::Button         _unlink_btn;
    class AvailableProfilesColumns : public Gtk::TreeModel::ColumnRecord
        {
        public:
            AvailableProfilesColumns()
              { add(fileColumn); add(nameColumn); add(separatorColumn); }
            Gtk::TreeModelColumn<Glib::ustring> fileColumn;
            Gtk::TreeModelColumn<Glib::ustring> nameColumn;
            Gtk::TreeModelColumn<bool> separatorColumn;
        };
    AvailableProfilesColumns _AvailableProfilesListColumns;
    Glib::RefPtr<Gtk::ListStore> _AvailableProfilesListStore;
    Gtk::ComboBox _AvailableProfilesList;
    bool _AvailableProfilesList_separator(const Glib::RefPtr<Gtk::TreeModel>& model, const Gtk::TreeModel::iterator& iter);
    class LinkedProfilesColumns : public Gtk::TreeModel::ColumnRecord
        {
        public:
            LinkedProfilesColumns()
              { add(nameColumn); add(previewColumn); }
            Gtk::TreeModelColumn<Glib::ustring> nameColumn;
            Gtk::TreeModelColumn<Glib::ustring> previewColumn;
        };
    LinkedProfilesColumns _LinkedProfilesListColumns;
    Glib::RefPtr<Gtk::ListStore> _LinkedProfilesListStore;
    Gtk::TreeView _LinkedProfilesList;
    Gtk::ScrolledWindow _LinkedProfilesListScroller;
    Gtk::Menu _EmbProfContextMenu;

    //---------------------------------------------------------------
    Gtk::Button         _external_add_btn;
    Gtk::Button         _external_remove_btn;
    Gtk::Button         _embed_new_btn;
    Gtk::Button         _embed_remove_btn;
    Gtk::Box            _embed_button_box;

    class ExternalScriptsColumns : public Gtk::TreeModel::ColumnRecord
        {
        public:
            ExternalScriptsColumns()
               { add(filenameColumn); }
            Gtk::TreeModelColumn<Glib::ustring> filenameColumn;
        };
    ExternalScriptsColumns _ExternalScriptsListColumns;
    class EmbeddedScriptsColumns : public Gtk::TreeModel::ColumnRecord
        {
        public:
            EmbeddedScriptsColumns()
               { add(idColumn); }
            Gtk::TreeModelColumn<Glib::ustring> idColumn;
        };
    EmbeddedScriptsColumns _EmbeddedScriptsListColumns;
    Glib::RefPtr<Gtk::ListStore> _ExternalScriptsListStore;
    Glib::RefPtr<Gtk::ListStore> _EmbeddedScriptsListStore;
    Gtk::TreeView _ExternalScriptsList;
    Gtk::TreeView _EmbeddedScriptsList;
    Gtk::ScrolledWindow _ExternalScriptsListScroller;
    Gtk::ScrolledWindow _EmbeddedScriptsListScroller;
    Gtk::Menu _ExternalScriptsContextMenu;
    Gtk::Menu _EmbeddedScriptsContextMenu;
    Gtk::Entry _script_entry;
    Gtk::TextView _EmbeddedContent;
    Gtk::ScrolledWindow _EmbeddedContentScroller;
    //---------------------------------------------------------------

    Gtk::Notebook   _grids_notebook;
    Gtk::Box        _grids_hbox_crea;
    Gtk::Label      _grids_label_crea;
    Gtk::Button     _grids_button_remove;
    Gtk::Label      _grids_label_def;
    //---------------------------------------------------------------

    RDEList _rdflist;
    UI::Widget::Licensor _licensor;

    Gtk::Box& _createPageTabLabel(const Glib::ustring& label, const char *label_image);

private:
    // callback methods for buttons on grids page.
    void onNewGrid(GridType type);
    void onRemoveGrid();

    // callback for display unit change
    void display_unit_change(const Inkscape::Util::Unit* unit);

    class WatchConnection : private XML::NodeObserver
    {
    public:
        WatchConnection(DocumentProperties *dialog)
            : _dialog(dialog)
        {}
        ~WatchConnection() override { disconnect(); }
        void connect(Inkscape::XML::Node *node);
        void disconnect();

    private:
        void notifyChildAdded(XML::Node &node, XML::Node &child, XML::Node *prev) final;
        void notifyChildRemoved(XML::Node &node, XML::Node &child, XML::Node *prev) final;
        void notifyAttributeChanged(XML::Node &node, GQuark name, Util::ptr_shared old_value,
                                    Util::ptr_shared new_value) final;

        Inkscape::XML::Node *_node{nullptr};
        DocumentProperties *_dialog;
    };
    // nodes connected to listeners
    WatchConnection _namedview_connection;
    WatchConnection _root_connection;
};

} // namespace Dialog
} // namespace UI
} // namespace Inkscape

#endif // INKSCAPE_UI_DIALOG_DOCUMENT_PREFERENCES_H

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
