// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * A panel for listing objects in a document.
 *
 * Authors:
 *   Martin Owens
 *   Mike Kowalski
 *   Adam Belis (UX/Design)
 *
 * Copyright (C) Authors 2020-2022
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <cmath>
#include <string>
#include <glibmm/i18n.h>
#include <glibmm/main.h>
#include <glibmm/ustring.h>
#include <pango/pango-utils.h>
#include <gtkmm/builder.h>
#include <gtkmm/button.h>
#include <gtkmm/cellrenderer.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/dragsource.h>
#include <gtkmm/droptarget.h>
#include <gtkmm/enums.h>
#include <gtkmm/eventcontrollerkey.h>
#include <gtkmm/eventcontrollermotion.h>
#include <gtkmm/gestureclick.h>
#include <gtkmm/icontheme.h>
#include <gtkmm/popover.h>
#include <gtkmm/scale.h>
#include <gtkmm/searchentry2.h>
#include <gtkmm/separator.h>
#include <gtkmm/togglebutton.h>
#include <gtkmm/treestore.h>
#include <gtkmm/treeview.h>

#include "objects.h"
#include "actions/actions-tools.h"
#include "desktop.h"
#include "desktop-style.h"
#include "display/translucency-group.h"
#include "document.h"
#include "document-undo.h"
#include "filter-chemistry.h"
#include "inkscape.h"
#include "inkscape-window.h"
#include "layer-manager.h"
#include "message-stack.h"
#include "object/sp-root.h"
#include "object/sp-shape.h"
#include "style-enums.h"
#include "style.h"
#include "svg/css-ostringstream.h"
#include "ui/builder-utils.h"
#include "ui/contextmenu.h"
#include "ui/controller.h"
#include "ui/icon-loader.h"
#include "ui/icon-names.h"
#include "ui/pack.h"
#include "ui/popup-menu.h"
#include "ui/shortcuts.h"
#include "ui/tools/node-tool.h"
#include "ui/util.h"
#include "ui/widget/canvas.h"
#include "ui/widget/widget-vfuncs-class-init.h"
#include "ui/widget/filter-effect-chooser.h"
#include "ui/widget/imagetoggler.h"
#include "ui/widget/objects-dialog-cells.h"
#include "ui/widget/shapeicon.h"
#include "util/numeric/converters.h"

// alpha (transparency) multipliers corresponding to item selection state combinations (SelectionState)
// when 0 - do not color item's background
static double const SELECTED_ALPHA[16] = {
    0.00, //00 not selected
    0.90, //01 selected
    0.50, //02 layer focused
    0.20, //03 layer focused & selected
    0.00, //04 child of focused layer
    0.90, //05 selected child of focused layer
    0.50, //06 2 and 4
    0.90, //07 1, 2 and 4
    0.40, //08 child of selected group
    0.90, //09 1 and 8
    0.40, //10 2 and 8
    0.90, //11 1, 2 and 8
    0.40, //12 4 and 8
    0.90, //13 1, 4 and 8
    0.40, //14 2, 4 and 8
    0.90, //15 1, 2 , 4 and 8
};

static double const HOVER_ALPHA = 0.10;

namespace Inkscape::UI::Dialog {

namespace {

void connect_on_window_when_mapped(Glib::RefPtr<Gtk::EventController> controller, Gtk::Widget &widget)
{
    auto const on_map = [controller, &widget] {
        auto& window = dynamic_cast<Gtk::Window &>(*widget.get_root());
        window.add_controller(controller);
    };
    auto const on_unmap = [controller, &widget] {
        auto& window = dynamic_cast<Gtk::Window &>(*widget.get_root());
        window.remove_controller(controller);
    };
    widget.signal_map().connect(on_map);
    widget.signal_unmap().connect(on_unmap);
}

} // namespace

using Inkscape::XML::Node;
using namespace Inkscape::UI::Widget;

// This was the 1 widget where we used signal_style_updated(), so just hack together a replacement!
class ObjectsPanel::TreeViewWithCssChanged final
    : public WidgetVfuncsClassInit
    , public Gtk::TreeView
{
public:
    TreeViewWithCssChanged()
        : Glib::ObjectBase{"TreeViewWithCssChanged"}
        , WidgetVfuncsClassInit{}
        , Gtk::TreeView{}
    {
    }

    auto connect_css_changed(sigc::slot<void (GtkCssStyleChange *)> slot)
    {
        return _signal.connect(std::move(slot));
    }

private:
    sigc::signal<void (GtkCssStyleChange *)> _signal;

    void css_changed(GtkCssStyleChange * const change) final
    {
        _signal.emit(change);
    }
};

class ObjectWatcher : public Inkscape::XML::NodeObserver
{
public:
    ObjectWatcher(ObjectsPanel *panel, SPItem *, Gtk::TreeRow *row, bool is_filtered);
    ~ObjectWatcher() override;

    void initRowInfo();
    void updateRowInfo();
    void updateRowHighlight();
    void updateRowAncestorState(bool invisible, bool locked);
    void updateRowBg(guint32 rgba = 0.0);

    ObjectWatcher *findChild(Node *node);
    void addDummyChild();
    bool addChild(SPItem *, bool dummy = true);
    void addChildren(SPItem *, bool dummy = false);
    void setSelectedBit(SelectionState mask, bool enabled);
    void setSelectedBitRecursive(SelectionState mask, bool enabled);
    void setSelectedBitChildren(SelectionState mask, bool enabled);
    void rememberExtendedItems();
    void moveChild(Node &child, Node *sibling);
    bool isFiltered() const { return is_filtered; }

    Gtk::TreeNodeChildren getChildren() const;
    Gtk::TreeModel::iterator getChildIter(Node *) const;

    void notifyChildRemoved(Node &, Node &, Node *) final;
    void notifyChildOrderChanged(Node &, Node &child, Node *, Node *) final;
    void notifyChildAdded(Node &, Node &, Node *) final;
    void notifyAttributeChanged(Node &, GQuark, Util::ptr_shared, Util::ptr_shared) final;

    /// Associate this watcher with a tree row
    void setRow(const Gtk::TreeModel::Path &path)
    {
        assert(path);
        row_ref = Gtk::TreeModel::RowReference(panel->_store, path);
    }
    void setRow(const Gtk::TreeModel::Row &row)
    {
        setRow(panel->_store->get_path(row.get_iter()));
    }

    // Get the path out of this watcher
    Gtk::TreeModel::Path getTreePath() const {
        if (!row_ref)
            return {};
        return row_ref.get_path();
    }

    /// True if this watchr has a valid row reference.
    bool hasRow() const { return bool(row_ref); }

    /// Transfer a child watcher to its new parent
    void transferChild(Node *childnode)
    {
        auto *target = panel->getWatcher(childnode->parent());
        assert(target != this);
        auto nh = child_watchers.extract(childnode);
        assert(nh);
        bool inserted = target->child_watchers.insert(std::move(nh)).inserted;
        assert(inserted);
    }

    /// The XML node associated with this watcher.
    Node *getRepr() const { return node; }
    std::optional<Gtk::TreeRow> getRow() const {
        if (auto path = row_ref.get_path()) {
            if(auto iter = panel->_store->get_iter(path)) {
                return *iter;
            }
        }
        return std::nullopt;
    }

    std::unordered_map<Node const *, std::unique_ptr<ObjectWatcher>> child_watchers;

private:
    Node *node;
    Gtk::TreeModel::RowReference row_ref;
    ObjectsPanel *panel;
    SelectionState selection_state;
    bool is_filtered;
};

class ObjectsPanel::ModelColumns final : public Gtk::TreeModel::ColumnRecord
{
public:
    ModelColumns()
    {
        add(_colNode);
        add(_colLabel);
        add(_colType);
        add(_colIconColor);
        add(_colClipMask);
        add(_colBgColor);
        add(_colInvisible);
        add(_colLocked);
        add(_colAncestorInvisible);
        add(_colAncestorLocked);
        add(_colHover);
        add(_colItemStateSet);
        add(_colBlendMode);
        add(_colOpacity);
        add(_colItemState);
        add(_colHoverColor);
    }

