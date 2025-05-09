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

#include "selectorsdialog.h"

#include <algorithm>
#include <glibmm/i18n.h>
#include <glibmm/regex.h>
#include <gtkmm/adjustment.h>
#include <gtkmm/dialog.h>
#include <gtkmm/entry.h>
#include <gtkmm/gestureclick.h>
#include <gtkmm/label.h>
#include <gtkmm/togglebutton.h>
#include <gtkmm/treemodelfilter.h>
#include <map>
#include <string>
#include <utility>

#include "attribute-rel-svg.h"
#include "css/syntactic-decomposition.h"
#include "desktop.h"
#include "document-undo.h"
#include "document.h"
#include "inkscape.h"
#include "preferences.h"
#include "selection.h"
#include "style.h"
#include "ui/dialog-run.h"
#include "ui/dialog/styledialog.h"
#include "ui/icon-names.h"
#include "ui/pack.h"
#include "ui/widget/iconrenderer.h"
#include "util/trim.h"
#include "xml/attribute-record.h"
#include "xml/sp-css-attr.h"

// G_MESSAGES_DEBUG=DEBUG_SELECTORSDIALOG  gdb ./inkscape
// #define DEBUG_SELECTORSDIALOG
// #define G_LOG_DOMAIN "SELECTORSDIALOG"

using Inkscape::DocumentUndo;

namespace Inkscape::UI::Dialog {

// Keeps a watch on style element
class SelectorsDialog::NodeObserver : public Inkscape::XML::NodeObserver
{
public:
    NodeObserver(SelectorsDialog *selectorsdialog)
        : _selectorsdialog(selectorsdialog)
    {
        g_debug("SelectorsDialog::NodeObserver: Constructor");
    };

    void notifyContentChanged(Inkscape::XML::Node &node,
                              Inkscape::Util::ptr_shared old_content,
                              Inkscape::Util::ptr_shared new_content) override;

    SelectorsDialog *_selectorsdialog;
};

void SelectorsDialog::NodeObserver::notifyContentChanged(Inkscape::XML::Node &,
                                                         Inkscape::Util::ptr_shared,
                                                         Inkscape::Util::ptr_shared)
{
    g_debug("SelectorsDialog::NodeObserver::notifyContentChanged");
    _selectorsdialog->_scrollock = true;
    _selectorsdialog->_updating = false;
    _selectorsdialog->_readStyleElement();
    _selectorsdialog->_selectRow();
}

// Keeps a watch for new/removed/changed nodes
// (Must update objects that selectors match.)
class SelectorsDialog::NodeWatcher : public Inkscape::XML::NodeObserver
{
public:
    NodeWatcher(SelectorsDialog *selectorsdialog)
        : _selectorsdialog(selectorsdialog)
    {
        g_debug("SelectorsDialog::NodeWatcher: Constructor");
    };

    void notifyChildAdded(Inkscape::XML::Node &,
                          Inkscape::XML::Node &child,
                          Inkscape::XML::Node *) override
    {
        _selectorsdialog->_nodeAdded(child);
    }

    void notifyChildRemoved(Inkscape::XML::Node &,
                            Inkscape::XML::Node &child,
                            Inkscape::XML::Node *) override
    {
        _selectorsdialog->_nodeRemoved(child);
    }

    void notifyAttributeChanged(Inkscape::XML::Node &node,
                                GQuark qname,
                                Util::ptr_shared,
                                Util::ptr_shared) override
    {
        static GQuark const CODE_id = g_quark_from_static_string("id");
        static GQuark const CODE_class = g_quark_from_static_string("class");

        if (qname == CODE_id || qname == CODE_class) {
            _selectorsdialog->_nodeChanged(node);
        }
    }

