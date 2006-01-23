/** \file
 * Per-desktop selection container
 *
 * Authors:
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   MenTaLguY <mental@rydia.net>
 *   bulia byak <buliabyak@users.sf.net>
 *
 * Copyright (C) 2004-2005 MenTaLguY
 * Copyright (C) 1999-2002 Lauris Kaplinski
 * Copyright (C) 2001-2002 Ximian, Inc.
 *
 * Released under GNU GPL, read the file 'COPYING' for more information
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include "macros.h"
#include "inkscape-private.h"
#include "desktop.h"
#include "desktop-handles.h"
#include "document.h"
#include "selection.h"
#include "xml/repr.h"

#include "sp-shape.h"


#define SP_SELECTION_UPDATE_PRIORITY (G_PRIORITY_HIGH_IDLE + 1)

namespace Inkscape {

Selection::Selection(SPDesktop *desktop) :
    _objs(NULL),
    _reprs(NULL),
    _items(NULL),
    _desktop(desktop),
    _flags(0),
    _idle(0)
{
    clearOnceInaccessible(&_desktop);
}

Selection::~Selection() {
    _clear();
    _desktop = NULL;
    if (_idle) {
        g_source_remove(_idle);
        _idle = 0;
    }
}

void
Selection::_release(SPObject *obj, Selection *selection)
{
    selection->remove(obj);
}

/* Handler for selected objects "modified" signal */

void
Selection::_schedule_modified(SPObject *obj, guint flags, Selection *selection)
{
    if (!selection->_idle) {
        /* Request handling to be run in _idle loop */
        selection->_idle = g_idle_add_full(SP_SELECTION_UPDATE_PRIORITY, GSourceFunc(&Selection::_emit_modified), selection, NULL);
    }

    /* Collect all flags */
    selection->_flags |= flags;
}

gboolean
Selection::_emit_modified(Selection *selection)
{
    /* force new handler to be created if requested before we return */
    selection->_idle = 0;
    guint flags = selection->_flags;
    selection->_flags = 0;

    selection->_emitModified(flags);

    /* drop this handler */
    return FALSE;
}

void Selection::_emitModified(guint flags) {
    inkscape_selection_modified(this, flags);
    _modified_signal.emit(this, flags);
}

void Selection::_emitChanged() {
    inkscape_selection_changed(this);
    _changed_signal.emit(this);
}

void Selection::_invalidateCachedLists() {
    g_slist_free(_items);
    _items = NULL;

    g_slist_free(_reprs);
    _reprs = NULL;
}

void Selection::_clear() {
    _invalidateCachedLists();
    while (_objs) {
        SPObject *obj=reinterpret_cast<SPObject *>(_objs->data);
        sp_signal_disconnect_by_data(obj, this);
        _objs = g_slist_remove(_objs, obj);
    }
}

bool Selection::includes(SPObject *obj) const {
    if (obj == NULL)
        return FALSE;

    g_return_val_if_fail(SP_IS_OBJECT(obj), FALSE);

    return ( g_slist_find(_objs, obj) != NULL );
}

void Selection::add(SPObject *obj) {
    g_return_if_fail(obj != NULL);
    g_return_if_fail(SP_IS_OBJECT(obj));

    if (includes(obj)) {
        return;
    }

    _invalidateCachedLists();
    _add(obj);
    _emitChanged();
}

void Selection::_add(SPObject *obj) {
    // unselect any of the item's ancestors and descendants which may be selected
    // (to prevent double-selection)
    _removeObjectDescendants(obj);
    _removeObjectAncestors(obj);

    _objs = g_slist_prepend(_objs, obj);
    g_signal_connect(G_OBJECT(obj), "release",
                     G_CALLBACK(&Selection::_release), this);
    g_signal_connect(G_OBJECT(obj), "modified",
                     G_CALLBACK(&Selection::_schedule_modified), this);

    /*
    if (!SP_IS_SHAPE(obj)) {
        printf("This is not a shape\n");
    }
    */
}

void Selection::set(SPObject *object) {
    _clear();
    add(object);
}

void Selection::toggle(SPObject *obj) {
    if (includes (obj)) {
        remove (obj);
    } else {
        add(obj);
    }
}

