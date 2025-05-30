// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * @brief A dialog for CSS selectors
 */
/* Authors:
 *   Kamalpreet Kaur Grewal
 *   Tavmjong Bah
 *   Jabiertxof
 *
 * Copyright (C) Kamalpreet Kaur Grewal 2016 <grewalkamal005@gmail.com>
 * Copyright (C) Tavmjong Bah 2017 <tavmjong@free.fr>
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef SELECTORSDIALOG_H
#define SELECTORSDIALOG_H

#include <glibmm/refptr.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/gesture.h> // Gtk::EventSequenceState
#include <gtkmm/paned.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/treemodel.h>
#include <gtkmm/treestore.h>
#include <gtkmm/treeview.h>
#include <memory>
#include <vector>

#include "css/syntactic-decomposition.h"
#include "ui/dialog/dialog-base.h"
#include "xml/helper-observer.h"

namespace Gtk {
class Adjustment;
class Dialog;
class GestureClick;
class ToggleButton;
class SelectionData;
class TreeModelFilter;
} // namespace Gtk

namespace Inkscape {

namespace UI::Dialog {

class StyleDialog;

/**
 * @brief The SelectorsDialog class
 * A list of CSS selectors will show up in this dialog. This dialog allows one to
 * add and delete selectors. Elements can be added to and removed from the selectors
 * in the dialog. Selection of any selector row selects the matching  objects in
 * the drawing and vice-versa. (Only simple selectors supported for now.)
 *
 * This class must keep two things in sync:
 *   1. The text node of the style element.
 *   2. The Gtk::TreeModel.
 */
class SelectorsDialog final : public DialogBase
{
public:
    SelectorsDialog();
    ~SelectorsDialog() final;

    void update() final;
    void desktopReplaced() final;
    void documentReplaced() final;
    void selectionChanged(Selection *selection) final;

  private:
    // Monitor <style> element for changes.
    class NodeObserver;

    void removeObservers();

    // Monitor all objects for addition/removal/attribute change
    class NodeWatcher;
    enum SelectorType { CLASS, ID, TAG };
    void _nodeAdded(   Inkscape::XML::Node &repr );
    void _nodeRemoved( Inkscape::XML::Node &repr );
    void _nodeChanged( Inkscape::XML::Node &repr );
    // Data structure
    enum coltype { OBJECT, SELECTOR, OTHER };
    class ModelColumns : public Gtk::TreeModel::ColumnRecord {
    public:
        ModelColumns() {
            add(_colSelector);
            add(_colExpand);
            add(_colType);
            add(_colObj);
            add(_colProperties);
            add(_fontWeight);
        }
        Gtk::TreeModelColumn<Glib::ustring> _colSelector;       // Selector or matching object id.
        Gtk::TreeModelColumn<bool> _colExpand;                  // Open/Close store row.
        Gtk::TreeModelColumn<gint> _colType;                    // Selector row or child object row.
        Gtk::TreeModelColumn<SPObject *> _colObj;               // Matching object (if any).
        Gtk::TreeModelColumn<Glib::ustring> _colProperties;     // List of properties.
        Gtk::TreeModelColumn<gint> _fontWeight;                 // Text label font weight.
    };
    ModelColumns _mColumns;

    // Override Gtk::TreeStore to control drag-n-drop (only allow dragging and dropping of selectors).
    // See: https://developer.gnome.org/gtkmm-tutorial/stable/sec-treeview-examples.html.en
    //
    // TreeStore implements simple drag and drop (DND) but there appears no way to know when a DND
    // has been completed (other than doing the whole DND ourselves). As a hack, we use
    // on_row_deleted to trigger write of style element.
    class TreeStore final : public Gtk::TreeStore {
    protected:
        TreeStore();
        bool row_draggable_vfunc(const Gtk::TreeModel::Path& path) const final;
        bool row_drop_possible_vfunc(const Gtk::TreeModel::Path& path,
                                     const Glib::ValueBase& selection_data) const final;
        void on_row_deleted(const TreeModel::Path& path) final;