    SelectorsDialog *_selectorsdialog;
};

namespace {

/// Extract a label from a CSS syntactic element, in order to have something to show in the UI.
auto extract_label(CSS::RuleStatement const &rule)
{
    return rule.selectors;
}
auto extract_label(CSS::BlockAtStatement const &block_at)
{
    return block_at.at_statement;
}
auto extract_label(CSS::OtherStatement const &other)
{
    return other;
}

constexpr int FONT_WEIGHT_NORMAL = 400;
constexpr int FONT_WEIGHT_BOLD = 700;
} // namespace

void SelectorsDialog::_nodeAdded(Inkscape::XML::Node &node)
{
    _readStyleElement();
    _selectRow();
}

void SelectorsDialog::_nodeRemoved(Inkscape::XML::Node &repr)
{
    if (_textNode == &repr) {
        _textNode = nullptr;
    }

    _readStyleElement();
    _selectRow();
}

void SelectorsDialog::_nodeChanged(Inkscape::XML::Node &object)
{
    g_debug("SelectorsDialog::NodeChanged");

    _scrollock = true;

    _readStyleElement();
    _selectRow();
}

SelectorsDialog::TreeStore::TreeStore() = default;

/**
 * Allow dragging only selectors.
 */
bool SelectorsDialog::TreeStore::row_draggable_vfunc(const Gtk::TreeModel::Path &path) const
{
    g_debug("SelectorsDialog::TreeStore::row_draggable_vfunc");

    auto unconstThis = const_cast<SelectorsDialog::TreeStore *>(this);
    const_iterator iter = unconstThis->get_iter(path);
    if (iter) {
        auto const &row = *iter;
        bool is_draggable = row[_selectorsdialog->_mColumns._colType] == SELECTOR;
        return is_draggable;
    }
    return Gtk::TreeStore::row_draggable_vfunc(path);
}

/**
 * Allow dropping only in between other selectors.
 */
bool SelectorsDialog::TreeStore::row_drop_possible_vfunc(const Gtk::TreeModel::Path &dest,
                                                         const Glib::ValueBase &) const
{
    g_debug("SelectorsDialog::TreeStore::row_drop_possible_vfunc");

    Gtk::TreeModel::Path dest_parent = dest;
    dest_parent.up();
    return dest_parent.empty();
}

// This is only here to handle updating style element after a drag and drop.
void SelectorsDialog::TreeStore::on_row_deleted(const TreeModel::Path &path)
{
    if (_selectorsdialog->_updating)
        return; // Don't write if we deleted row (other than from DND)

    g_debug("on_row_deleted");
    _selectorsdialog->_writeStyleElement();
    _selectorsdialog->_readStyleElement();
}

Glib::RefPtr<SelectorsDialog::TreeStore> SelectorsDialog::TreeStore::create(SelectorsDialog *selectorsdialog)
{
    g_debug("SelectorsDialog::TreeStore::create");

    SelectorsDialog::TreeStore *store = new SelectorsDialog::TreeStore();
    store->_selectorsdialog = selectorsdialog;
    store->set_column_types(store->_selectorsdialog->_mColumns);
    return Glib::RefPtr<SelectorsDialog::TreeStore>(store);
}

/**
 * Constructor
 * A treeview and a set of two buttons are added to the dialog. _addSelector
 * adds selectors to treeview. _delSelector deletes the selector from the dialog.
 * Any addition/deletion of the selectors updates XML style element accordingly.
 */
SelectorsDialog::SelectorsDialog()
    : DialogBase("/dialogs/selectors", "Selectors")
{
    g_debug("SelectorsDialog::SelectorsDialog");

    m_nodewatcher = std::make_unique<NodeWatcher>(this);
    m_styletextwatcher = std::make_unique<NodeObserver>(this);

    // Tree
    _store = TreeStore::create(this);
    _treeView.set_model(_store);

    auto const addRenderer = Gtk::make_managed<UI::Widget::IconRenderer>();
    addRenderer->add_icon("edit-delete");
    addRenderer->add_icon("list-add");
    addRenderer->add_icon("empty-icon");
    addRenderer->signal_activated().connect([this](Glib::ustring const &path) {
        _vscroll();
        Gtk::TreeModel::iterator iter = _store->get_iter(path);
        Gtk::TreeModel::Row row = *iter;
        if (!row.parent()) {
            _addToSelector(row);
        } else {
            _removeFromSelector(row);
        }
        _vadj->set_value(std::min(_scrollpos, _vadj->get_upper()));
        _updating = true;
        _del.set_visible(true);
        _updating = false;
        _selectRow();
    });

    // ALWAYS be a single selection widget
    _treeView.get_selection()->set_mode(Gtk::SelectionMode::SINGLE);

    _treeView.set_headers_visible(false);
    _treeView.enable_model_drag_source();
    _treeView.enable_model_drag_dest( Gdk::DragAction::MOVE );
    int addCol = _treeView.append_column("", *addRenderer) - 1;
    Gtk::TreeViewColumn *col = _treeView.get_column(addCol);
    if ( col ) {
        col->add_attribute(addRenderer->property_icon(), _mColumns._colType);
    }

    auto const label = Gtk::make_managed<Gtk::CellRendererText>();
    addCol = _treeView.append_column("CSS Selector", *label) - 1;
    col = _treeView.get_column(addCol);
    if (col) {
        col->add_attribute(label->property_text(), _mColumns._colSelector);
        col->add_attribute(label->property_weight(), _mColumns._fontWeight);
    }
    _treeView.set_expander_column(*(_treeView.get_column(1)));

    _treeView.signal_row_expanded().connect(sigc::mem_fun(*this, &SelectorsDialog::_rowExpand));
    _treeView.signal_row_collapsed().connect(sigc::mem_fun(*this, &SelectorsDialog::_rowCollapse));

    _showWidgets();
    set_visible(true);
}

void SelectorsDialog::_vscroll()
{
    if (!_scrollock) {
        _scrollpos = _vadj->get_value();
    } else {
        _vadj->set_value(_scrollpos);
        _scrollock = false;
    }
}

void SelectorsDialog::_showWidgets()
{
    // Pack widgets
    g_debug("SelectorsDialog::_showWidgets");

    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    bool dir = prefs->getBool("/dialogs/selectors/vertical", true);

    _paned.set_orientation(dir ? Gtk::Orientation::VERTICAL : Gtk::Orientation::HORIZONTAL);

    _selectors_box.set_orientation(Gtk::Orientation::VERTICAL);
    _selectors_box.set_name("SelectorsDialog");

    _scrolled_window_selectors.set_child(_treeView);
    _scrolled_window_selectors.set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
    _scrolled_window_selectors.set_overlay_scrolling(false);

    _vadj = _scrolled_window_selectors.get_vadjustment();
    _vadj->signal_value_changed().connect(sigc::mem_fun(*this, &SelectorsDialog::_vscroll));
    UI::pack_start(_selectors_box, _scrolled_window_selectors, UI::PackOptions::expand_widget);

    _styleButton(_create, "list-add", "Add a new CSS Selector");
    _create.signal_clicked().connect(sigc::mem_fun(*this, &SelectorsDialog::_addSelector));
    _styleButton(_del, "list-remove", "Remove a CSS Selector");

    UI::pack_start(_button_box, _create, UI::PackOptions::shrink);
    UI::pack_start(_button_box, _del, UI::PackOptions::shrink);

    auto const _horizontal = Gtk::make_managed<Gtk::ToggleButton>();
    auto const _vertical = Gtk::make_managed<Gtk::ToggleButton>();
    _horizontal->set_image_from_icon_name(INKSCAPE_ICON("horizontal"));
    _vertical->set_image_from_icon_name(INKSCAPE_ICON("vertical"));
    _vertical->set_group(*_horizontal);
    _vertical->set_active(dir);
    _vertical->signal_toggled().connect(
        sigc::bind(sigc::mem_fun(*this, &SelectorsDialog::_toggleDirection), _vertical));
    UI::pack_end(_button_box, *_horizontal, false, false);
    UI::pack_end(_button_box, *_vertical, false, false);

    _del.signal_clicked().connect(sigc::mem_fun(*this, &SelectorsDialog::_delSelector));
    _del.set_visible(false);

    _style_dialog = Gtk::make_managed<StyleDialog>();
    _style_dialog->set_name("StyleDialog");

    _paned.set_start_child(*_style_dialog);
    _paned.set_shrink_start_child();
    _paned.set_end_child(_selectors_box);
    _paned.set_shrink_end_child();
    _paned.set_resize_end_child();
    _paned.set_wide_handle(true);
    _paned.set_size_request(320, -1);

    auto const contents = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL);
    UI::pack_start(*contents, _paned, UI::PackOptions::expand_widget);
    UI::pack_start(*contents, _button_box, false, false);
    contents->set_valign(Gtk::Align::FILL);
    UI::pack_start(*this, *contents, UI::PackOptions::expand_widget);