void Selection::remove(SPObject *obj) {
    g_return_if_fail(obj != NULL);
    g_return_if_fail(SP_IS_OBJECT(obj));
    g_return_if_fail(includes(obj));

    _invalidateCachedLists();
    _remove(obj);
    _emitChanged();
}

void Selection::_remove(SPObject *obj) {
    sp_signal_disconnect_by_data(obj, this);
    _objs = g_slist_remove(_objs, obj);
}

void Selection::setList(GSList const *list) {
    _clear();

    if ( list != NULL ) {
        for ( GSList const *iter = list ; iter != NULL ; iter = iter->next ) {
            _add(reinterpret_cast<SPObject *>(iter->data));
        }
    }

    _emitChanged();
}

void Selection::addList(GSList const *list) {

    if (list == NULL)
        return;

    _invalidateCachedLists();

    for ( GSList const *iter = list ; iter != NULL ; iter = iter->next ) {
        SPObject *obj = reinterpret_cast<SPObject *>(iter->data);
        if (includes(obj)) {
            continue;
        }
        _add (obj);
    }

    _emitChanged();
}

void Selection::setReprList(GSList const *list) {
    _clear();

    for ( GSList const *iter = list ; iter != NULL ; iter = iter->next ) {
        SPObject *obj=_objectForXMLNode(reinterpret_cast<Inkscape::XML::Node *>(iter->data));
        if (obj) {
            _add(obj);
        }
    }

    _emitChanged();
}

void Selection::clear() {
    _clear();
    _emitChanged();
}

GSList const *Selection::list() {
    return _objs;
}

GSList const *Selection::itemList() {
    if (_items) {
        return _items;
    }

    for ( GSList const *iter=_objs ; iter != NULL ; iter = iter->next ) {
        SPObject *obj=reinterpret_cast<SPObject *>(iter->data);
        if (SP_IS_ITEM(obj)) {
            _items = g_slist_prepend(_items, SP_ITEM(obj));
        }
    }
    _items = g_slist_reverse(_items);

    return _items;
}

GSList const *Selection::reprList() {
    if (_reprs) { return _reprs; }

    for ( GSList const *iter=itemList() ; iter != NULL ; iter = iter->next ) {
        SPObject *obj=reinterpret_cast<SPObject *>(iter->data);
        _reprs = g_slist_prepend(_reprs, SP_OBJECT_REPR(obj));
    }
    _reprs = g_slist_reverse(_reprs);

    return _reprs;
}

SPObject *Selection::single() {
    if ( _objs != NULL && _objs->next == NULL ) {
        return reinterpret_cast<SPObject *>(_objs->data);
    } else {
        return NULL;
    }
}

SPItem *Selection::singleItem() {
    GSList const *items=itemList();
    if ( items != NULL && items->next == NULL ) {
        return reinterpret_cast<SPItem *>(items->data);
    } else {
        return NULL;
    }
}

Inkscape::XML::Node *Selection::singleRepr() {
    SPObject *obj=single();
    return obj ? SP_OBJECT_REPR(obj) : NULL;
}

NRRect *Selection::bounds(NRRect *bbox) const
{
    g_return_val_if_fail (bbox != NULL, NULL);
    NR::Rect const b = bounds();
    bbox->x0 = b.min()[NR::X];
    bbox->y0 = b.min()[NR::Y];
    bbox->x1 = b.max()[NR::X];
    bbox->y1 = b.max()[NR::Y];
    return bbox;
}

NR::Rect Selection::bounds() const
{
    GSList const *items = const_cast<Selection *>(this)->itemList();
    if (!items) {
        return NR::Rect(NR::Point(0, 0), NR::Point(0, 0));
    }

    GSList const *i = items;
    NR::Rect bbox = sp_item_bbox_desktop(SP_ITEM(i->data));

    while (i != NULL) {
        bbox = NR::Rect::union_bounds(bbox, sp_item_bbox_desktop(SP_ITEM(i->data)));
        i = i->next;
    }

    return bbox;
}

