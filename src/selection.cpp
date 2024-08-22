// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Per-desktop selection container
 *
 * Authors:
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   MenTaLguY <mental@rydia.net>
 *   bulia byak <buliabyak@users.sf.net>
 *   Andrius R. <knutux@gmail.com>
 *   Abhishek Sharma
 *   Adrian Boguszewski
 *
 * Copyright (C) 2016 Adrian Boguszewski
 * Copyright (C) 2006 Andrius R.
 * Copyright (C) 2004-2005 MenTaLguY
 * Copyright (C) 1999-2002 Lauris Kaplinski
 * Copyright (C) 2001-2002 Ximian, Inc.
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifdef HAVE_CONFIG_H
#endif

#include <cmath>

#include "inkscape.h"
#include "preferences.h"
#include "desktop.h"
#include "document.h"
#include "document-undo.h"
#include "ui/tools/node-tool.h"
#include "ui/tool/multi-path-manipulator.h"
#include "ui/tool/path-manipulator.h"
#include "ui/tool/control-point-selection.h"
#include "layer-manager.h"
#include "page-manager.h"
#include "object/sp-page.h"
#include "object/sp-path.h"
#include "object/sp-defs.h"
#include "object/sp-shape.h"
#include "xml/repr.h"

#define SP_SELECTION_UPDATE_PRIORITY (G_PRIORITY_HIGH_IDLE + 1)