    _updating = true;
    _paned.property_position() = 200;
    _updating = false;

    set_name("SelectorsAndStyleDialog");
}

void SelectorsDialog::_toggleDirection(Gtk::ToggleButton *vertical)
{
    g_debug("SelectorsDialog::_toggleDirection");
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    bool dir = vertical->get_active();
    prefs->setBool("/dialogs/selectors/vertical", dir);
    _paned.set_orientation(dir ? Gtk::Orientation::VERTICAL : Gtk::Orientation::HORIZONTAL);
    // _paned.check_resize(); // No longer needed?
    int widthpos = _paned.property_max_position() - _paned.property_min_position();
    prefs->setInt("/dialogs/selectors/panedpos", widthpos / 2);
    _paned.property_position() = widthpos / 2;
}

/**
 * @return Inkscape::XML::Node* pointing to a style element's text node.
 * Returns the style element's text node. If there is no style element, one is created.
 * Ditto for text node.
 */
Inkscape::XML::Node *SelectorsDialog::_getStyleTextNode(bool create_if_missing)
{
    g_debug("SelectorsDialog::_getStyleTextNode");

    auto const textNode = get_first_style_text_node(m_root, create_if_missing);

    if (_textNode != textNode) {
        if (_textNode) {
            _textNode->removeObserver(*m_styletextwatcher);
        }

        _textNode = textNode;

        if (_textNode) {
            _textNode->addObserver(*m_styletextwatcher);
        }
    }

    return textNode;
}

/**
 * Populate a tree row with a representation of a CSS rule statement.
 * Reminder: a rule statement is something like
 *
 * .selector1, #selector2 { fill: red; stroke-width: 2pt; }
 *
 * @param rule    The rule statement
 * @param expand  Whether the tree row should be expanded
 * @param where   Iterator to the tree row to populate
 */
void SelectorsDialog::_insertSyntacticElement(CSS::RuleStatement const &rule, bool expand,
                                              Gtk::TreeIter<Gtk::TreeRow> where)
{
    auto row = *where;
    row[_mColumns._colSelector] = rule.selectors;
    row[_mColumns._colExpand] = expand;
    row[_mColumns._colType] = SELECTOR;
    row[_mColumns._colObj] = nullptr;
    row[_mColumns._colProperties] = rule.rules;
    row[_mColumns._fontWeight] = FONT_WEIGHT_NORMAL;

    // Add objects that match the selector as children
    for (auto const &obj : _getObjVec(rule.selectors)) {
        auto const id = obj->getId();
        if (!id) {
            continue;
        }
        auto childrow = *_store->append(row.children());
        childrow[_mColumns._colSelector] = "#" + Glib::ustring(id);
        childrow[_mColumns._colExpand] = false;
        childrow[_mColumns._colType] = OBJECT;
        childrow[_mColumns._colObj] = obj;
        childrow[_mColumns._fontWeight] = FONT_WEIGHT_NORMAL;
    }
}

/**
 * Populate a tree row with a representation of a CSS block @-statement.
 * Reminder: a block @-statement is something like
 *
 * @media print {
 *    rect { fill: none; }
 *    ...
 * }
 * In particular, the block may contain further nested blocks.
 *
 * @param block_at  The block @-statement
 * @param expand    Whether the tree row should be expanded
 * @param where     Iterator to the tree row to populate
 */
void SelectorsDialog::_insertSyntacticElement(CSS::BlockAtStatement const &block_at, bool expand,
                                              Gtk::TreeIter<Gtk::TreeRow> where)
{
    auto row = *where;
    row[_mColumns._colSelector] = block_at.at_statement;
    row[_mColumns._colExpand] = expand;
    row[_mColumns._colType] = OTHER;
    row[_mColumns._colObj] = nullptr;
    row[_mColumns._fontWeight] = FONT_WEIGHT_NORMAL;

    if (block_at.block_content) {
        block_at.block_content->for_each([this, expand, subtree = row.children()](auto const &element) {
            _insertSyntacticElement(element, expand, _store->append(subtree));
        });
    }
}