NRRect *Selection::boundsInDocument(NRRect *bbox) const {
    g_return_val_if_fail (bbox != NULL, NULL);

    GSList const *items=const_cast<Selection *>(this)->itemList();
    if (!items) {
        bbox->x0 = bbox->y0 = bbox->x1 = bbox->y1 = 0.0;
        return bbox;
    }

    bbox->x0 = bbox->y0 = 1e18;
    bbox->x1 = bbox->y1 = -1e18;

    for ( GSList const *iter=items ; iter != NULL ; iter = iter->next ) {
        SPItem *item=SP_ITEM(iter->data);
        NR::Matrix const i2doc(sp_item_i2doc_affine(item));
        sp_item_invoke_bbox(item, bbox, i2doc, FALSE);
    }

    return bbox;
}

NR::Rect Selection::boundsInDocument() const {
    NRRect r;
    return NR::Rect(*boundsInDocument(&r));
}

/**
 * Compute the list of points in the selection that are to be considered for snapping.
 */
std::vector<NR::Point> Selection::getSnapPoints() const {
    GSList const *items = const_cast<Selection *>(this)->itemList();
    std::vector<NR::Point> p;
    for (GSList const *iter = items; iter != NULL; iter = iter->next) {
        sp_item_snappoints(SP_ITEM(iter->data), SnapPointsIter(p));
    }

    return p;
}

std::vector<NR::Point> Selection::getSnapPointsConvexHull() const {
    GSList const *items = const_cast<Selection *>(this)->itemList();
    std::vector<NR::Point> p;
    for (GSList const *iter = items; iter != NULL; iter = iter->next) {
	sp_item_snappoints(SP_ITEM(iter->data), SnapPointsIter(p));
    }
    
    std::vector<NR::Point>::iterator i; 
    NR::ConvexHull cvh(*(p.begin()));
    for (i = p.begin(); i != p.end(); i++) {
	// these are the points we get back
	cvh.add(*i); 
    }
    
    NR::Rect rHull = cvh.bounds(); 
    std::vector<NR::Point> pHull(4); 
    pHull[0] = rHull.corner(0); 
    pHull[1] = rHull.corner(1); 
    pHull[2] = rHull.corner(2); 
    pHull[3] = rHull.corner(3); 

    return pHull;
}

std::vector<NR::Point> Selection::getBBoxPoints() const {
    GSList const *items = const_cast<Selection *>(this)->itemList();
    std::vector<NR::Point> p;
    for (GSList const *iter = items; iter != NULL; iter = iter->next) {
        NR::Rect b = sp_item_bbox_desktop(SP_ITEM(iter->data));
        p.push_back(b.min());
        p.push_back(b.max());
    }

    return p;
}

void Selection::_removeObjectDescendants(SPObject *obj) {
    GSList *iter, *next;
    for ( iter = _objs ; iter ; iter = next ) {
        next = iter->next;
        SPObject *sel_obj=reinterpret_cast<SPObject *>(iter->data);
        SPObject *parent=SP_OBJECT_PARENT(sel_obj);
        while (parent) {
            if ( parent == obj ) {
                _remove(sel_obj);
                break;
            }
            parent = SP_OBJECT_PARENT(parent);
        }
    }
}

void Selection::_removeObjectAncestors(SPObject *obj) {
        SPObject *parent=SP_OBJECT_PARENT(obj);
        while (parent) {
            if (includes(parent)) {
                _remove(parent);
            }
            parent = SP_OBJECT_PARENT(parent);
        }
}

SPObject *Selection::_objectForXMLNode(Inkscape::XML::Node *repr) const {
    g_return_val_if_fail(repr != NULL, NULL);
    gchar const *id = repr->attribute("id");
    g_return_val_if_fail(id != NULL, NULL);
    SPObject *object=SP_DT_DOCUMENT(_desktop)->getObjectById(id);
    g_return_val_if_fail(object != NULL, NULL);
    return object;
}

guint Selection::numberOfLayers() {
      GSList const *items = const_cast<Selection *>(this)->itemList();
	GSList *layers = NULL;
	for (GSList const *iter = items; iter != NULL; iter = iter->next) {
		SPObject *layer = desktop()->layerForObject(SP_OBJECT(iter->data));
		if (g_slist_find (layers, layer) == NULL) {
			layers = g_slist_prepend (layers, layer);
		}
	}
	guint ret = g_slist_length (layers);
	g_slist_free (layers);
	return ret;
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
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:encoding=utf-8:textwidth=99 :
