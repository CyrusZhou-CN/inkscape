/*
 * A class for handling connector endpoint movement and libavoid interaction.
 *
 * Authors:
 *   Peter Moulder <pmoulder@mail.csse.monash.edu.au>
 *   Michael Wybrow <mjwybrow@users.sourceforge.net>
 *
 *    * Copyright (C) 2004-2005 Monash University
 *
 * Released under GNU GPL, read the file 'COPYING' for more information
 */
#include <glib/gmem.h>

#include "attributes.h"
#include "sp-conn-end-pair.h"
#include "sp-conn-end.h"
#include "sp-object.h"
#include "uri.h"
#include "display/curve.h"
#include "libnr/nr-matrix-div.h"
#include "libnr/nr-matrix-fns.h"
#include "libnr/nr-rect.h"
#include "xml/repr.h"
#include "sp-path.h"
#include "libavoid/vertices.h"
#include "libavoid/connector.h"


SPConnEndPair::SPConnEndPair(SPPath *const owner)
    : _invalid_path_connection()
    , _path(owner)
    , _connRef(NULL)
    , _connType(SP_CONNECTOR_NOAVOID)
{
    for (unsigned handle_ix = 0; handle_ix <= 1; ++handle_ix) {
        this->_connEnd[handle_ix] = new SPConnEnd(SP_OBJECT(owner));
        this->_connEnd[handle_ix]->_changed_connection
            = this->_connEnd[handle_ix]->ref.changedSignal()
            .connect(sigc::bind(sigc::ptr_fun(sp_conn_end_href_changed),
                                this->_connEnd[handle_ix], owner, handle_ix));
    }
}

SPConnEndPair::~SPConnEndPair()
{
    for (unsigned handle_ix = 0; handle_ix < 2; ++handle_ix) {
        delete this->_connEnd[handle_ix];
        this->_connEnd[handle_ix] = NULL;
    }
    if (_connRef) {
        _connRef->removeFromGraph();
        delete _connRef;
        _connRef = NULL;
    }
    
    _invalid_path_connection.disconnect();
}

void
SPConnEndPair::release()
{
    for (unsigned handle_ix = 0; handle_ix < 2; ++handle_ix) {
        this->_connEnd[handle_ix]->_changed_connection.disconnect();
        this->_connEnd[handle_ix]->_delete_connection.disconnect();
        this->_connEnd[handle_ix]->_transformed_connection.disconnect();
        g_free(this->_connEnd[handle_ix]->href);
        this->_connEnd[handle_ix]->href = NULL;
        this->_connEnd[handle_ix]->ref.detach();
    }
}

void
sp_conn_end_pair_build(SPObject *object)
{
    sp_object_read_attr(object, "inkscape:connector-type");
    sp_object_read_attr(object, "inkscape:connection-start");
    sp_object_read_attr(object, "inkscape:connection-end");
}

void
SPConnEndPair::setAttr(unsigned const key, gchar const *const value)
{
    if (key == SP_ATTR_CONNECTOR_TYPE) {
        if (value && (strcmp(value, "polyline") == 0)) {
            _connType = SP_CONNECTOR_POLYLINE;
            
            GQuark itemID = g_quark_from_string(SP_OBJECT(_path)->id);
            _connRef = new Avoid::ConnRef(itemID);
            _invalid_path_connection = connectInvalidPath(
                    sigc::ptr_fun(&sp_conn_adjust_invalid_path));
        }
        else {
            _connType = SP_CONNECTOR_NOAVOID;
            
            if (_connRef) {
                _connRef->removeFromGraph();
                delete _connRef;
                _connRef = NULL;
                _invalid_path_connection.disconnect();
            }
        }
        return;

    }
    
    unsigned const handle_ix = key - SP_ATTR_CONNECTION_START;
    g_assert( handle_ix <= 1 );
    this->_connEnd[handle_ix]->setAttacherHref(value);
}