/**
 * Populate a tree row with a representation of a generic ("other") CSS statement.
 * This function is used for statements other than rule set statements or block @-statements,
 * for example for an @charset statement.
 *
 * @param other  The CSS statement
 * @param where  Iterator to the tree row to populate
 */
void SelectorsDialog::_insertSyntacticElement(CSS::OtherStatement const &other, bool, Gtk::TreeIter<Gtk::TreeRow> where)
{
    auto row = *where;
    row[_mColumns._colSelector] = other;
    row[_mColumns._colExpand] = false;
    row[_mColumns._colType] = OTHER;
    row[_mColumns._colObj] = nullptr;
    row[_mColumns._fontWeight] = FONT_WEIGHT_NORMAL;
}

/**
 * Fill the internal Gtk::TreeStore from the svg:style element.
 */
void SelectorsDialog::_readStyleElement()
{
    g_debug("SelectorsDialog::_readStyleElement(): updating %s", (_updating ? "true" : "false"));

    if (_updating) return; // Don't read if we wrote style element.
    _updating = true;
    _scrollock = true;
    auto const *textNode = _getStyleTextNode();

    // Get content from style text node.
    std::string const content = (textNode && textNode->content()) ? textNode->content() : "";

    CSS::SyntacticDecomposition const syntactic_decomposition{content};
    if (syntactic_decomposition.empty()) {
        _store->clear();
        _updating = false;
        return;
    }

    // Remember the old expanded status before clearing the store
    std::map<std::string, bool> expanded_status;

    syntactic_decomposition.for_each([&, rows = _store->children(), this](auto const &element) {
        auto const label = extract_label(element);
        auto const row_with_matching_label = std::find_if(rows.begin(), rows.end(), [&label, this](auto const &row) {
            return label == Glib::ustring{row[_mColumns._colSelector]}.raw();
        });
        if (row_with_matching_label != rows.end()) {
            expanded_status.emplace(label, (*row_with_matching_label)[_mColumns._colExpand]);
        }
    });

    _store->clear();

    // Populate the tree store with representations of the CSS syntactic decomposition elements
    syntactic_decomposition.for_each([&, this](auto const &element) {
        _insertSyntacticElement(element, expanded_status[extract_label(element)], _store->append());
    });

    _updating = false;
    _scrollock = false;
    _vadj->set_value(std::min(_scrollpos, _vadj->get_upper()));

}

void SelectorsDialog::_rowExpand(const Gtk::TreeModel::iterator &iter, const Gtk::TreeModel::Path &path)
{
    g_debug("SelectorsDialog::_row_expand()");
    Gtk::TreeModel::Row row = *iter;
    row[_mColumns._colExpand] = true;
}

void SelectorsDialog::_rowCollapse(const Gtk::TreeModel::iterator &iter, const Gtk::TreeModel::Path &path)
{
    g_debug("SelectorsDialog::_row_collapse()");
    Gtk::TreeModel::Row row = *iter;
    row[_mColumns._colExpand] = false;
}

/// Return the representation of the contents of a tree row in the dialog as a CSS string.
Glib::ustring SelectorsDialog::_formatRowAsCSS(Gtk::TreeConstRow const &row) const
{
    if (row[_mColumns._colType] == SELECTOR) {
        return row[_mColumns._colSelector] + " { " + row[_mColumns._colProperties] + " }\n";
    } else if (row[_mColumns._colType] == OTHER) {
        Glib::ustring result = row[_mColumns._colSelector];

        if (!row.children().empty()) {
            result += " { ";
            for (auto const &child : row.children()) {
                result += _formatRowAsCSS(child);
            }
            result += " }";
        }
        return result + '\n';
    }
    return {};
}

/**
 * Update the content of the style element as selectors (or objects) are added/removed.
 */
void SelectorsDialog::_writeStyleElement()
{
    if (_updating) {
        return;
    }
    _scrollock = true;
    _updating = true;

    Glib::ustring style_content;
    for (auto const &row : _store->children()) {
        style_content += _formatRowAsCSS(row);
    }

    Inkscape::XML::Node *text_node = _getStyleTextNode(true);
    g_assert(text_node);
    text_node->setContent(style_content.c_str());
    DocumentUndo::done(SP_ACTIVE_DOCUMENT, _("Edited style element."), INKSCAPE_ICON("dialog-selectors"));

    _updating = false;
    _scrollock = false;
    _vadj->set_value(std::min(_scrollpos, _vadj->get_upper()));
    g_debug("SelectorsDialog::_writeStyleElement(): | %s |", style_content.c_str());
}

