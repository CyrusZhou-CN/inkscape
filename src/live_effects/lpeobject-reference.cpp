// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * The reference corresponding to the inkscape:live-effect attribute
 *
 * Copyright (C) 2007 Johan Engelen
 *
 * Release under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "live_effects/lpeobject-reference.h"
#include "live_effects/effect.h"

#include <cstring>

#include "bad-uri-exception.h"
#include "live_effects/lpeobject.h"
#include "object/uri.h"

namespace Inkscape {

namespace LivePathEffect {

static void lpeobjectreference_href_changed(SPObject *old_ref, SPObject *ref, LPEObjectReference *lpeobjref);
static void lpeobjectreference_release_self(SPObject *release, LPEObjectReference *lpeobjref);

LPEObjectReference::LPEObjectReference(SPObject* i_owner) : URIReference(i_owner)
{
    owner=i_owner;
    lpeobject_href = nullptr;
    lpeobject_repr = nullptr;
    lpeobject = nullptr;
    _changed_connection = changedSignal().connect(sigc::bind(sigc::ptr_fun(lpeobjectreference_href_changed), this)); // listening to myself, this should be virtual instead

    user_unlink = nullptr;
}

LPEObjectReference::~LPEObjectReference()
{
    std::vector<SPLPEItem *> lpeitems;
    if (lpeobject && lpeobject->get_lpe()) {
        lpeitems = lpeobject->get_lpe()->getCurrrentLPEItems();
        lpeobject->get_lpe()->doOnBeforeCommit();
        lpeobject->get_lpe()->sp_lpe_item = nullptr;
    } 
    _changed_connection.disconnect(); // to do before unlinking
    quit_listening();
    unlink();
    for (auto lpeitem : lpeitems) {
        sp_lpe_item_update_patheffect(lpeitem,false,false);
    }
}

bool LPEObjectReference::_acceptObject(SPObject * const obj) const
{
    auto lpobj = cast<LivePathEffectObject>(obj);
    if (lpobj) {
        return URIReference::_acceptObject(obj);
    } else {
        return false;
    }
}

void
LPEObjectReference::link(const char *to)
{
    if (!to || !to[0]) {
        quit_listening();
        unlink();
    } else {
        if ( !lpeobject_href || ( strcmp(to, lpeobject_href) != 0 ) ) {
            if (lpeobject_href) {
                g_free(lpeobject_href);
            }
            lpeobject_href = g_strdup(to);
            try {
                attach(Inkscape::URI(to));
            } catch (Inkscape::BadURIException &e) {
                /* TODO: Proper error handling as per
                 * http://www.w3.org/TR/SVG11/implnote.html#ErrorProcessing.
                 */
                g_warning("%s", e.what());
                detach();
            }
        }
    }
}

void
LPEObjectReference::unlink()
{
    if (lpeobject_href) {
        g_free(lpeobject_href);
        lpeobject_href = nullptr;
    }
    detach();
}

void
LPEObjectReference::start_listening(LivePathEffectObject* to)
{
    if ( to == nullptr ) {
        return;
    }
    lpeobject = to;
    lpeobject_repr = to->getRepr();
    _release_connection = to->connectRelease(sigc::bind(sigc::ptr_fun(&lpeobjectreference_release_self), this));
}

void
LPEObjectReference::quit_listening()
{
    _modified_connection.disconnect();
    _release_connection.disconnect();
    lpeobject_repr = nullptr;
    lpeobject = nullptr;
}

static void
lpeobjectreference_href_changed(SPObject */*old_ref*/, SPObject */*ref*/, LPEObjectReference *lpeobjref)
{
    //lpeobjref->quit_listening();
    auto refobj = cast<LivePathEffectObject>( lpeobjref->getObject() );
    if ( refobj ) {
        lpeobjref->start_listening(refobj);
    }
    if (lpeobjref->owner) {
        lpeobjref->owner->requestDisplayUpdate(SP_OBJECT_MODIFIED_FLAG);
    }
}

static void
lpeobjectreference_release_self(SPObject */*release*/, LPEObjectReference *lpeobjref)
{
    lpeobjref->quit_listening();
    lpeobjref->unlink();
    if (lpeobjref->user_unlink) {
        lpeobjref->user_unlink(lpeobjref, lpeobjref->owner);
    }
    
}

} //namespace LivePathEffect

} // namespace inkscape

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