void
SPConnEndPair::writeRepr(Inkscape::XML::Node *const repr) const
{
    for (unsigned handle_ix = 0; handle_ix < 2; ++handle_ix) {
        if (this->_connEnd[handle_ix]->ref.getURI()) {
            char const * const attr_strs[] = {"inkscape:connection-start",
                                              "inkscape:connection-end"};
            gchar *uri_string = this->_connEnd[handle_ix]->ref.getURI()->toString();
            sp_repr_set_attr(repr, attr_strs[handle_ix], uri_string);
            g_free(uri_string);
        }
    }
}

void
SPConnEndPair::getAttachedItems(SPItem *h2attItem[2]) const {
    for (unsigned h = 0; h < 2; ++h) {
        h2attItem[h] = this->_connEnd[h]->ref.getObject();
    }
}

void
SPConnEndPair::getEndpoints(NR::Point endPts[]) const {
    SPCurve *curve = _path->curve;
    SPItem *h2attItem[2];
    getAttachedItems(h2attItem);
    
    for (unsigned h = 0; h < 2; ++h) {
        if ( h2attItem[h] ) {
            NRRect bboxrect;
            sp_item_invoke_bbox(h2attItem[h], &bboxrect,
                    sp_item_i2doc_affine(h2attItem[h]), true);
            NR::Rect bbox(bboxrect);
            endPts[h] = bbox.midpoint();
        }
        else 
        {
            if (h == 0) {
                endPts[h] = sp_curve_first_point(curve);
            }
            else {
                endPts[h] = sp_curve_last_point(curve);
            }
        }
    }
}

sigc::connection
SPConnEndPair::connectInvalidPath(sigc::slot<void, SPPath *> slot)
{
    return _invalid_path_signal.connect(slot);
}

static void emitPathInvalidationNotification(void *ptr)
{
    // We emit a signal here rather than just calling the reroute function
    // since this allows all the movement action computation to happen,
    // then all connectors (that require it) will be rerouted.  Otherwise,
    // one connector could get rerouted several times as a result of
    // dragging a couple of shapes.
   
    SPPath *path = SP_PATH(ptr);
    path->connEndPair._invalid_path_signal.emit(path);
}

void
SPConnEndPair::rerouteFromManipulation(void)
{
    _connRef->makePathInvalid();
    sp_conn_adjust_path(_path);
}

void
SPConnEndPair::reroute(void)
{
    sp_conn_adjust_path(_path);
}

// Called from sp_path_update to initialise the endpoints.
void
SPConnEndPair::update(void)
{
    if (_connType != SP_CONNECTOR_NOAVOID) {
        g_assert(_connRef != NULL);
        if (!(_connRef->isInitialised())) {
            NR::Point endPt[2];
            getEndpoints(endPt);

            Avoid::Point src = { endPt[0][NR::X], endPt[0][NR::Y] };
            Avoid::Point dst = { endPt[1][NR::X], endPt[1][NR::Y] };

            _connRef->lateSetup(src, dst);
            _connRef->setCallback(&emitPathInvalidationNotification, _path);
        }
    }
}
    

bool
SPConnEndPair::isAutoRoutingConn(void)
{
    if (_connType != SP_CONNECTOR_NOAVOID) {
        return true;
    }
    return false;
}
    
void
SPConnEndPair::makePathInvalid(void)
{
    _connRef->makePathInvalid();
}

void
SPConnEndPair::reroutePath(void)
{
    if (!isAutoRoutingConn()) {
        // Do nothing
        return;
    }

    SPCurve *curve = _path->curve;

    NR::Point endPt[2];
    getEndpoints(endPt);

    Avoid::Point src = { endPt[0][NR::X], endPt[0][NR::Y] };
    Avoid::Point dst = { endPt[1][NR::X], endPt[1][NR::Y] };
    
    _connRef->updateEndPoint(Avoid::VertID::src, src);
    _connRef->updateEndPoint(Avoid::VertID::tar, dst);

    _connRef->generatePath(src, dst);

    Avoid::PolyLine route = _connRef->route();
    _connRef->calcRouteDist();
    
    sp_curve_reset(curve);
    sp_curve_moveto(curve, endPt[0]);

    for (int i = 1; i < route.pn; ++i) {
        NR::Point p(route.ps[i].x, route.ps[i].y);
        sp_curve_lineto(curve, p);
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
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4 :