Glib::ustring SelectorsDialog::_getSelectorClasses(Glib::ustring selector)
{
    g_debug("SelectorsDialog::_getSelectorClasses");

    if (std::vector<Glib::ustring> tokensplus = Glib::Regex::split_simple("[ ]+", selector);
        !tokensplus.empty())
    {
        selector = std::move(tokensplus.back());
    } else {
        g_assert(!tokensplus.empty());
    }

    // Erase any comma/space
    Util::trim(selector, ",");
    Glib::ustring toparse = Glib::ustring(selector);
    selector = Glib::ustring("");
    auto i = toparse.find(".");
    if (i == std::string::npos) {
        return "";
    }
    if (toparse[0] != '.' && toparse[0] != '#') {
        i = std::min(toparse.find("#"), toparse.find("."));
        Glib::ustring tag = toparse.substr(0, i);
        if (!SPAttributeRelSVG::isSVGElement(tag)) {
            return selector;
        }
        if (i != std::string::npos) {
            toparse.erase(0, i);
        }
    }

    i = toparse.find("#");
    if (i != std::string::npos) {
        toparse.erase(i, 1);
    }

    auto j = toparse.find("#");
    if (j != std::string::npos) {
        return selector;
    }

    if (i != std::string::npos) {
        toparse.insert(i, "#");
        if (i) {
            Glib::ustring post = toparse.substr(0, i);
            Glib::ustring pre = toparse.substr(i, toparse.size() - i);
            toparse = pre + post;
        }

        auto k = toparse.find(".");
        if (k != std::string::npos) {
            toparse = toparse.substr(k, toparse.size() - k);
        }
    }

    return toparse;
}

std::vector<SPObject *> SelectorsDialog::getSelectedObjects()
{
    auto const objects = getDesktop()->getSelection()->objects();
    return std::vector<SPObject *>(objects.begin(), objects.end());
}

/**
 * @param row
 * Add selected objects on the desktop to the selector corresponding to 'row'.
 */
void SelectorsDialog::_addToSelector(Gtk::TreeModel::Row row)
{
    g_debug("SelectorsDialog::_addToSelector: Entrance");

    if (!row) return;

    // Store list of selected elements on desktop (not to be confused with selector).
    _updating = true;
    if (row[_mColumns._colType] == OTHER) {
        return;
    }

    auto const toAddObjVec = getSelectedObjects();

    Glib::ustring multiselector = row[_mColumns._colSelector];
    row[_mColumns._colExpand] = true;

    std::vector<Glib::ustring> tokens = Glib::Regex::split_simple("[,]+", multiselector);

    for (auto const &obj : toAddObjVec) {
        auto const id = obj->getId();
        if (!id)
            continue;

        for (auto const &tok : tokens) {
            Glib::ustring clases = _getSelectorClasses(tok);
            if (!clases.empty()) {
                _insertClass(obj, clases);

                std::vector<SPObject *> currentobjs = _getObjVec(multiselector);
                bool removeclass = true;
                for (auto currentobj : currentobjs) {
                    if (g_strcmp0(currentobj->getId(), id) == 0) {
                        removeclass = false;
                    }
                }
                if (removeclass) {
                    _removeClass(obj, clases);
                }
            }
        }

        std::vector<SPObject *> currentobjs = _getObjVec(multiselector);

        bool insertid = true;
        for (auto currentobj : currentobjs) {
            if (g_strcmp0(currentobj->getId(), id) == 0) {
                insertid = false;
            }
        }
        if (insertid) {
            multiselector = multiselector + ",#" + id;
        }

        auto childrow = *_store->prepend(row.children());
        childrow[_mColumns._colSelector] = "#" + Glib::ustring(id);
        childrow[_mColumns._colExpand] = false;
        childrow[_mColumns._colType] = OBJECT;
        childrow[_mColumns._colObj] = obj;
        childrow[_mColumns._fontWeight] = FONT_WEIGHT_NORMAL;
    }

    row[_mColumns._colSelector] = multiselector;
    _updating = false;

    // Add entry to style element
    for (auto const &obj : toAddObjVec) {
        SPCSSAttr *css = sp_repr_css_attr_new();
        SPCSSAttr *css_selector = sp_repr_css_attr_new();

        sp_repr_css_attr_add_from_string(css, obj->getRepr()->attribute("style"));

        Glib::ustring selprops = row[_mColumns._colProperties];

        sp_repr_css_attr_add_from_string(css_selector, selprops.c_str());

        for (const auto & iter : css_selector->attributeList()) {
            auto const key = g_quark_to_string(iter.key);
            css->removeAttribute(key);
        }

        Glib::ustring css_str;
        sp_repr_css_write_string(css, css_str);

        sp_repr_css_attr_unref(css);
        sp_repr_css_attr_unref(css_selector);

        obj->getRepr()->setAttribute("style", css_str);
        obj->style->readFromObject(obj);
        obj->requestDisplayUpdate(SP_OBJECT_MODIFIED_FLAG | SP_OBJECT_STYLE_MODIFIED_FLAG);
    }

    _writeStyleElement();
}

/**
 * @param row
 * Remove the object corresponding to 'row' from the parent selector.
 */
void SelectorsDialog::_removeFromSelector(Gtk::TreeModel::Row row)
{
    g_debug("SelectorsDialog::_removeFromSelector: Entrance");

    if (!row) return;

    _scrollock = true;
    _updating = true;
    SPObject *obj = nullptr;
    Glib::ustring objectLabel = row[_mColumns._colSelector];

    if (auto const iter = row.parent()) {
        Gtk::TreeModel::Row parent = *iter;
        Glib::ustring multiselector = parent[_mColumns._colSelector];
        Util::trim(multiselector, ",");

        obj = _getObjVec(objectLabel)[0];
        Glib::ustring selector;

        for (auto const &tok : Glib::Regex::split_simple("[,]+", multiselector)) {
            if (tok.empty()) {
                continue;
            }

            // TODO: handle when other selectors has the removed class applied to maybe not remove
            Glib::ustring clases = _getSelectorClasses(tok);
            if (!clases.empty()) {
                _removeClass(obj, tok, true);
            }

            auto i = tok.find(row[_mColumns._colSelector]);
            if (i == std::string::npos) {
                selector = selector.empty() ? tok : selector + "," + tok;
            }
        }

        Util::trim(selector);

        if (selector.empty()) {
            _store->erase(parent.get_iter());
        } else {
            _store->erase(row.get_iter());
            parent[_mColumns._colSelector] = selector;
            parent[_mColumns._colExpand] = true;
            parent[_mColumns._colObj] = nullptr;
        }
    }

    _updating = false;

    // Add entry to style element
    _writeStyleElement();
    obj->style->readFromObject(obj);
    obj->requestDisplayUpdate(SP_OBJECT_MODIFIED_FLAG | SP_OBJECT_STYLE_MODIFIED_FLAG);
    _scrollock = false;
    _vadj->set_value(std::min(_scrollpos, _vadj->get_upper()));
}