    public:
      static Glib::RefPtr<SelectorsDialog::TreeStore> create(SelectorsDialog *styledialog);

    private:
      SelectorsDialog *_selectorsdialog;
    };

    // TreeView
    Glib::RefPtr<Gtk::TreeModelFilter> _modelfilter;
    Glib::RefPtr<TreeStore> _store;
    Gtk::TreeView _treeView;
    Gtk::TreeModel::Path _lastpath;
    // Widgets
    StyleDialog *_style_dialog;
    Gtk::Paned _paned;
    Glib::RefPtr<Gtk::Adjustment> _vadj;
    Gtk::Box _button_box;
    Gtk::Box _selectors_box;
    Gtk::ScrolledWindow _scrolled_window_selectors;

    Gtk::Button _del;
    Gtk::Button _create;

    // Reading the style element.
    Inkscape::XML::Node *_getStyleTextNode(bool create_if_missing = false);
    void _readStyleElement();

    // Helper functions for inserting representations of CSS syntactic elements.
    void _insertSyntacticElement(CSS::RuleStatement const &rule, bool expand, Gtk::TreeIter<Gtk::TreeRow> where);
    void _insertSyntacticElement(CSS::BlockAtStatement const &block_at, bool expand, Gtk::TreeIter<Gtk::TreeRow> where);
    void _insertSyntacticElement(CSS::OtherStatement const &other, bool, Gtk::TreeIter<Gtk::TreeRow> where);

    // Writing the style element.
    void _writeStyleElement();
    Glib::ustring _formatRowAsCSS(Gtk::TreeConstRow const &row) const;

    // Update watchers
    std::unique_ptr<Inkscape::XML::NodeObserver> m_nodewatcher;
    std::unique_ptr<Inkscape::XML::NodeObserver> m_styletextwatcher;

    // Manipulate Tree
    [[nodiscard]] std::vector<SPObject *> getSelectedObjects();
    void _addToSelector(Gtk::TreeModel::Row row);
    void _removeFromSelector(Gtk::TreeModel::Row row);
    Glib::ustring _getIdList(std::vector<SPObject *>);
    std::vector<SPObject *> _getObjVec(Glib::ustring const &selector);
    void _insertClass(const std::vector<SPObject *>& objVec, const Glib::ustring& className);
    void _insertClass(SPObject *obj, const Glib::ustring &className);
    void _removeClass(const std::vector<SPObject *> &objVec, const Glib::ustring &className, bool all = false);
    void _removeClass(SPObject *obj, const Glib::ustring &className, bool all = false);
    void _toggleDirection(Gtk::ToggleButton *vertical);
    void _showWidgets();

    // Variables
    double _scrollpos{0.0};
    bool _scrollock{false};
    bool _updating{false};          // Prevent cyclic actions: read <-> write, select via dialog <-> via desktop
    Inkscape::XML::Node *m_root{nullptr};
    Inkscape::XML::Node *_textNode{nullptr}; // Track so we know when to add a NodeObserver.

    void _rowExpand(const Gtk::TreeModel::iterator &iter, const Gtk::TreeModel::Path &path);
    void _rowCollapse(const Gtk::TreeModel::iterator &iter, const Gtk::TreeModel::Path &path);
    void _closeDialog(Gtk::Dialog *textDialogPtr);

    Inkscape::XML::SignalObserver _objObserver; // Track object in selected row (for style change).

    // Signal and handlers - Internal
    void _addSelector();
    void _delSelector();
    static Glib::ustring _getSelectorClasses(Glib::ustring selector);
    void onTreeViewClickReleased(int n_press, double x, double y);
    void _selectRow(); // Select row in tree when selection changed.
    void _vscroll();

    // GUI
    void _styleButton(Gtk::Button& btn, char const* iconName, char const* tooltip);
};

} // namespace UI::Dialog

} // namespace Inkscape

#endif // SELECTORSDIALOG_H

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