namespace Inkscape {

Selection::Selection(SPDesktop *desktop):
    ObjectSet(desktop),
    _selection_context(nullptr),
    _flags(0),
    _idle(0),
    anchor_x(0.0),
    anchor_y(0.0)
{
}

Selection::Selection(SPDocument *document):
    ObjectSet(document),
    _selection_context(nullptr),
    _flags(0),
    _idle(0),
    anchor_x(0.0),
    anchor_y(0.0)
{
}

Selection::~Selection() {
    if (_idle) {
        g_source_remove(_idle);
        _idle = 0;
    }

    for (auto &c : _modified_connections) {
        c.second.disconnect();
    }
}

/* Handler for selected objects "modified" signal */

void Selection::_schedule_modified(SPObject */*obj*/, guint flags) {
    if (!this->_idle) {
        /* Request handling to be run in _idle loop */
        this->_idle = g_idle_add_full(SP_SELECTION_UPDATE_PRIORITY, GSourceFunc(&Selection::_emit_modified), this, nullptr);
    }

    /* Collect all flags */
    this->_flags |= flags;
}

gboolean Selection::_emit_modified(Selection *selection)
{
    /* force new handler to be created if requested before we return */
    selection->_idle = 0;
    guint flags = selection->_flags;
    selection->_flags = 0;

    selection->_emitModified(flags);

    /* drop this handler */
    return FALSE;
}

void Selection::_emitModified(guint flags)
{
    for (auto it = _modified_signals.begin(); it != _modified_signals.end(); ) {
        it->emit(this, flags);
        if (it->empty()) it = _modified_signals.erase(it); else ++it;
    }

    if (!_desktop || isEmpty()) {
        return;
    }

    auto &pm = _desktop->getDocument()->getPageManager();

    // If the selected items have been moved to a new page...
    if (auto item = singleItem()) {
        pm.selectPage(item, false);
    } else {
        SPPage *page = pm.getPageFor(firstItem(), true);
        for (auto this_item : this->items()) {
            if (page != pm.getPageFor(this_item, true)) {
                return;
            }
        }
        pm.selectPage(page);
    }
}

void Selection::_emitChanged(bool persist_selection_context/* = false */) {
    ObjectSet::_emitChanged();
    if (persist_selection_context) {
        if (nullptr == _selection_context) {
            _selection_context = _desktop->layerManager().currentLayer();
            sp_object_ref(_selection_context, nullptr);
            _context_release_connection = _selection_context->connectRelease(sigc::mem_fun(*this, &Selection::_releaseContext));
        }
    } else {
        _releaseContext(_selection_context);
    }

    /** Change the layer selection to the item selection
      * TODO: Should it only change if there's a single object?
      */
    if (_document && _desktop) {
        if (auto item = singleItem()) {
            if (_change_layer) {
                auto layer = _desktop->layerManager().layerForObject(item);
                if (layer && layer != _selection_context) {
                    _desktop->layerManager().setCurrentLayer(layer);
                }
            }
            if (_change_page) {
                // This could be more complex if we want to be smarter.
                _document->getPageManager().selectPage(item, false);
            }
        }
        DocumentUndo::resetKey(_document);
    }

    for (auto it = _changed_signals.begin(); it != _changed_signals.end(); ) {
        it->emit(this);
        if (it->empty()) it = _changed_signals.erase(it); else ++it;
    }
}

void Selection::_releaseContext(SPObject *obj)
{
    if (nullptr == _selection_context || _selection_context != obj)
        return;

    _context_release_connection.disconnect();

    sp_object_unref(_selection_context, nullptr);
    _selection_context = nullptr;
}

SPObject *Selection::activeContext() {
    if (nullptr != _selection_context)
        return _selection_context;
    return _desktop->layerManager().currentLayer();
}

std::vector<Inkscape::SnapCandidatePoint> Selection::getSnapPoints(SnapPreferences const *snapprefs) const {
    std::vector<Inkscape::SnapCandidatePoint> p;

    if (snapprefs != nullptr){
        SnapPreferences snapprefs_dummy = *snapprefs; // create a local copy of the snapping prefs
        snapprefs_dummy.setTargetSnappable(Inkscape::SNAPTARGET_ROTATION_CENTER, false); // locally disable snapping to the item center
        auto items = const_cast<Selection *>(this)->items();
        for (auto iter = items.begin(); iter != items.end(); ++iter) {
            SPItem *this_item = *iter;
            this_item->getSnappoints(p, &snapprefs_dummy);

            //Include the transformation origin for snapping
            //For a selection or group only the overall center is considered, not for each item individually
            if (snapprefs->isTargetSnappable(Inkscape::SNAPTARGET_ROTATION_CENTER)) {
                p.emplace_back(this_item->getCenter(), SNAPSOURCE_ROTATION_CENTER);
            }
        }
    }

    return p;
}

sigc::connection Selection::connectChanged(sigc::slot<void (Selection *)> const &slot)
{
    if (_changed_signals.empty()) _changed_signals.emplace_back();
    return _changed_signals.back().connect(slot);
}

sigc::connection Selection::connectChangedFirst(sigc::slot<void (Selection *)> const &slot)
{
    _changed_signals.emplace_front();
    return _changed_signals.front().connect(slot);
}

void Selection::setAnchor(double x, double y, bool set)
{
    double const epsilon = 1e-12;
    if (std::fabs(anchor_x - x) > epsilon || std::fabs(anchor_y - y) > epsilon || set != has_anchor) {
        anchor_x = x;
        anchor_y = y;
        has_anchor = set;
        this->_emitModified(SP_OBJECT_MODIFIED_FLAG);
    }
}

sigc::connection Selection::connectModified(sigc::slot<void (Selection *, unsigned)> const &slot)
{
    if (_modified_signals.empty()) _modified_signals.emplace_back();
    return _modified_signals.back().connect(slot);
}

sigc::connection Selection::connectModifiedFirst(sigc::slot<void (Selection *, unsigned)> const &slot)
{
    _modified_signals.emplace_front();
    return _modified_signals.front().connect(slot);
}

SPObject *Selection::_objectForXMLNode(Inkscape::XML::Node *repr) const {
    g_return_val_if_fail(repr != nullptr, NULL);
    SPObject *object = _desktop->getDocument()->getObjectByRepr(repr);
    assert(object == _desktop->getDocument()->getObjectById(repr->attribute("id")));
    return object;
}

size_t Selection::numberOfLayers() {
    auto items = this->items();
    std::set<SPObject*> layers;
    for (auto iter = items.begin(); iter != items.end(); ++iter) {
        SPObject *layer = _desktop->layerManager().layerForObject(*iter);
        layers.insert(layer);
    }

    return layers.size();
}

size_t Selection::numberOfParents() {
    auto items = this->items();
    std::set<SPObject*> parents;
    for (auto iter = items.begin(); iter != items.end(); ++iter) {
        SPObject *parent = (*iter)->parent;
        parents.insert(parent);
    }
    return parents.size();
}

void Selection::_connectSignals(SPObject *object) {
    _modified_connections[object] = object->connectModified(sigc::mem_fun(*this, &Selection::_schedule_modified));
}

void Selection::_releaseSignals(SPObject *object) {
    _modified_connections[object].disconnect();
    _modified_connections.erase(object);
}

void
Selection::emptyBackup(){
    _selected_ids.clear();
    _seldata.clear();
    params.clear();
}

void
Selection::setBackup ()
{
    SPDesktop *desktop = this->desktop();
    Inkscape::UI::Tools::NodeTool *tool = nullptr;
    if (desktop) {
        if (auto nt = dynamic_cast<Inkscape::UI::Tools::NodeTool*>(desktop->event_context)) {
            tool = nt;
        }
    }
    _selected_ids.clear();
    _seldata.clear();
    params.clear();
    auto items = const_cast<Selection *>(this)->items();
    for (auto iter = items.begin(); iter != items.end(); ++iter) {
        SPItem *item = *iter;
        if(!item->getId()) {
            continue;
        }
        std::string selected_id;
        selected_id += "--id=";
        selected_id += item->getId();
        params.push_back(selected_id);
        _selected_ids.emplace_back(item->getId());
    }
    if(tool){
        Inkscape::UI::ControlPointSelection *cps = tool->_selected_nodes;
        std::list<Inkscape::UI::SelectableControlPoint *> points_list = cps->_points_list;
        for (auto & i : points_list) {
            Inkscape::UI::Node *node = dynamic_cast<Inkscape::UI::Node*>(i);
            if (node) {
                std::string id = node->nodeList().subpathList().pm().item()->getId();

                int sp = 0;
                bool found_sp = false;
                for(Inkscape::UI::SubpathList::iterator i = node->nodeList().subpathList().begin(); i != node->nodeList().subpathList().end(); ++i,++sp){
                    if(&**i == &(node->nodeList())){
                        found_sp = true;
                        break;
                    }
                }
                int nl=0;
                bool found_nl = false;
                for (Inkscape::UI::NodeList::iterator j = node->nodeList().begin(); j != node->nodeList().end(); ++j, ++nl){
                    if(&*j==node){
                        found_nl = true;
                        break;
                    }
                }
                std::ostringstream ss;
                ss<< "--selected-nodes=" << id << ":" << sp << ":" << nl;
                Glib::ustring selected_nodes = ss.str();

                if(found_nl && found_sp) {
                    _seldata.emplace_back(id,std::make_pair(sp,nl));
                    params.push_back(selected_nodes);
                } else {
                    g_warning("Something went wrong while trying to pass selected nodes to extension. Please report a bug.");
                }
            }
        }
    }//end add selected nodes
}

void
Selection::restoreBackup()
{
    SPDesktop *desktop = this->desktop();
    SPDocument *document = SP_ACTIVE_DOCUMENT;
    Inkscape::UI::Tools::NodeTool *tool = nullptr;
    if (desktop) {
        if (auto nt = dynamic_cast<Inkscape::UI::Tools::NodeTool*>(desktop->event_context)) {
            tool = nt;
        }
    }

    // update selection
    std::vector<std::string>::iterator it = _selected_ids.begin();
    std::vector<SPItem*> new_selection;
    for (; it!= _selected_ids.end(); ++it){
        auto item = cast<SPItem>(document->getObjectById(it->c_str()));
        SPDefs * defs = document->getDefs();
        if (item && !defs->isAncestorOf(item)) {
            new_selection.push_back(item);
        }
    }
    clear();
    add(new_selection.begin(), new_selection.end());

    if (tool) {
        Inkscape::UI::ControlPointSelection *cps = tool->_selected_nodes;
        cps->selectAll();
        std::list<Inkscape::UI::SelectableControlPoint *> points_list = cps->_points_list;
        cps->clear();
        Inkscape::UI::Node * node = dynamic_cast<Inkscape::UI::Node*>(*points_list.begin());
        if (node) {
            Inkscape::UI::SubpathList sp = node->nodeList().subpathList();
            for (auto & l : _seldata) {
                gint sp_count = 0;
                for (Inkscape::UI::SubpathList::iterator j = sp.begin(); j != sp.end(); ++j, ++sp_count) {
                    if(sp_count == l.second.first) {
                        gint nt_count = 0;
                        for (Inkscape::UI::NodeList::iterator k = (*j)->begin(); k != (*j)->end(); ++k, ++nt_count) {
                            if(nt_count == l.second.second) {
                                cps->insert(k.ptr());
                                break;
                            }
                        }
                        break;
                    }
                }
            }
        }
        points_list.clear();
    }
}


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