/**
 * @param sel
 * @return This function returns a comma separated list of ids for objects in input vector.
 * It is used in creating an 'id' selector. It relies on objects having 'id's.
 */
Glib::ustring SelectorsDialog::_getIdList(std::vector<SPObject *> sel)
{
    g_debug("SelectorsDialog::_getIdList");

    Glib::ustring str;
    for (auto const &obj: sel) {
        if (auto const id = obj->getId()) {
            if (!str.empty()) {
                str.append(", ");
            }
            str.append("#").append(id);
        }
    }
    return str;
}

/**
 * @param selector: a valid CSS selector string.
 * @return objVec: a vector of pointers to SPObject's the selector matches.
 * Return a vector of all objects that selector matches.
 */
std::vector<SPObject *> SelectorsDialog::_getObjVec(Glib::ustring const &selector)
{
    g_debug("SelectorsDialog::_getObjVec: | %s |", selector.c_str());

    g_assert(selector.find(";") == Glib::ustring::npos);

    return getDesktop()->getDocument()->getObjectsBySelector(selector);
}

/**
 * @param objs: list of objects to insert class
 * @param class: class to insert
 * Insert a class name into objects' 'class' attribute.
 */
void SelectorsDialog::_insertClass(const std::vector<SPObject *> &objVec, const Glib::ustring &className)
{
    g_debug("SelectorsDialog::_insertClass");

    for (auto const &obj: objVec) {
        _insertClass(obj, className);
    }
}

template <typename T, typename U>
[[nodiscard]] bool vector_contains(std::vector<T> const &haystack, U const &needle)
{
    auto const end = haystack.end();
    return std::find(haystack.begin(), end, needle) != end;
}

/**
 * @param objs: list of objects to insert class
 * @param class: class to insert
 * Insert a class name into objects' 'class' attribute.
 */
void SelectorsDialog::_insertClass(SPObject *obj, const Glib::ustring &className)
{
    g_debug("SelectorsDialog::_insertClass");

    Glib::ustring classAttr;
    if (obj->getRepr()->attribute("class")) {
        classAttr = obj->getRepr()->attribute("class");
    }

    std::vector<Glib::ustring> tokens = Glib::Regex::split_simple("[.]+", className);
    std::sort(tokens.begin(), tokens.end());
    tokens.erase(std::unique(tokens.begin(), tokens.end()), tokens.end());

    std::vector<Glib::ustring> const tokensplus = Glib::Regex::split_simple("[\\s]+", classAttr);
    for (auto const &tok : tokens) {
        bool const exist = vector_contains(tokensplus, tok);
        if (!exist) {
            classAttr = classAttr.empty() ? tok : classAttr + " " + tok;
        }
    }

    obj->getRepr()->setAttribute("class", classAttr);
}

/**
 * @param objs: list of objects to insert class
 * @param class: class to insert
 * Insert a class name into objects' 'class' attribute.
 */
void SelectorsDialog::_removeClass(const std::vector<SPObject *> &objVec, const Glib::ustring &className, bool all)
{
    g_debug("SelectorsDialog::_removeClass");

    for (auto const &obj : objVec) {
        _removeClass(obj, className, all);
    }
}

/**
 * @param objs: list of objects to insert class
 * @param class: class to insert
 * Insert a class name into objects' 'class' attribute.
 */
void SelectorsDialog::_removeClass(SPObject *obj, const Glib::ustring &className, bool all) // without "."
{
    g_debug("SelectorsDialog::_removeClass");

    if (obj->getRepr()->attribute("class")) {
        Glib::ustring classAttr = obj->getRepr()->attribute("class");
        Glib::ustring classAttrRestore = classAttr;
        bool notfound = false;

        for (auto const &tok : Glib::Regex::split_simple("[.]+", className)) {
            auto i = classAttr.find(tok);
            if (i != std::string::npos) {
                classAttr.erase(i, tok.length());
            } else {
                notfound = true;
            }
        }

        if (all && notfound) {
            classAttr = classAttrRestore;
        }

        Util::trim(classAttr, ",");

        if (classAttr.empty()) {
            obj->getRepr()->removeAttribute("class");
        } else {
            obj->getRepr()->setAttribute("class", classAttr);
        }
    }
}

/**
 * This function opens a dialog to add a selector. The dialog is prefilled
 * with an 'id' selector containing a list of the id's of selected objects
 * or with a 'class' selector if no objects are selected.
 */