    Gtk::TreeModelColumn<Node*> _colNode;
    Gtk::TreeModelColumn<Glib::ustring> _colLabel;
    Gtk::TreeModelColumn<Glib::ustring> _colType;
    Gtk::TreeModelColumn<unsigned int> _colIconColor;
    Gtk::TreeModelColumn<unsigned int> _colClipMask;
    Gtk::TreeModelColumn<Gdk::RGBA> _colBgColor;
    Gtk::TreeModelColumn<bool> _colInvisible;
    Gtk::TreeModelColumn<bool> _colLocked;
    Gtk::TreeModelColumn<bool> _colAncestorInvisible;
    Gtk::TreeModelColumn<bool> _colAncestorLocked;
    Gtk::TreeModelColumn<bool> _colHover;
    Gtk::TreeModelColumn<bool> _colItemStateSet;
    Gtk::TreeModelColumn<SPBlendMode> _colBlendMode;
    Gtk::TreeModelColumn<double> _colOpacity;
    Gtk::TreeModelColumn<Glib::ustring> _colItemState;
    // Set when hovering over the color tag cell
    Gtk::TreeModelColumn<bool> _colHoverColor;
};

/**
 * Creates a new ObjectWatcher, a gtk TreeView iterated watching device.
 *
 * @param panel The panel to which the object watcher belongs
 * @param obj The SPItem to watch in the document
 * @param row The optional list store tree row for the item,
          if not provided, assumes this is the root 'document' object.
 * @param filtered, if true this watcher will filter all chldren using the panel filtering function on each item to decide if it should be shown.
 */
ObjectWatcher::ObjectWatcher(ObjectsPanel* panel, SPItem* obj, Gtk::TreeRow *row, bool filtered)
    : panel(panel)
    , row_ref()
    , selection_state(0)
    , is_filtered(filtered)
    , node(obj->getRepr())
{
    if(row != nullptr) {
        assert(row->children().empty());
        setRow(*row);
        initRowInfo();
        updateRowInfo();
    }
    node->addObserver(*this);

    // Only show children for groups (and their subclasses like SPAnchor or SPRoot)
    if (!is<SPGroup>(obj)) {
        return;
    }

    // Add children as a dummy row to avoid excensive execution when
    // the tree is really large, but not in layers mode.
    addChildren(obj, (bool)row && !obj->isExpanded());
}

ObjectWatcher::~ObjectWatcher()
{
    node->removeObserver(*this);
    Gtk::TreeModel::Path path;
    if (bool(row_ref) && (path = row_ref.get_path())) {
        if (auto iter = panel->_store->get_iter(path)) {
            panel->_store->erase(iter);
        }
    }
    child_watchers.clear();
}

void ObjectWatcher::initRowInfo()
{
    auto const _model = panel->_model.get();
    auto row = *panel->_store->get_iter(row_ref.get_path());
    row[_model->_colHover] = false;
}

/**
 * Update the information in the row from the stored node
 */
void ObjectWatcher::updateRowInfo()
{
    if (auto item = cast<SPItem>(panel->getObject(node))) {
        assert(row_ref);
        assert(row_ref.get_path());

        auto const _model = panel->_model.get();
        auto row = *panel->_store->get_iter(row_ref.get_path());
        row[_model->_colNode] = node;

        // show ids without "#"
        char const *id = item->getId();
        row[_model->_colLabel] = (id && !item->label()) ? id : item->defaultLabel();

        row[_model->_colType] = item->typeName();
        row[_model->_colClipMask] =
            (item->getClipObject() ? Inkscape::UI::Widget::OVERLAY_CLIP : 0) |
            (item->getMaskObject() ? Inkscape::UI::Widget::OVERLAY_MASK : 0);
        row[_model->_colInvisible] = item->isHidden();
        row[_model->_colLocked] = !item->isSensitive();
        auto blend = item->style && item->style->mix_blend_mode.set ? item->style->mix_blend_mode.value : SP_CSS_BLEND_NORMAL;
        row[_model->_colBlendMode] = blend;
        auto opacity = 1.0;
        if (item->style && item->style->opacity.set) {
            opacity = SP_SCALE24_TO_FLOAT(item->style->opacity.value);
        }
        row[_model->_colOpacity] = opacity;
        std::string item_state;
        if (opacity == 0.0) {
            item_state = "object-transparent";
        }
        else if (blend != SP_CSS_BLEND_NORMAL) {
            item_state = opacity == 1.0 ? "object-blend-mode" : "object-translucent-blend-mode";
        }
        else if (opacity < 1.0) {
            item_state = "object-translucent";
        }
        row[_model->_colItemState] = item_state;
        row[_model->_colItemStateSet] = !item_state.empty();

        updateRowHighlight();
        updateRowAncestorState(row[_model->_colAncestorInvisible], row[_model->_colAncestorLocked]);
    }
}

/**
 * Propagate changes to the highlight color to all children.
 */
void ObjectWatcher::updateRowHighlight() {

    if (!hasRow()) {
        std::cerr << "ObjectWatcher::updateRowHighlight: no row_ref: " << node->name() << std::endl;
        return;
    }

    if (auto item = cast<SPItem>(panel->getObject(node))) {
        auto row = *panel->_store->get_iter(row_ref.get_path());
        auto new_color = item->highlight_color().toRGBA();
        if (new_color != row[panel->_model->_colIconColor]) {
            row[panel->_model->_colIconColor] = new_color;
            updateRowBg(new_color);
            for (auto &watcher : child_watchers) {
                watcher.second->updateRowHighlight();
            }
        }
    }
}

/**
 * Propagate a change in visibility or locked state to all children
 */
void ObjectWatcher::updateRowAncestorState(bool invisible, bool locked) {
    auto const _model = panel->_model.get();
    auto row = *panel->_store->get_iter(row_ref.get_path());
    row[_model->_colAncestorInvisible] = invisible;
    row[_model->_colAncestorLocked] = locked;
    for (auto &watcher : child_watchers) {
        watcher.second->updateRowAncestorState(
            invisible || row[_model->_colInvisible],
            locked || row[_model->_colLocked]);
    }
}

Gdk::RGBA selection_color;

/**
 * Updates the row's background colour as indicated by its selection.
 */
void ObjectWatcher::updateRowBg(guint32 rgba)
{
    assert(row_ref);
    if (auto row = *panel->_store->get_iter(row_ref.get_path())) {
        auto alpha = SELECTED_ALPHA[selection_state];
        if (alpha == 0.0) {
            row[panel->_model->_colBgColor] = Gdk::RGBA();
            return;
        }

        const auto& sel = selection_color;
        const auto gdk_color = change_alpha(sel, sel.get_alpha() * alpha);
        row[panel->_model->_colBgColor] = gdk_color;
    }
}

/**
 * Flip the selected state bit on or off as needed, calls updateRowBg if changed.
 *
 * @param mask - The selection bit to set or unset
 * @param enabled - If the bit should be set or unset
 */
void ObjectWatcher::setSelectedBit(SelectionState mask, bool enabled) {
    if (!row_ref) return;
    SelectionState value = selection_state;
    SelectionState original = value;
    if (enabled) {
        value |= mask;
    } else {
        value &= ~mask;
    }
    if (value != original) {
        selection_state = value;
        updateRowBg();
    }
}

/**
 * Flip the selected state bit on or off as needed, on this watcher and all
 * its direct and indirect children.
 */
void ObjectWatcher::setSelectedBitRecursive(SelectionState mask, bool enabled)
{
    setSelectedBit(mask, enabled);
    setSelectedBitChildren(mask, enabled);
}
void ObjectWatcher::setSelectedBitChildren(SelectionState mask, bool enabled)
{
    for (auto &pair : child_watchers) {
        pair.second->setSelectedBitRecursive(mask, enabled);
    }
}

/**
 * Keep expanded rows expanded and recurse through all children.
 */
void ObjectWatcher::rememberExtendedItems()
{
    if (auto item = cast<SPItem>(panel->getObject(node))) {
        if (item->isExpanded())
            panel->_tree.expand_row(row_ref.get_path(), false);
    }
    for (auto &pair : child_watchers) {
        pair.second->rememberExtendedItems();
    }
}

/**
 * Find the child watcher for the given node.
 */
ObjectWatcher *ObjectWatcher::findChild(Node *node)
{
    auto it = child_watchers.find(node);
    if (it != child_watchers.end()) {
        return it->second.get();
    }
    return nullptr;
}

/**
 * Add the child object to this node.
 *
 * @param child - SPObject to be added
 * @param dummy - Add a dummy objects (hidden) instead
 *
 * @returns true if child added was a dummy objects
 */
bool ObjectWatcher::addChild(SPItem *child, bool dummy)
{
    if (is_filtered && !panel->showChildInTree(child)) {
        return false;
    }

    auto children = getChildren();
    if (!is_filtered && dummy && row_ref) {
        if (children.empty()) {
            auto const iter = panel->_store->append(children);
            assert(panel->isDummy(*iter));
            return true;
        } else if (panel->isDummy(children[0])) {
            return false;
        }
    }

    auto *node = child->getRepr();
    assert(node);
    Gtk::TreeModel::Row row = *(panel->_store->prepend(children));

    // Ancestor states are handled inside the list store (so we don't have to re-ask every update)
    auto const _model = panel->_model.get();
    if (row_ref) {
        auto parent_row = *panel->_store->get_iter(row_ref.get_path());
        row[_model->_colAncestorInvisible] = parent_row[_model->_colAncestorInvisible] || parent_row[_model->_colInvisible];
        row[_model->_colAncestorLocked] = parent_row[_model->_colAncestorLocked] || parent_row[_model->_colLocked];
    } else {
        row[_model->_colAncestorInvisible] = false;
        row[_model->_colAncestorLocked] = false;
    }

    auto &watcher = child_watchers[node];
    assert(!watcher);
    watcher.reset(new ObjectWatcher(panel, child, &row, is_filtered));

    // Make sure new children have the right focus set.
    if ((selection_state & LAYER_FOCUSED) != 0) {
        watcher->setSelectedBit(LAYER_FOCUS_CHILD, true);
    }
    return false;
}

/**
 * Add all SPItem children as child rows.
 */
void ObjectWatcher::addChildren(SPItem *obj, bool dummy)
{
    assert(child_watchers.empty());

    for (auto &child : obj->children) {
        if (auto item = cast<SPItem>(&child)) {
            if (addChild(item, dummy) && dummy) {
                // one dummy child is enough to make the group expandable
                break;
            }
        }
    }
}

/**
 * Move the child to just after the given sibling
 *
 * @param child - SPObject to be moved
 * @param sibling - Optional sibling Object to add next to, if nullptr the
 *                  object is moved to BEFORE the first item.
 */
void ObjectWatcher::moveChild(Node &child, Node *sibling)
{
    auto child_iter = getChildIter(&child);
    if (!child_iter)
        return; // This means the child was never added, probably not an SPItem.

    // sibling might not be an SPItem and thus not be represented in the
    // TreeView. Find the closest SPItem and use that for the reordering.
    while (sibling && !is<SPItem>(panel->getObject(sibling))) {
        sibling = sibling->prev();
    }

    auto sibling_iter = getChildIter(sibling);
    panel->_store->move(child_iter, sibling_iter);
}

/**
 * Get the TreeRow's children iterator
 *
 * @returns Gtk Tree Node Children iterator
 */
Gtk::TreeNodeChildren ObjectWatcher::getChildren() const
{
    Gtk::TreeModel::Path path;
    if (row_ref && (path = row_ref.get_path())) {
        return panel->_store->get_iter(path)->children();
    }
    assert(!row_ref);
    return panel->_store->children();
}

/**
 * Convert SPObject to TreeView Row, assuming the object is a child.
 *
 * @param child - The child object to find in this branch
 * @returns Gtk TreeRow for the child, or end() if not found
 */
Gtk::TreeModel::iterator ObjectWatcher::getChildIter(Node *node) const
{
    auto childrows = getChildren();

    if (!node) {
        return childrows.end();
    }

    for (auto &row : childrows) {
        if (panel->getRepr(row) == node) {
            return row.get_iter();
        }
    }
    // In layer mode, we will come here for all non-layers
    return childrows.begin();
}

void ObjectWatcher::notifyChildAdded( Node &node, Node &child, Node *prev )
{
    assert(this->node == &node);
    // Ignore XML nodes which are not displayable items
    if (auto item = cast<SPItem>(panel->getObject(&child))) {
        addChild(item);
        moveChild(child, prev);
    }
}
void ObjectWatcher::notifyChildRemoved( Node &node, Node &child, Node* /*prev*/ )
{
    assert(this->node == &node);

    if (child_watchers.erase(&child) > 0) {
        return;
    }

    if (node.firstChild() == nullptr) {
        assert(row_ref);
        auto iter = panel->_store->get_iter(row_ref.get_path());
        panel->removeDummyChildren(*iter);
    }
}
void ObjectWatcher::notifyChildOrderChanged( Node &parent, Node &child, Node */*old_prev*/, Node *new_prev )
{
    assert(this->node == &parent);

    moveChild(child, new_prev);
}
void ObjectWatcher::notifyAttributeChanged( Node &node, GQuark name, Util::ptr_shared /*old_value*/, Util::ptr_shared /*new_value*/ )
{
    assert(this->node == &node);

    // The root <svg> node doesn't have a row
    if (this == panel->getRootWatcher()) {
        return;
    }

    // Almost anything could change the icon, so update upon any change, defer for lots of updates.

    // examples of not-so-obvious cases:
    // - width/height: Can change type "circle" to an "ellipse"

    static std::set<GQuark> const excluded{
        g_quark_from_static_string("transform"),
        g_quark_from_static_string("x"),
        g_quark_from_static_string("y"),
        g_quark_from_static_string("d"),
        g_quark_from_static_string("sodipodi:nodetypes"),
    };

    if (excluded.count(name)) {
        return;
    }

    updateRowInfo();
}

/**
 * Get the object from the node.
 *
 * @param node - XML Node involved in the signal.
 * @returns SPObject matching the node, returns nullptr if not found.
 */
SPObject *ObjectsPanel::getObject(Node *node) {
    if (node != nullptr && getDocument())
        return getDocument()->getObjectByRepr(node);
    return nullptr;
}

/**
 * Get the object watcher from the xml node (reverse lookup), it uses a ancesstor
 * recursive pattern to match up with the root_watcher.
 *
 * @param node - The node to look up.
 * @return the ObjectWatcher object if it's possible to find.
 */
ObjectWatcher* ObjectsPanel::getWatcher(Node *node)
{
    assert(node);

    if (root_watcher->getRepr() == node) {
        return root_watcher.get();
    }

    if (node->parent()) {
        if (auto parent_watcher = getWatcher(node->parent())) {
            return parent_watcher->findChild(node);
        }
    }

    return nullptr;
}

/**
 * Constructor
 */
ObjectsPanel::ObjectsPanel()
    : DialogBase("/dialogs/objects", "Objects")
    , _model{std::make_unique<ModelColumns>()}
    , _layer(nullptr)
    , _is_editing(false)
    , _page(Gtk::Orientation::VERTICAL)
    , _builder(create_builder("dialog-objects.glade"))
    , _settings_menu(get_widget<Gtk::Popover>(_builder, "settings-menu"))
    , _object_menu(get_widget<Gtk::Popover>(_builder, "object-menu"))
    , _colors(std::make_shared<Colors::ColorSet>(nullptr, false))
    , _searchBox(get_widget<Gtk::SearchEntry2>(_builder, "search"))
    , _opacity_slider(get_widget<Gtk::Scale>(_builder, "opacity-slider"))
    , _setting_layers(get_derived_widget<PrefCheckButton, Glib::ustring, bool>(_builder, "setting-layers", "/dialogs/objects/layers_only", false))
    , _setting_track(get_derived_widget<PrefCheckButton, Glib::ustring, bool>(_builder, "setting-track", "/dialogs/objects/expand_to_layer", true))
    , _tree{*Gtk::make_managed<TreeViewWithCssChanged>()}
{
    _store = Gtk::TreeStore::create(*_model);

    //Set up the tree
    _tree.set_model(_store);
    _tree.set_headers_visible(false);
    _tree.set_reorderable(false); // Don't interfere with D&D via controllers!
    _tree.set_name("ObjectsTreeView");

    auto& header = get_widget<Gtk::Box>(_builder, "header");
    // Search
    _searchBox.signal_search_changed().connect(sigc::mem_fun(*this, &ObjectsPanel::_searchActivated));

    // Buttons
    auto& _move_up_button = get_widget<Gtk::Button>(_builder, "move-up");
    auto& _move_down_button = get_widget<Gtk::Button>(_builder, "move-down");
    auto& _object_delete_button = get_widget<Gtk::Button>(_builder, "remove-object");
    _move_up_button.signal_clicked().connect([this]() {
        _activateAction("win.layer-raise", "selection-stack-up");
    });
    _move_down_button.signal_clicked().connect([this]() {
        _activateAction("win.layer-lower", "selection-stack-down");
    });
    _object_delete_button.signal_clicked().connect([this]() {
        _activateAction("win.layer-delete", "delete-selection");
    });

    //Label
    _name_column = Gtk::make_managed<Gtk::TreeViewColumn>();
    _text_renderer = Gtk::make_managed<Gtk::CellRendererText>();
    _text_renderer->property_editable() = true;
    _text_renderer->property_ellipsize().set_value(Pango::EllipsizeMode::END);
    _text_renderer->signal_editing_started().connect([this](Gtk::CellEditable*,const Glib::ustring&){
        _is_editing = true;
    });
    _text_renderer->signal_editing_canceled().connect([this](){
        _is_editing = false;
    });
    _text_renderer->signal_edited().connect([this](const Glib::ustring&,const Glib::ustring&){
        _is_editing = false;
    });

    const int icon_col_width = 24;
    auto const icon_renderer = Gtk::make_managed<Inkscape::UI::Widget::CellRendererItemIcon>();
    icon_renderer->property_xpad() = 2;
    icon_renderer->property_width() = icon_col_width;
    _tree.append_column(*_name_column);
    _name_column->set_expand(true);
    _name_column->pack_start(*icon_renderer, false);
    _name_column->pack_start(*_text_renderer, true);
    _name_column->add_attribute(_text_renderer->property_text(), _model->_colLabel);
    _name_column->add_attribute(_text_renderer->property_cell_background_rgba(), _model->_colBgColor);
    _name_column->add_attribute(icon_renderer->property_shape_type(), _model->_colType);
    _name_column->add_attribute(icon_renderer->property_color(), _model->_colIconColor);
    _name_column->add_attribute(icon_renderer->property_clipmask(), _model->_colClipMask);
    _name_column->add_attribute(icon_renderer->property_cell_background_rgba(), _model->_colBgColor);

    // blend mode and opacity icon(s)
    _item_state_toggler = Gtk::make_managed<UI::Widget::ImageToggler>(
        INKSCAPE_ICON("object-blend-mode"), INKSCAPE_ICON("object-opaque"));
    int modeColNum = _tree.append_column("mode", *_item_state_toggler) - 1;
    if (auto col = _tree.get_column(modeColNum)) {
        col->add_attribute(_item_state_toggler->property_active(), _model->_colItemStateSet);
        col->add_attribute(_item_state_toggler->property_active_icon(), _model->_colItemState);
        col->add_attribute(_item_state_toggler->property_cell_background_rgba(), _model->_colBgColor);
        col->add_attribute(_item_state_toggler->property_activatable(), _model->_colHover);
        col->set_fixed_width(icon_col_width);
        _blend_mode_column = col;
    }

    _tree.signal_query_tooltip().connect([this](int x, int y, bool kbd, const Glib::RefPtr<Gtk::Tooltip>& tooltip){
        Gtk::TreeModel::iterator iter;
        if (!_tree.get_tooltip_context_iter(x, y, kbd, iter) || !iter) {
            return false;
        }
        auto blend = (*iter)[_model->_colBlendMode];
        auto opacity = (*iter)[_model->_colOpacity];
        auto templt = !pango_version_check(1, 50, 0) ?
            "<span>%1 %2%%\n</span><span line_height=\"0.5\">\n</span><span>%3\n<i>%4</i></span>" :
            "<span>%1 %2%%\n</span><span>\n</span><span>%3\n<i>%4</i></span>";
        auto label = Glib::ustring::compose(templt,
            _("Opacity:"), Util::format_number(opacity * 100.0, 1),
            _("Blend mode:"), _blend_mode_names[blend]
        );
        tooltip->set_markup(label);
        _tree.set_tooltip_cell(tooltip, nullptr, _blend_mode_column, _item_state_toggler);
        return true;
    }, false); // before

    _object_menu.signal_closed().connect([this]{
        _item_state_toggler->set_force_visible(false);
        _tree.queue_draw();
    });

    auto& modes = get_widget<Gtk::Grid>(_builder, "modes");
    _opacity_slider.set_format_value_func([](double const val){
        return Util::format_number(val, 1) + "%";
    });
    const int min = 0, max = 100;
    for (int i = min; i <= max; i += 50) {
        _opacity_slider.add_mark(i, Gtk::PositionType::BOTTOM, "");
    }
    _opacity_slider.signal_value_changed().connect([this](){
        if (current_item) {
            auto value = _opacity_slider.get_value() / 100.0;
            Inkscape::CSSOStringStream os;
            os << CLAMP(value, 0.0, 1.0);
            auto css = sp_repr_css_attr_new();
            sp_repr_css_set_property(css, "opacity", os.str().c_str());
            current_item->changeCSS(css, "style");
            sp_repr_css_attr_unref(css);
            DocumentUndo::maybeDone(current_item->document, ":opacity", _("Change opacity"), INKSCAPE_ICON("dialog-object-properties"));
        }
    });

    // object blend mode and opacity popup
    Gtk::CheckButton *group = nullptr;
    int top = 0;
    int left = 0;
    int width = 2;
    for (size_t i = 0; i < Inkscape::SPBlendModeConverter._length; ++i) {
        auto& data = Inkscape::SPBlendModeConverter.data(i);
        auto label = _blend_mode_names[data.id] = g_dpgettext2(nullptr, "BlendMode", data.label.c_str());
        if (Inkscape::SPBlendModeConverter.get_key(data.id) == "-") {
            if (top >= (Inkscape::SPBlendModeConverter._length + 1) / 2) {
                ++left;
                top = 2;
            } else if (!left) {
                auto const sep = Gtk::make_managed<Gtk::Separator>();
                sep->set_visible(true);
                modes.attach(*sep, left, top, 2, 1);
            }
        } else {
            // Manual correction that indicates this should all be done in glade
            if (left == 1 && top == 9)
                top++;

            auto const check = Gtk::make_managed<Gtk::CheckButton>(label);
            if (!group) group = check;
            else check->set_group(*group);
            check->set_halign(Gtk::Align::START);
            check->signal_toggled().connect([=, this]{
                if (!check->get_active()) return;
                // set blending mode
                if (set_blend_mode(current_item, data.id)) {
                    for (auto const &btn : _blend_items) {
                        btn.second->property_active().set_value(btn.first == data.id);
                    }
                    DocumentUndo::done(getDocument(), "set-blend-mode", _("Change blend mode"));
                }
            });
            _blend_items[data.id] = check;
            _blend_mode_names[data.id] = label;
            check->set_visible(true);
            modes.attach(*check, left, top, width, 1);
            width = 1; // First element takes whole width
        }
        top++;
    }

    // Visible icon
    auto const eyeRenderer = Gtk::make_managed<UI::Widget::ImageToggler>(
            INKSCAPE_ICON("object-hidden"), INKSCAPE_ICON("object-visible"));
    int visibleColNum = _tree.append_column("vis", *eyeRenderer) - 1;
    if (auto eye = _tree.get_column(visibleColNum)) {
        eye->add_attribute(eyeRenderer->property_active(), _model->_colInvisible);
        eye->add_attribute(eyeRenderer->property_cell_background_rgba(), _model->_colBgColor);
        eye->add_attribute(eyeRenderer->property_activatable(), _model->_colHover);
        eye->add_attribute(eyeRenderer->property_gossamer(), _model->_colAncestorInvisible);
        eye->set_fixed_width(icon_col_width);
        _eye_column = eye;
    }

    // Unlocked icon
    auto const lockRenderer = Gtk::make_managed<UI::Widget::ImageToggler>(
        INKSCAPE_ICON("object-locked"), INKSCAPE_ICON("object-unlocked"));
    int lockedColNum = _tree.append_column("lock", *lockRenderer) - 1;
    if (auto lock = _tree.get_column(lockedColNum)) {
        lock->add_attribute(lockRenderer->property_active(), _model->_colLocked);
        lock->add_attribute(lockRenderer->property_cell_background_rgba(), _model->_colBgColor);
        lock->add_attribute(lockRenderer->property_activatable(), _model->_colHover);
        lock->add_attribute(lockRenderer->property_gossamer(), _model->_colAncestorLocked);
        lock->set_fixed_width(icon_col_width);
        _lock_column = lock;
    }

    // hierarchy indicator - using item's layer highlight color
    auto const tag_renderer = Gtk::make_managed<Inkscape::UI::Widget::ColorTagRenderer>();
    int tag_column = _tree.append_column("tag", *tag_renderer) - 1;
    if (auto tag = _tree.get_column(tag_column)) {
        tag->add_attribute(tag_renderer->property_color(), _model->_colIconColor);
        tag->add_attribute(tag_renderer->property_hover(), _model->_colHoverColor);
        tag->set_fixed_width(tag_renderer->get_width());
        _color_tag_column = tag;
    }

    //Set the expander columns and search columns
    _tree.set_expander_column(*_name_column);
    _tree.set_search_column(-1);
    _tree.set_enable_search(false);
    _tree.get_selection()->set_mode(Gtk::SelectionMode::NONE);

    //Set up tree signals
    auto const click = Gtk::GestureClick::create();
    click->set_button(0); // any
    click->set_propagation_phase(Gtk::PropagationPhase::TARGET);
    click->signal_pressed().connect(Controller::use_state([this](auto &&...args) { return on_click(args..., EventType::pressed); }, *click));
    click->signal_released().connect(Controller::use_state([this](auto &&...args) { return on_click(args..., EventType::released); }, *click));
    _tree.add_controller(click);

    auto const key = Gtk::EventControllerKey::create();
    key->signal_key_pressed().connect([this, &key = *key](auto &&...args) { return on_tree_key_pressed(key, args...); }, true);
    _tree.add_controller(key);

    auto const motion = Gtk::EventControllerMotion::create();
    motion->set_propagation_phase(Gtk::PropagationPhase::TARGET);
    motion->signal_enter().connect(sigc::mem_fun(*this, &ObjectsPanel::on_motion_enter));
    motion->signal_leave().connect(sigc::mem_fun(*this, &ObjectsPanel::on_motion_leave));
    motion->signal_motion().connect([this, &motion = *motion](auto &&...args) { on_motion_motion(&motion, args...); });
    _tree.add_controller(motion);

    // Track Alt key on parent window so we don't need to have key focus to work
    auto const window_key = Gtk::EventControllerKey::create();
    window_key->signal_key_pressed().connect([this, &window_key = *window_key](auto &&...args) { return on_window_key(window_key, args..., EventType::pressed); }, true);
    window_key->signal_key_released().connect([this, &window_key = *window_key](auto &&...args) { on_window_key(window_key, args..., EventType::released); });
    connect_on_window_when_mapped(window_key, _tree);

    // Before expanding a row, replace the dummy child with the actual children
    _tree.signal_test_expand_row().connect([this](const Gtk::TreeModel::iterator &iter, const Gtk::TreeModel::Path &) {
        if (cleanDummyChildren(*iter)) {
            if (getSelection()) {
                _selectionChanged();
            }
        }
        return false;
    }, false); // before
    _tree.signal_row_expanded().connect([this](const Gtk::TreeModel::iterator &iter, const Gtk::TreeModel::Path &) {
        if (auto item = getItem(*iter)) {
            item->setExpanded(true);
        }
    });
    _tree.signal_row_collapsed().connect([this](const Gtk::TreeModel::iterator &iter, const Gtk::TreeModel::Path &) {
        if (auto item = getItem(*iter)) {
            item->setExpanded(false);
        }
    });

    auto const drag = Gtk::DragSource::create();
    drag->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
    drag->set_actions(Gdk::DragAction::MOVE);
    drag->signal_prepare().connect([this, &drag = *drag](auto &&...args) { return on_prepare(drag, args...); }, false); // before
    drag->signal_drag_begin().connect(sigc::mem_fun(*this, &ObjectsPanel::on_drag_begin));
    drag->signal_drag_end().connect(sigc::mem_fun(*this, &ObjectsPanel::on_drag_end));
    _tree.add_controller(drag);

    auto const drop = Gtk::DropTarget::create(Glib::Value<Glib::ustring>::value_type(), Gdk::DragAction::MOVE);
    drop->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
    drop->signal_motion().connect(sigc::mem_fun(*this, &ObjectsPanel::on_drag_motion), false); // before
    drop->signal_drop().connect(sigc::mem_fun(*this, &ObjectsPanel::on_drag_drop), false); // before
    _tree.add_controller(drop);

    //Set up the label editing signals
    _text_renderer->signal_edited().connect(sigc::mem_fun(*this, &ObjectsPanel::_handleEdited));

    //Set up the scroller window and pack the page
    // turn off overlay scrollbars - they block access to the 'lock' icon
    _scroller.set_overlay_scrolling(false);
    _scroller.set_child(_tree);
    _scroller.set_policy( Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC );
    _scroller.set_has_frame(true);
    _scroller.set_vexpand();
    Gtk::Requisition sreq;
    Gtk::Requisition sreq_natural;
    _scroller.get_preferred_size(sreq_natural, sreq);
    int minHeight = 70;
    if (sreq.get_height() < minHeight) {
        // Set a min height to see the layers when used with Ubuntu liboverlay-scrollbar
        _scroller.set_size_request(sreq.get_width(), minHeight);
    }

    _page.append(header);
    _page.append(_scroller);
    _popoverbin.setChild(&_page);
    _popoverbin.set_expand();
    append(_popoverbin);

    auto const set_selection_color = [&] {
        selection_color = get_color_with_class(_tree, "theme_selected_bg_color");
    };
    set_selection_color();

    auto enter_layer_label_editing_mode = [this]() {
        layerChanged(getDesktop()->layerManager().currentLayer());
        if (auto watcher = getWatcher(_layer->getRepr())) {
            _tree.set_cursor(watcher->getTreePath(), *_tree.get_column(0), true);
            _is_editing = true;
        }
    };
    auto& add_layer_btn = get_widget<Gtk::Button>(_builder, "insert-layer");
    add_layer_btn.signal_clicked().connect(enter_layer_label_editing_mode);

    _tree_style = _tree.connect_css_changed([=, this] (GtkCssStyleChange *change) {
        set_selection_color();

        if (!root_watcher) return;
        for (auto&& kv : root_watcher->child_watchers) {
            if (kv.second) {
                kv.second->updateRowHighlight();
            }
        }
    });

    // Clear and update entire tree (do not use this in changed/modified signals)
    auto prefs = Inkscape::Preferences::get();
    _watch_object_mode = prefs->createObserver("/dialogs/objects/layers_only", [this]() { setRootWatcher(); });

    update();
}

ObjectsPanel::~ObjectsPanel() = default;

void ObjectsPanel::desktopReplaced()
{
    layer_changed.disconnect();

    if (auto desktop = getDesktop()) {
        layer_changed = desktop->layerManager().connectCurrentLayerChanged(sigc::mem_fun(*this, &ObjectsPanel::layerChanged));
    }
}

void ObjectsPanel::documentReplaced()
{
    setRootWatcher();
}

void ObjectsPanel::setRootWatcher()
{
    root_watcher.reset();
    _idle_connection.disconnect();

    auto const document = getDocument();
    if (!document) return;

    auto const prefs = Inkscape::Preferences::get();
    bool const filtered = prefs->getBool("/dialogs/objects/layers_only", false) || _searchBox.get_text().length();

    // A filtered object watcher behaves differently to an unfiltered one.
    // Filtering disables creating dummy children and instead processes entire trees.
    root_watcher = std::make_unique<ObjectWatcher>(this, document->getRoot(), nullptr, filtered);
    root_watcher->rememberExtendedItems();
    layerChanged(getDesktop()->layerManager().currentLayer());
    _selectionChanged();
}

/**
 * Apply any ongoing filters to the items.
 */
bool ObjectsPanel::showChildInTree(SPItem *item) {
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();

    bool show_child = true;

    // Filter by object type, the layers dialog here.
    if (prefs->getBool("/dialogs/objects/layers_only", false)) {
        auto group = cast<SPGroup>(item);
        if (!group || group->layerMode() != SPGroup::LAYER) {
            show_child = false;
        }
    }

    // Filter by text search, if the search text box has any contents
    auto term = _searchBox.get_text().lowercase();
    if (show_child && term.length()) {
        // A source document allows search for different pieces of metadata
        std::stringstream source;
        source << "#" << item->getId();
        if (auto label = item->label())
            source << " " << label;
        source << " @" << item->getTagName();
        // Might want to add class names here as ".class"

        auto doc = source.str();
        transform(doc.begin(), doc.end(), doc.begin(), ::tolower);
        show_child = doc.find(term) != std::string::npos;
    }

    // Now the terrible bit, searching all the children causing a
    // duplication of work as it must re-scan up the tree multiple times
    // when the tree is very deep.
    for (auto child_obj : item->childList(false)) {
        if (show_child)
            break;
        if (auto child = cast<SPItem>(child_obj)) {
            show_child = showChildInTree(child);
        }
    }

    return show_child;
}

/**
 * This both unpacks the tree, and populates lazy loading
 */
ObjectWatcher *ObjectsPanel::unpackToObject(SPObject *item)
{
    ObjectWatcher *watcher = nullptr;

    for (auto &parent : item->ancestorList(true)) {
        if (parent->getRepr() == root_watcher->getRepr()) {
            watcher = root_watcher.get();
        } else if (watcher &&
                   (watcher = watcher->findChild(parent->getRepr())))
        {
            if (auto const row = watcher->getRow()) {
                cleanDummyChildren(*row);
            }
        }
    }

    return watcher;
}

// Same definition as in 'document.cpp'
#define SP_DOCUMENT_UPDATE_PRIORITY (G_PRIORITY_HIGH_IDLE - 2)

void ObjectsPanel::selectionChanged(Selection *selected /* not used */)
{
    if (!_idle_connection.connected()) {
        auto handler = sigc::mem_fun(*this, &ObjectsPanel::_selectionChanged);
        int priority = SP_DOCUMENT_UPDATE_PRIORITY + 1;
        _idle_connection = Glib::signal_idle().connect(handler, priority);
    }
}

bool ObjectsPanel::_selectionChanged()
{
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    root_watcher->setSelectedBitRecursive(SELECTED_OBJECT, false);
    root_watcher->setSelectedBitRecursive(GROUP_SELECT_CHILD, false);
    bool keep_current_item = false;

    for (auto item : getSelection()->items()) {
        keep_current_item |= (item == current_item);
        if (auto watcher = unpackToObject(item)) {
            // Expand layers themselves, but do not expand groups.
            auto focus_watcher = watcher;

            // Failing to find the child watcher here means the object is filtered out
            // of the current object view and we expand to the closest sublayer instead.
            if (auto child_watcher = watcher->findChild(item->getRepr())) {
                child_watcher->setSelectedBit(SELECTED_OBJECT, true);
                child_watcher->setSelectedBitRecursive(GROUP_SELECT_CHILD, true);
                watcher = child_watcher;
            }

            {
                if (prefs->getBool("/dialogs/objects/expand_to_layer", true)) {
                    _tree.expand_to_path(focus_watcher->getTreePath());
                    if (!_scroll_lock) {
                        _tree.scroll_to_row(watcher->getTreePath(), 0.5);
                    }
                }
            }
        }
    }
    if (!keep_current_item) {
        current_item = nullptr;
    }
    _scroll_lock = false;

    // Returning 'false' disconnects idle signal handler
    return false;
}

/**
 * Happens when the layer selected is changed.
 *
 * @param layer - The layer now selected
 */
void ObjectsPanel::layerChanged(SPObject *layer)
{
    root_watcher->setSelectedBitRecursive(LAYER_FOCUS_CHILD | LAYER_FOCUSED, false);

    if (!layer || !layer->getRepr()) return;

    auto const watcher = getWatcher(layer->getRepr());
    if (watcher && watcher != root_watcher.get()) {
        watcher->setSelectedBitChildren(LAYER_FOCUS_CHILD, true);
        watcher->setSelectedBit(LAYER_FOCUSED, true);
    }

    _layer = layer;
}

/**
 * Special context-aware functions - If nothing is selected
 * or layers-only mode is active, move/delete layers.
 */
void ObjectsPanel::_activateAction(const std::string& layerAction, const std::string& selectionAction)
{
    auto selection = getSelection();
    auto *prefs = Inkscape::Preferences::get();
    if (selection->isEmpty() || prefs->getBool("/dialogs/objects/layers_only", false)) {
        InkscapeWindow* win = InkscapeApplication::instance()->get_active_window();
        win->activate_action(layerAction);
    } else {
        Glib::RefPtr<Gio::Application> app = Gio::Application::get_default();
        app->activate_action(selectionAction);
    }
}

/**
 * Sets visibility of items in the tree
 * @param iter Current item in the tree
 */
bool ObjectsPanel::toggleVisible(Gdk::ModifierType const state, Gtk::TreeModel::Row row)
{
    auto desktop = getDesktop();
    auto selection = getSelection();

    if (SPItem* item = getItem(row)) { 
        if (Controller::has_flag(state, Gdk::ModifierType::SHIFT_MASK)) {
            // Toggle Visible for layers (hide all other layers)
            if (desktop->layerManager().isLayer(item)) {
                desktop->layerManager().toggleLayerSolo(item);
                DocumentUndo::done(getDocument(), _("Hide other layers"), "");
            }
            return true;
        }
        bool visible = !row[_model->_colInvisible];
        if (Controller::has_flag(state, Gdk::ModifierType::CONTROL_MASK) ||
            !selection->includes(item))
        {
            item->setHidden(visible);
        } else {
            for (auto sitem : selection->items()) {
                sitem->setHidden(visible);
            }
        }
        // Use maybeDone so user can flip back and forth without making loads of undo items
        DocumentUndo::maybeDone(getDocument(), "toggle-vis", _("Toggle item visibility"), "");
        return visible;
    }
    return false;
}

// show blend mode popup menu for current item
bool ObjectsPanel::blendModePopup(int const x, int const y, Gtk::TreeModel::Row row)
{
    auto const item = getItem(row);
    if (item == nullptr) {
        return false;
    }

    current_item = nullptr;

    auto blend = SP_CSS_BLEND_NORMAL;
    if (item->style && item->style->mix_blend_mode.set) {
        blend = item->style->mix_blend_mode.value;
    }

    auto opacity = 1.0;
    if (item->style && item->style->opacity.set) {
        opacity = SP_SCALE24_TO_FLOAT(item->style->opacity.value);
    }

    for (auto const &btn : _blend_items) {
        btn.second->property_active().set_value(btn.first == blend);
    }

    _opacity_slider.set_value(opacity * 100);
    current_item = item;

    _item_state_toggler->set_force_visible(true);

    _popoverbin.setPopover(&_object_menu);
    UI::popup_at(_object_menu, _tree, x, y);
    return true;
}

bool ObjectsPanel::colorTagPopup(int const x, int const y, Gtk::TreeModel::Row row)
{
    auto const item = getItem(row);
    if (item == nullptr) {
        return false;
    }
    _colors->set(item->highlight_color());
    auto color_popup = Gtk::make_managed<Gtk::Popover>();
    _color_selector = Gtk::make_managed<ColorNotebook>(_colors);
    _color_selector->set_label(_("Highlight Color"));
    _color_selector->set_margin(4);
    color_popup->set_child(*_color_selector);
    _colors->signal_changed.connect([this]() {
        if (auto item = getItem(_clicked_item_row)) {
            item->setHighlight(_colors->get().value());
            DocumentUndo::maybeDone(getDocument(), "highlight-color", _("Set item highlight color"), INKSCAPE_ICON("dialog-object-properties"));
        }
    });
    _popoverbin.setPopover(&*color_popup);
    UI::popup_at(*color_popup, _tree, x, y);

    return true;
}

/**
 * Sets sensitivity of items in the tree
 * @param iter Current item in the tree
 * @param locked Whether the item should be locked
 */
bool ObjectsPanel::toggleLocked(Gdk::ModifierType const state, Gtk::TreeModel::Row row)
{
    auto desktop = getDesktop();
    auto selection = getSelection();

    if (SPItem* item = getItem(row)) { 
        if (Controller::has_flag(state, Gdk::ModifierType::SHIFT_MASK)) {
            // Toggle lock for layers (lock all other layers)
            if (desktop->layerManager().isLayer(item)) {
                desktop->layerManager().toggleLockOtherLayers(item);
                DocumentUndo::done(getDocument(), _("Lock other layers"), "");
            }
            return true;
        }
        bool locked = !row[_model->_colLocked];
        if (Controller::has_flag(state, Gdk::ModifierType::CONTROL_MASK) ||
            !selection->includes(item))
        {
            item->setLocked(locked);
        } else {
            for (auto sitem : selection->items()) {
                sitem->setLocked(locked);
            }
        }
        // Use maybeDone so user can flip back and forth without making loads of undo items
        DocumentUndo::maybeDone(getDocument(), "toggle-lock", _("Toggle item locking"), "");
        return locked;
    }
    return false;
}

/**
 * Handles keyboard events on the TreeView
 * @return Whether the event should be eaten (om nom nom)
 */
bool ObjectsPanel::on_tree_key_pressed(Gtk::EventControllerKey const &controller,
                                       unsigned keyval, unsigned keycode, Gdk::ModifierType state)
{
    auto desktop = getDesktop();
    if (!desktop)
        return false;

    // This isn't needed in Gtk4, use expand_collapse_cursor_row instead.
    Gtk::TreeModel::Path path;
    Gtk::TreeViewColumn *column;
    _tree.get_cursor(path, column);

    auto const shift = Controller::has_flag(state, Gdk::ModifierType::SHIFT_MASK);
    auto const shortcut = Inkscape::Shortcuts::get_from(controller, keyval, keycode, state);
    switch (shortcut.get_key()) {
        case GDK_KEY_Escape:
            if (desktop->getCanvas()) {
                desktop->getCanvas()->grab_focus();
                return true;
            }
            break;
        case GDK_KEY_Left:
        case GDK_KEY_KP_Left:
            if (path && shift) {
                _tree.collapse_row(path);
                return true;
            }
            break;
        case GDK_KEY_Right:
        case GDK_KEY_KP_Right:
            if (path && shift) {
                _tree.expand_row(path, false);
                return true;
            }
            break;
        case GDK_KEY_space:
            selectCursorItem(Gdk::ModifierType(state));
            return true;
        // Depending on the action to cover this causes it's special
        // text and node handling to block deletion of objects. DIY
        case GDK_KEY_Delete:
        case GDK_KEY_KP_Delete:
        case GDK_KEY_BackSpace:
            _activateAction("win.layer-delete", "delete-selection");
            // NOTE: We could select a sibling object here to make deleting many objects easier.
            return true;
        case GDK_KEY_Page_Up:
        case GDK_KEY_KP_Page_Up:
            if (shift) {
                _activateAction("win.layer-top", "selection-top");
                return true;
            }
            break;
        case GDK_KEY_Page_Down:
        case GDK_KEY_KP_Page_Down:
            if (shift) {
                _activateAction("win.layer-bottom", "selection-bottom");
                return true;
            }
            break;
        case GDK_KEY_Up:
        case GDK_KEY_KP_Up:
            if (shift) {
                _activateAction("win.layer-raise", "selection-stack-up");
                return true;
            }
            break;
        case GDK_KEY_Down:
        case GDK_KEY_KP_Down:
            if (shift) {
                _activateAction("win.layer-lower", "selection-stack-down");
                return true;
            }
        case GDK_KEY_Return:
            if (auto item = getSelection()->singleItem()) {
                if (auto watcher = getWatcher(item->getRepr())) {
                    auto item_path = watcher->getTreePath();
                    _tree.set_cursor(item_path, *_tree.get_column(0), true /* start_editing */);
                    _is_editing = true;
                    return true;
                }
            }
    }

    return false;
}

bool ObjectsPanel::on_window_key(Gtk::EventControllerKey const &controller,
                                 unsigned keyval, unsigned keycode,
                                 Gdk::ModifierType state, EventType event_type)
{
    auto desktop = getDesktop();
    if (!desktop)
        return false;

    auto const shortcut = Inkscape::Shortcuts::get_from(controller, keyval, keycode, state);
    switch (shortcut.get_key()) {
        case GDK_KEY_Alt_L:
        case GDK_KEY_Alt_R:
            _handleTransparentHover(event_type == EventType::pressed);
            return false;
    }

    return false;
}

/**
 * Handles mouse movements
 */

// Set a status bar text when entering the widget
void ObjectsPanel::on_motion_enter(double /*ex*/, double /*ey*/)
{
    _msg_id = getDesktop()->messageStack()->push(Inkscape::NORMAL_MESSAGE,
         _("<b>Hold ALT</b> while hovering over item to highlight, "
           "<b>hold SHIFT</b> and click to hide/lock all."));
}
// watch mouse leave too to clear any state.
void ObjectsPanel::on_motion_leave()
{
    getDesktop()->messageStack()->cancel(_msg_id);
    on_motion_motion(nullptr, 0, 0);
}

void ObjectsPanel::on_motion_motion(Gtk::EventControllerMotion const *controller,
                                    double ex, double ey)
{
    if (_is_editing) return;

    // Unhover any existing hovered row.
    if (_hovered_row_ref) {
        if (auto row = *_store->get_iter(_hovered_row_ref.get_path())) {
            row[_model->_colHover] = false;
            row[_model->_colHoverColor] = false;
            // selection etc. might change _colBgColor. Erase hover
            // highlight only if it hasn't changed
            if (row[_model->_colBgColor] == _hovered_row_color) {
                row[_model->_colBgColor] = _hovered_row_old_color;
            }
            else { // update row's slection color if it has changed
                _hovered_row_old_color = row[_model->_colBgColor];
            }
        }
    }

    // Allow this function to be called by LEAVE motion
    if (controller == nullptr) {
        _hovered_row_ref = Gtk::TreeModel::RowReference();
        _handleTransparentHover(false);
        return;
    }


    Gtk::TreeModel::Path path;
    Gtk::TreeViewColumn* col = nullptr;
    int cell_x, cell_y;
    if (_tree.get_path_at_pos(ex, ey, path, col, cell_x, cell_y)) {
        // Only allow drag and drop from the name column, not any others
        if (col == _name_column) {
            _drag_column = nullptr;
        }

        // Only allow drag and drop when not filtering. Otherwise bad things happen
        // _tree.set_reorderable(col == _name_column);

        if (auto row = *_store->get_iter(path)) {
            row[_model->_colHover] = true;
            _hovered_row_ref = Gtk::TreeModel::RowReference(_store, path);
            // update color for hovered row
            const Gdk::RGBA color = row[_model->_colBgColor]; // current color
            _hovered_row_old_color = color; // store old color
            _hovered_row_color = change_alpha(color, color.get_alpha() + HOVER_ALPHA);
            row[_model->_colBgColor] = _hovered_row_color;

            if (col == _color_tag_column) {
                row[_model->_colHoverColor] = true;
            }

            // Dragging over the eye or locks will set them all
            auto item = getItem(row);
            if (item && _drag_column && col == _drag_column) {
                if (col == _eye_column) {
                    // Defer visibility to th idle thread (it's expensive)
                    Glib::signal_idle().connect_once([this, item]() {
                        item->setHidden(_drag_flip);
                        DocumentUndo::maybeDone(getDocument(), "toggle-vis", _("Toggle item visibility"), "");
                    }, Glib::PRIORITY_DEFAULT_IDLE);
                } else if (col == _lock_column) {
                    item->setLocked(_drag_flip);
                    DocumentUndo::maybeDone(getDocument(), "toggle-lock", _("Toggle item locking"), "");
                }
            }
        }
    }

    auto const state = controller->get_current_event_state();
    _handleTransparentHover(Controller::has_flag(state, Gdk::ModifierType::ALT_MASK));
}

void ObjectsPanel::_handleTransparentHover(bool enabled)
{
    auto &trg = getDesktop()->getTranslucencyGroup();
    SPItem *item = nullptr;
    if (enabled && _hovered_row_ref) {
        if (auto row = *_store->get_iter(_hovered_row_ref.get_path())) {
            item = getItem(row);
        }
    }
    // Save any solid item from other inkscape features
    if (enabled && !_translucency_enabled) {
        _old_solid_item = trg.getSolidItem();
    } else if (!enabled && _translucency_enabled) {
        item = _old_solid_item;
    }
    _translucency_enabled = enabled;

    // Ask the canvas to only show one item fully opaque
    trg.setSolidItem(item);
}

[[nodiscard]] static auto get_cell_area(Gtk::TreeView const &tree_view,
                                        Gtk::TreeModel::Path const &path, Gtk::TreeViewColumn &column)
{
    auto area = Gdk::Rectangle{};
    tree_view.get_cell_area(path, column, area);
    return area;
}

[[nodiscard]] static auto get_cell_center(Gtk::TreeView const &tree_view,
                                          Gtk::TreeModel::Path const &path, Gtk::TreeViewColumn &column)
{
    auto const area = get_cell_area(tree_view, path, column);
    return std::pair{std::lround(area.get_x() + area.get_width () / 2.0),
                     std::lround(area.get_y() + area.get_height() / 2.0)};
}

/**
 * Handles mouse button click events
 * @return whether to eat the event (om nom nom)
 */
Gtk::EventSequenceState ObjectsPanel::on_click(Gtk::GestureClick const &gesture,
                                               int const n_press, double const ex, double const ey,
                                               EventType const event_type)
{
    auto selection = getSelection();
    if (!selection) {
        return Gtk::EventSequenceState::NONE;
    }

    if (event_type == EventType::released) {
        _drag_column = nullptr;
    }

    Gtk::TreeModel::Path path;
    Gtk::TreeViewColumn* col = nullptr;
    int x, y;
    if (!_tree.get_path_at_pos(ex, ey, path, col, x, y)) {
        // Over background (below list or between list items).
        return Gtk::EventSequenceState::NONE;
    }

    // Setting the cursor on the clicked row so that later calls to selectCursorItem knows which
    // item to select (via get_cursor).
    // This used to be done in on_motion_motion but was moved here because of issue #5156.
    _tree.set_cursor(path);

    if (auto row = *_store->get_iter(path)) {
        if (event_type == EventType::pressed) {
            auto const state = gesture.get_current_event_state();
            // Remember column for dragging feature
            _drag_column = col;
            if (col == _eye_column) {
                _drag_flip = toggleVisible(state, row);
            } else if (col == _lock_column) {
                _drag_flip = toggleLocked(state, row);
            } else if (col == _blend_mode_column) {
                auto const [cx, cy] = get_cell_center(_tree, path, *_blend_mode_column);
                return blendModePopup(cx, cy, row) ? Gtk::EventSequenceState::CLAIMED
                                                   : Gtk::EventSequenceState::NONE;
            } else if (col == _color_tag_column) {
                _clicked_item_row = *_store->get_iter(path);
                auto const [cx, cy] = get_cell_center(_tree, path, *_color_tag_column);
                return colorTagPopup(cx, cy, row) ? Gtk::EventSequenceState::CLAIMED
                                                   : Gtk::EventSequenceState::NONE;
            }
        }
    }

    // Block D&D via controllers if over icons.
    if (col != _name_column) {
        return Gtk::EventSequenceState::CLAIMED;
    }

    // Gtk lacks the ability to detect if the user is clicking on the
    // expander icon. So we must detect it using the cell_area check.
    auto const is_expander = x < get_cell_area(_tree, path, *_name_column).get_x();
    if (is_expander) {
        return Gtk::EventSequenceState::NONE; // Or else expander won't work.
    }

    // Rename row item.
    if (n_press == 2) {
        _tree.set_cursor(path, *col, true); // true -> Start editing.
        _is_editing = true;
        return Gtk::EventSequenceState::CLAIMED;
    }

    _is_editing &= event_type == EventType::released;

    auto row = *_store->get_iter(path);
    if (!row) {
        // Already handled above by get_path_at_pos()...
        return Gtk::EventSequenceState::NONE;
    }

    SPItem *item = getItem(row);
    assert(item);

    auto layer = Inkscape::LayerManager::asLayer(item);
    auto const state = gesture.get_current_event_state();
    // returns true if layer has to be set as active but not selected
    auto const should_set_current_layer = [&] {
        if (!layer) {
            return false;
        }

        // Modifier keys force selection mode.
        if (Controller::has_flag(state, Gdk::ModifierType::SHIFT_MASK |
                                        Gdk::ModifierType::CONTROL_MASK)) {
            return false;
        }

        return _layer != layer || selection->includes(layer);
    };

    // Load the right click menu?
    auto const button = gesture.get_current_button();
    auto const context_menu = event_type == EventType::pressed && button == 3;

    // Select items on button release to not confuse drag (unless it's a right-click which selects
    // item for use by context menu).
    if (!_is_editing && (event_type == EventType::released || context_menu)) {
        if (context_menu) {
            // If right-clicking on a layer, make it current for context menu actions to work correctly.
            if (layer && !selection->includes(layer)) {
                getDesktop()->layerManager().setCurrentLayer(item, true);
            }

            // If the item under cursor is not selected, we select it before opening the
            // contextmenu. Otherwise, if the item hasn't been selected with left-click
            // beforehand, ContextMenu's constructor may select the item and cause the list
            // to scroll to it. Also, if the item is the parent group of a selected object,
            // it won't get selected by ContextMenu's constructor.
            // See https://gitlab.com/inkscape/inkscape/-/issues/5243
            if (!selection->includes(item)) {
                selectCursorItem(state);
            }

            // true == hide menu item for opening this dialog!
            std::vector<SPItem *> items = {item};
            auto menu = Gtk::make_managed<ContextMenu>(getDesktop(), item, items, true);
            // popup context menu pointing to the clicked tree row:
            _popoverbin.setPopover(menu);
            UI::popup_at(*menu, _tree, ex, ey);
        } else if (should_set_current_layer()) {
            getDesktop()->layerManager().setCurrentLayer(item, true);
            _initial_path = path;
        } else {
            selectCursorItem(state);
        }

        return Gtk::EventSequenceState::CLAIMED;
    } else {
        // Remember the item for we are about to drag it!
        current_item = item;
    }

    return Gtk::EventSequenceState::NONE;
}

/**
 * Handle a successful item label edit
 * @param path Tree path of the item currently being edited
 * @param new_text New label text
 */
void ObjectsPanel::_handleEdited(const Glib::ustring& path, const Glib::ustring& new_text)
{
    _is_editing = false;
    if (auto row = *_store->get_iter(path)) {
        if (auto item = getItem(row)) {
            if (!new_text.empty() && (!item->label() || new_text != item->label())) {
                auto obj = cast<SPGroup>(item);
                if (obj && obj->layerMode() == SPGroup::LAYER && !obj->isHighlightSet()) {
                    obj->setHighlight(obj->highlight_color());
                }
                item->setLabel(new_text.c_str());
                DocumentUndo::done(getDocument(), _("Rename object"), "");
            }
        }
    }
}

/**
 * Take over the select row functionality from the TreeView, this is because
 * we have two selections (layer and object selection) and require a custom
 * method of rendering the result to the treeview.
 */
bool ObjectsPanel::select_row( Glib::RefPtr<Gtk::TreeModel> const & /*model*/, Gtk::TreeModel::Path const &path, bool /*sel*/ )
{
    return true;
}

/**
 * Get the XML node which is associated with a row. Can be NULL for dummy children.
 */
Node *ObjectsPanel::getRepr(Gtk::TreeModel::ConstRow const &row) const
{
    return row[_model->_colNode];
}

/**
 * Get the item which is associated with a row. If getRepr(row) is not NULL,
 * then this call is expected to also not be NULL.
 */
SPItem *ObjectsPanel::getItem(Gtk::TreeModel::ConstRow const &row) const
{
    auto const this_const = const_cast<ObjectsPanel *>(this);
    return cast<SPItem>(this_const->getObject(getRepr(row)));
}

/**
 * Return true if this row has dummy children.
 */
bool ObjectsPanel::hasDummyChildren(Gtk::TreeModel::ConstRow const &row) const
{
    for (auto &c : row.children()) {
        if (isDummy(c)) {
            return true;
        }
    }
    return false;
}

/**
 * If the given row has dummy children, remove them.
 * @pre Eiter all, or no children are dummies
 * @post If the function returns true, the row has no children
 * @return False if there are children and they are not dummies
 */
bool ObjectsPanel::removeDummyChildren(Gtk::TreeModel::Row row)
{
    auto &children = row.children();
    if (!children.empty()) {
        auto const iter = row.get_iter();
        Gtk::TreeStore::iterator child = children.begin();
        if (!isDummy(*child)) {
            assert(!hasDummyChildren(row));
            return false;
        }

        do {
            assert(child->parent() == iter);
            assert(isDummy(*child));
            child = _store->erase(child);
        } while (child && child->parent() == iter);
    }
    return true;
}

bool ObjectsPanel::cleanDummyChildren(Gtk::TreeModel::Row row)
{
    if (removeDummyChildren(row)) {
        assert(row);
        if (auto watcher = getWatcher(getRepr(row))) {
            watcher->addChildren(getItem(row));
            return true;
        }
    }
    return false;
}

/**
 * Signal handler for "drag-motion"
 *
 * Refuses drops onto self.
 */
Gdk::DragAction ObjectsPanel::on_drag_motion(double x, double y)
{
    auto selection = getSelection();
    auto document = getDocument();
    if (!selection || !document) {
        return Gdk::DragAction{}; // not supported
    }

    Gtk::TreeModel::Path path;
    Gtk::TreeView::DropPosition pos;
    _tree.get_dest_row_at_pos(x, y, path, pos);
    if (path) {
        auto item = getItem(*_store->get_iter(path));
        if (!item) {
            std::cerr << "ObjectsPanel::on_drag_motion: path doesn't correspond to an item!" << std::endl;
            return Gdk::DragAction{}; // not supported
        }

        // Don't drop on self. This causes disturbing flickering so maybe remove this and
        // rely on code in "on_drag_drop" to reject dropping on self.
        if (selection->includes(item)) {
            return Gdk::DragAction{}; // not supported
        }

        // Don't drop on descendent.
        if (selection->includesAncestor(item)) {
            return Gdk::DragAction{}; // not supported
        }

        // Only allow dragging rows from name column.
        int cell_x, cell_y;
        Gtk::TreeViewColumn* col = nullptr;
        _tree.get_path_at_pos(x, y, path, col, cell_x, cell_y);
        if (col != _name_column) {
            return Gdk::DragAction{}; // not supported
        }

        // Setting CSS class here is useless as we can't set CSS on CellRenderer.
    } else {
        if (_tree.is_blank_at_pos(x, y)) {
            // Dropping on background.
            path = --_store->children().end();
            auto item = getItem(*_store->get_iter(path));
            if (selection->includes(item)) {
                // Don't drop after self.
                return Gdk::DragAction{}; // not supported
            }
        } else {
            std::cerr << "ObjectsPanel::on_drag_motion: invalid drop area!" << std::endl;
        }
    }

    // need to cater scenarios where we got no selection/empty bottom space
    return Gdk::DragAction::MOVE;
}

/**
 * Signal handler for "drag-drop".
 *
 * Do the actual work of drag-and-drop.
 */
bool ObjectsPanel::on_drag_drop(Glib::ValueBase const &/*value*/, double x, double y)
{
    Gtk::TreeModel::Path path;
    Gtk::TreeView::DropPosition pos;
    _tree.get_dest_row_at_pos(x, y, path, pos);

    if (!path) {
        if (_tree.is_blank_at_pos(x, y)){
            // We are in background/bottom empty space. Hence, need to drop the item at end.
            // We will move to the last node/path and set drop position accordingly.
            path = --_store->children().end();
            pos = Gtk::TreeView::DropPosition::AFTER;
        } else {
            std::cerr << "ObjectsPanel::on_drag_drop: invalid drop area!" << std::endl;
            return true;
        }
    }
    
    auto drop_repr = getRepr(*_store->get_iter(path));
    bool const drop_into = pos != Gtk::TreeView::DropPosition::BEFORE && //
                           pos != Gtk::TreeView::DropPosition::AFTER;

    auto selection = getSelection();
    auto document = getDocument();
    if (selection && document) {
        auto item = document->getObjectByRepr(drop_repr);

        // We always try to drop the item, even if we end up dropping it after the non-group item.
        if (drop_into && is<SPGroup>(item)) {
            selection->toLayer(item);
        } else {
            // Note: Object dialog order opposite of XML order.
            Node *after = (pos == Gtk::TreeView::DropPosition::BEFORE ||
                           pos == Gtk::TreeView::DropPosition::INTO_OR_BEFORE)
                ? drop_repr : drop_repr->prev();
            selection->toLayer(item->parent, after);
        }
        DocumentUndo::done(document, _("Move items"), INKSCAPE_ICON("selection-move-to-layer"));
    }

    drag_end_impl();
    return true;
}

Glib::RefPtr<Gdk::ContentProvider> ObjectsPanel::on_prepare(Gtk::DragSource &controller, double x, double y)
{
    Gtk::TreeModel::Path path;
    Gtk::TreeView::DropPosition pos;
    _tree.get_dest_row_at_pos(x, y, path, pos);

    if (path) {
        // Set icon (or else icon is determined by provider value).
        auto surface = _tree.create_row_drag_icon(path);
        controller.set_icon(surface, x, 12);
    }

    // We must have some kind of value which matches DropTarget type! Use a string for now.
    Glib::Value<Glib::ustring> value;
    value.init(G_TYPE_STRING);
    value.set("ObjectsPanelDrag");
    auto provider = Gdk::ContentProvider::create(value);
    return provider;
}

void ObjectsPanel::on_drag_begin(Glib::RefPtr<Gdk::Drag> const &/*drag*/)
{
    _scroll_lock = true;

    auto selection = _tree.get_selection();
    selection->set_mode(Gtk::SelectionMode::MULTIPLE);
    selection->unselect_all();

    auto obj_selection = getSelection();
    if (!obj_selection)
        return;

    if (current_item && !obj_selection->includes(current_item)) {
        // This means the item the user started to drag is not one that is selected
        // So we'll deselect everything and start dragging this item instead.
        auto watcher = getWatcher(current_item->getRepr());
        if (watcher) {
            auto path = watcher->getTreePath();
            selection->select(path);
            obj_selection->set(current_item);
        }
    } else {
        // Drag all the items currently selected (multi-row)
        for (auto item : obj_selection->items()) {
            auto watcher = getWatcher(item->getRepr());
            if (watcher) {
                auto path = watcher->getTreePath();
                selection->select(path);
            }
        }
    }
    // auto content = controller.get_content();  Can't modify content! Can't modify controller!
}

void ObjectsPanel::drag_end_impl()
{
    auto selection = _tree.get_selection();
    selection->unselect_all();
    selection->set_mode(Gtk::SelectionMode::NONE);
    current_item = nullptr;
}

void ObjectsPanel::on_drag_end(Glib::RefPtr<Gdk::Drag> const &/*drag*/, bool /*delete_data*/)
{
    drag_end_impl();
}

void ObjectsPanel::selectRange(Gtk::TreeModel::Path start, Gtk::TreeModel::Path end)
{
    auto &layers = getDesktop()->layerManager();

    if (!start || !end) {
        return;
    }

    if (gtk_tree_path_compare(start.gobj(), end.gobj()) > 0) {
        std::swap(start, end);
    }

    auto selection = getSelection();

    if (!_start_new_range) {
        // Deselect previous selection of this range first and then proceed.
        for (auto const &obj : _prev_range) {
            if (obj) {
                selection->remove(obj.get());
            }
        }
    }

    _prev_range.clear();

    // Select everything between the initial selection and currently selected item.
    _store->foreach ([&](Gtk::TreeModel::Path const &p, Gtk::TreeModel::const_iterator const &it) {
        if ((gtk_tree_path_compare(start.gobj(), p.gobj()) <= 0) &&
            (gtk_tree_path_compare(end.gobj(), p.gobj()) >= 0)) {
            auto obj = getItem(*it);
            if (obj) {
                if (!layers.isLayer(obj)) {
                    _prev_range.emplace_back(obj);
                    selection->add(obj, false);
                }
            }
        }
        return false;
    });

    _start_new_range = false;
}

/**
 * Select the object currently under the list-cursor (keyboard or mouse)
 */
bool ObjectsPanel::selectCursorItem(Gdk::ModifierType const state)
{
    auto &layers = getDesktop()->layerManager();
    auto selection = getSelection();
    if (!selection)
        return false;

    Gtk::TreeModel::Path path;
    Gtk::TreeViewColumn *column;
    _tree.get_cursor(path, column);
    if (!path || !column)
        return false;

    auto row = *_store->get_iter(path);
    if (!row)
        return false;

    if (column == _eye_column) {
        toggleVisible(state, row);
    } else if (column == _lock_column) {
        toggleLocked(state, row);
    } else if (column == _name_column) {
        auto item = getItem(row);
        auto group = cast<SPGroup>(item);
        _scroll_lock = true; // Clicking to select shouldn't scroll the treeview.

        if (Controller::has_flag(state, Gdk::ModifierType::SHIFT_MASK) && !selection->isEmpty()) {
            // Shift + Click or Shift + Ctrl + Click
            // TODO: Fix layers expand unexpectedly on range selection.
            selectRange(_initial_path, path);
        } else if (Controller::has_flag(state, Gdk::ModifierType::CONTROL_MASK)) {
            if (selection->includes(item)) {
                selection->remove(item);
            } else {
                selection->add(item, false);
                _initial_path = path;
                _start_new_range = true;
            }
        } else if (group && selection->includes(item) && !group->isLayer()) {
            // Clicking off a group (second click) will enter the group
            layers.setCurrentLayer(item, true);
        } else {
            // Just Click
            if (layers.currentLayer() == item || group) {
                layers.setCurrentLayer(item->parent);
            }

            selection->set(item);
            _initial_path = path;
            _start_new_range = true;
        }

        return true;
    }
    return false;
}

/**
 * User pressed return in search box, process search query.
 */
void ObjectsPanel::_searchActivated()
{
    // The root watcher and watcher tree handles the search operations
    setRootWatcher();
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