void SelectorsDialog::_addSelector()
{
    g_debug("SelectorsDialog::_addSelector: Entrance");
    _scrollock = true;

    // Store list of selected elements on desktop (not to be confused with selector).
    auto const objVec = getSelectedObjects();

    // ==== Create popup dialog ====
    auto textDialogPtr = std::make_unique<Gtk::Dialog>();
    textDialogPtr->property_modal() = true;
    textDialogPtr->property_title() = _("CSS selector");
    textDialogPtr->add_button(_("Cancel"), Gtk::ResponseType::CANCEL);
    textDialogPtr->add_button(_("Add"),    Gtk::ResponseType::OK);

    auto const textEditPtr = Gtk::make_managed<Gtk::Entry>();
    textEditPtr->signal_activate().connect(
        sigc::bind(sigc::mem_fun(*this, &SelectorsDialog::_closeDialog), textDialogPtr.get()));
    UI::pack_start(*textDialogPtr->get_content_area(), *textEditPtr, UI::PackOptions::shrink);

    auto const textLabelPtr = Gtk::make_managed<Gtk::Label>(_("Invalid CSS selector."));
    UI::pack_start(*textDialogPtr->get_content_area(), *textLabelPtr, UI::PackOptions::shrink);

    /**
     * By default, the entrybox contains 'Class1' as text. However, if object(s)
     * is(are) selected and user clicks '+' at the bottom of dialog, the
     * entrybox will have the id(s) of the selected objects as text.
     */
    if (getDesktop()->getSelection()->isEmpty()) {
        textEditPtr->set_text(".Class1");
    } else {
        textEditPtr->set_text(_getIdList(objVec));
    }

    Gtk::Requisition sreq1, sreq2;
    textDialogPtr->get_preferred_size(sreq1, sreq2);
    int const minWidth  = std::max(200, sreq2.get_width());
    int const minHeight = std::max(100, sreq2.get_height());
    textDialogPtr->set_size_request(minWidth, minHeight);

    textEditPtr->set_visible(true);
    textLabelPtr->set_visible(false);
    textDialogPtr->set_visible(true);

    // ==== Get response ====
    int result = -1;
    bool invalid = true;
    Glib::ustring selectorValue;
    Glib::ustring originalValue;
    while (invalid) {
        result = dialog_run(*textDialogPtr);
        if (result != Gtk::ResponseType::OK) { // Cancel, close dialog, etc.
            return;
        }
        /**
         * @brief selectorValue
         * This string stores selector name. The text from entrybox is saved as name
         * for selector. If the entrybox is empty, the text (thus selectorName) is
         * set to ".Class1"
         */
        originalValue = textEditPtr->get_text();
        {
            std::unique_ptr<CRSelector, decltype(&cr_selector_destroy)> selector{
                cr_selector_parse_from_buf(reinterpret_cast<guchar const *>(originalValue.c_str()), CR_UTF_8),
                &cr_selector_destroy};
            if (!selector) {
                continue; // Try again on parse error
            }
            selectorValue = CSS::selector_to_validated_string(*selector);
        }
        _del.set_visible(true);
        if (originalValue.find("@import ") == std::string::npos && selectorValue.empty()) {
            textLabelPtr->set_visible(true);
        } else {
            invalid = false;
        }
    }

    // ==== Handle response ====
    // If class selector, add selector name to class attribute for each object
    Util::trim(selectorValue, ",");
    if (originalValue.find("@import ") != std::string::npos) {
        Gtk::TreeModel::Row row = *_store->prepend();
        row[_mColumns._colSelector] = originalValue;
        row[_mColumns._colExpand] = false;
        row[_mColumns._colType] = OTHER;
        row[_mColumns._colObj] = nullptr;
        row[_mColumns._fontWeight] = FONT_WEIGHT_NORMAL;
    } else {
        auto const tokens = Glib::Regex::split_simple("[,]+", selectorValue);
        for (auto const &obj : objVec) {
            for (auto const &tok : tokens) {
                Glib::ustring clases = _getSelectorClasses(tok);
                if (clases.empty()) {
                    continue;
                }

                _insertClass(obj, clases);

                auto const currentobjs = _getObjVec(selectorValue);
                bool const removeclass = !vector_contains(currentobjs, obj);
                if (removeclass) {
                    _removeClass(obj, clases);
                }
            }
        }

        Gtk::TreeModel::Row row = *(_store->prepend());
        row[_mColumns._colExpand] = true;
        row[_mColumns._colType] = SELECTOR;
        row[_mColumns._colSelector] = selectorValue;
        row[_mColumns._colObj] = nullptr;
        row[_mColumns._fontWeight] = FONT_WEIGHT_NORMAL;

        for (auto const &obj : _getObjVec(selectorValue)) {
            auto const id = obj->getId();
            if (!id)
                continue;

            auto childrow = *(_store->prepend(row.children()));
            childrow[_mColumns._colSelector] = "#" + Glib::ustring(id);
            childrow[_mColumns._colExpand] = false;
            childrow[_mColumns._colType] = OBJECT;
            childrow[_mColumns._colObj] = obj;
            childrow[_mColumns._fontWeight] = FONT_WEIGHT_NORMAL;
        }
    }

    // Add entry to style element
    _writeStyleElement();
    _scrollock = false;
    _vadj->set_value(std::min(_scrollpos, _vadj->get_upper()));
}

void SelectorsDialog::_closeDialog(Gtk::Dialog *textDialogPtr) { textDialogPtr->response(Gtk::ResponseType::OK); }

/**
 * This function deletes selector when '-' at the bottom is clicked.
 * Note: If deleting a class selector, class attributes are NOT changed.
 */
void SelectorsDialog::_delSelector()
{
    g_debug("SelectorsDialog::_delSelector");

    _scrollock = true;
    Glib::RefPtr<Gtk::TreeSelection> refTreeSelection = _treeView.get_selection();
    Gtk::TreeModel::iterator iter = refTreeSelection->get_selected();
    if (!iter) return;

    _vscroll();

    if (iter->children().size() > 2) {
        return;
    }

    _updating = true;
    _store->erase(iter);
    _updating = false;
    _writeStyleElement();
    _del.set_visible(false);
    _scrollock = false;
    _vadj->set_value(std::min(_scrollpos, _vadj->get_upper()));
}

// -------------------------------------------------------------------

class PropertyData
{
public:
    PropertyData() = default;;
    PropertyData(Glib::ustring name) : _name(std::move(name)) {};

    void _setSheetValue(Glib::ustring value) { _sheetValue = value; };
    void _setAttrValue(Glib::ustring value)  { _attrValue  = value; };
    Glib::ustring _getName()       { return _name;       };
    Glib::ustring _getSheetValue() { return _sheetValue; };
    Glib::ustring _getAttrValue()  { return _attrValue;  };

private:
    Glib::ustring _name;
    Glib::ustring _sheetValue;
    Glib::ustring _attrValue;
};

// -------------------------------------------------------------------

SelectorsDialog::~SelectorsDialog()
{
    removeObservers();
    _style_dialog->setDesktop(nullptr);
}

void SelectorsDialog::update()
{
    _style_dialog->update();
}

void SelectorsDialog::desktopReplaced()
{
    _style_dialog->setDesktop(getDesktop());
}

void SelectorsDialog::removeObservers()
{
    if (_textNode) {
        _textNode->removeObserver(*m_styletextwatcher);
        _textNode = nullptr;
    }
    if (m_root) {
        m_root->removeSubtreeObserver(*m_nodewatcher);
        m_root = nullptr;
    }
}

void SelectorsDialog::documentReplaced()
{
    removeObservers();
    if (auto document = getDocument()) {
        m_root = document->getReprRoot();
        m_root->addSubtreeObserver(*m_nodewatcher);
    }
    selectionChanged(getSelection());
}

void SelectorsDialog::selectionChanged(Selection *selection)
{
    _lastpath.clear();
    _readStyleElement();
    _selectRow();
}

/**
 * This function selects the row in treeview corresponding to an object selected
 * in the drawing. If more than one row matches, the first is chosen.
 */
void SelectorsDialog::_selectRow()
{
    g_debug("SelectorsDialog::_selectRow: updating: %s", (_updating ? "true" : "false"));

    _scrollock = true;

    _del.set_visible(false);

    std::vector<Gtk::TreeModel::Path> selectedrows = _treeView.get_selection()->get_selected_rows();
    if (selectedrows.size() == 1) {
        Gtk::TreeModel::Row row = *_store->get_iter(selectedrows[0]);
        if (!row.parent() && row.children().size() < 2) {
            _del.set_visible(true);
        }
        if (row) {
            _style_dialog->setCurrentSelector(row[_mColumns._colSelector]);
        }
    } else if (selectedrows.size() == 0) {
        _del.set_visible(true);
    }

    if (_updating || !getDesktop()) return; // Avoid updating if we have set row via dialog.

    Gtk::TreeModel::Children children = _store->children();

    Inkscape::Selection* selection = getDesktop()->getSelection();
    if (selection->isEmpty()) {
        _style_dialog->setCurrentSelector("");
    }

    for (auto &&row : children) {
        row[_mColumns._fontWeight] = FONT_WEIGHT_NORMAL;
        for (auto &&subrow : row.children()) {
            subrow[_mColumns._fontWeight] = FONT_WEIGHT_NORMAL;
        }
    }

    // Sort selection for matching.
    auto selected_objs = getSelectedObjects();
    std::sort(selected_objs.begin(), selected_objs.end());

    for (auto &&row : children) {
        if (row[_mColumns._colType] != SELECTOR) {
            continue;
        }
        // Recalculate the selector, in real time.
        auto row_children = _getObjVec(row[_mColumns._colSelector]);
        std::sort(row_children.begin(), row_children.end());

        // If all selected objects are in the css-selector, select it.
        if (row_children == selected_objs) {
            row[_mColumns._fontWeight] = FONT_WEIGHT_BOLD;
        }

        for (auto &&subrow : row.children()) {
            if (subrow[_mColumns._colObj] && selection->includes(subrow[_mColumns._colObj])) {
                subrow[_mColumns._fontWeight] = FONT_WEIGHT_BOLD;
            }
        }

        if (row[_mColumns._colExpand]) {
            _treeView.expand_to_path(_treeView.get_model()->get_path(row.get_iter()));
        }
    }

    _vadj->set_value(std::min(_scrollpos, _vadj->get_upper()));
}

/**
 * @param btn
 * @param iconName
 * @param tooltip
 * Set the style of '+' and '-' buttons at the bottom of dialog.
 */
void SelectorsDialog::_styleButton(Gtk::Button &btn, char const *iconName, char const *tooltip)
{
    g_debug("SelectorsDialog::_styleButton");

    btn.set_image_from_icon_name(iconName, Gtk::IconSize::NORMAL); // Previously GTK_ICON_SIZE_SMALL_TOOLBAR
    btn.set_has_frame(false);
    btn.set_tooltip_text (tooltip);
}

} // namespace Inkscape::UI::Dialog

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
