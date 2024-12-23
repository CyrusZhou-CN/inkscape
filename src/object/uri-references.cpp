// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * Helper methods for resolving URI References
 *
 * Authors:
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   Marc Jeanmougin
 *
 * Copyright (C) 2001-2002 Lauris Kaplinski
 * Copyright (C) 2001 Ximian, Inc.
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "uri-references.h"

#include <iostream>
#include <cstring>

#include <glibmm/miscutils.h>
#include "live_effects/lpeobject.h"
#include "bad-uri-exception.h"
#include "document.h"
#include "sp-object.h"
#include "uri.h"
#include "extract-uri.h"
#include "sp-tag-use.h"

namespace Inkscape {

URIReference::URIReference(SPObject *owner)
    : _owner(owner)
    , _owner_document(nullptr)
    , _obj(nullptr)
    , _uri(nullptr)
{
    g_assert(_owner != nullptr);
    /* FIXME !!! attach to owner's destroy signal to clean up in case */
}

URIReference::URIReference(SPDocument *owner_document)
    : _owner(nullptr)
    , _owner_document(owner_document)
    , _obj(nullptr)
    , _uri(nullptr)
{
    g_assert(_owner_document != nullptr);
}

URIReference::~URIReference()
{
    detach();
}

/*
 * The main ideas here are:
 * (1) "If we are inside a clone, then we can accept if and only if our "original thing" can accept the reference"
 * (this caused problems when there are clones because a change in ids triggers signals for the object hrefing this id,
 * but also its cloned reprs(descendants of <use> referencing an ancestor of the href'ing object)). 
 *
 * (2) Once we have an (potential owner) object, it can accept a href to obj, iff the graph of objects where directed
 * edges are
 * either parent->child relations , *** or href'ing to href'ed *** relations, stays acyclic.
 * We can go either from owner and up in the tree, or from obj and down, in either case this will be in the worst case
 *linear in the number of objects.
 * There are no easy objects allowing to do the second proposition, while "hrefList" is a "list of objects href'ing us",
 *so we'll take this.
 * Then we keep a set of already visited elements, and do a DFS on this graph. if we find obj, then BOOM.
 */

bool URIReference::_acceptObject(SPObject *obj) const
{
    // we go back following hrefList and parent to find if the object already references ourselves indirectly
    std::set<SPObject *> done;
    SPObject *owner = getOwner();
    //allow LPE as owner has any URI attached
    auto lpobj = cast<LivePathEffectObject>(obj);
    if (!owner || lpobj)
        return true;
    
    while (owner->cloned) {
        if(!owner->clone_original)//happens when the clone is existing and linking to something, even before the original objects exists.
                                  //for instance, it can happen when you paste a filtered object in a already cloned group: The construction of the 
                                  //clone representation of the filtered object will finish before the original object, so the cloned repr will
                                  //have to _accept the filter even though the original does not exist yet. In that case, we'll accept iff the parent of the 
                                  //original can accept it: loops caused by other relations than parent-child would be prevented when created on their base object.
                                  //Fixes bug 1636533.
            owner = owner->parent;
        else
            owner = owner->clone_original;
    }
    // once we have the "original" object (hopefully) we look at who is referencing it
    if (obj == owner)
        return false;
    std::list<SPObject *> todo(owner->hrefList);
    todo.push_front(owner->parent);
    while (!todo.empty()) {
        SPObject *e = todo.front();
        todo.pop_front();
        if (!e)
            continue;
        if (done.insert(e).second) {
            if (e == obj) {
                return false;
            }
            todo.push_front(e->parent);
            todo.insert(todo.begin(), e->hrefList.begin(), e->hrefList.end());
        }
    }
    return true;
}

void URIReference::attach(const URI &uri)
{
    SPDocument *document = nullptr;

    // Attempt to get the document that contains the URI
    if (_owner) {
        document = _owner->document;
    } else if (_owner_document) {
        document = _owner_document;
    }

    // createChildDoc() assumes that the referenced file is an SVG.
    // PNG and JPG files are allowed (in the case of feImage).
    gchar const *filename = uri.getPath() ? uri.getPath() : "";
    bool skip = false;
    if (g_str_has_suffix(filename, ".jpg") || g_str_has_suffix(filename, ".JPG") ||
        g_str_has_suffix(filename, ".png") || g_str_has_suffix(filename, ".PNG")) {
        skip = true;
    }

    // The path contains references to separate document files to load.
    if (document && uri.getPath() && !skip) {
        char const *base = document->getDocumentBase();
        auto absuri = URI::from_href_and_basedir(uri.str().c_str(), base);
        std::string path;

        try {
            path = absuri.toNativeFilename();
        } catch (const Glib::Error &e) {
            g_warning("%s", e.what());
        }

        if (!path.empty()) {
            document = document->createChildDoc(path);
        } else {
            document = nullptr;
        }
    }
    if (!document) {
        g_warning("Can't get document for referenced URI: %s", filename);
        return;
    }

    gchar const *fragment = uri.getFragment();
    if (uri.getQuery() || !fragment) {
        throw UnsupportedURIException();
    }

    /* FIXME !!! real xpointer support should be delegated to document */
    /* for now this handles the minimal xpointer form that SVG 1.0
     * requires of us
     */
    gchar *id = nullptr;
    if (!strncmp(fragment, "xpointer(", 9)) {
        /* FIXME !!! this is wasteful */
        /* FIXME: It looks as though this is including "))" in the id.  I suggest moving
           the strlen calculation and validity testing to before strdup, and copying just
           the id without the "))".  -- pjrm */
        if (!strncmp(fragment, "xpointer(id(", 12)) {
            id = g_strdup(fragment + 12);
            size_t const len = strlen(id);
            if (len < 3 || strcmp(id + len - 2, "))")) {
                g_free(id);
                throw MalformedURIException();
            }
        } else {
            throw UnsupportedURIException();
        }
    } else {
        id = g_strdup(fragment);
    }

    /* FIXME !!! validate id as an NCName somewhere */

    _connection.disconnect();
    delete _uri;
    _uri = new URI(uri);

    _setObject(document->getObjectById(id));
    _connection = document->connectIdChanged(id, sigc::mem_fun(*this, &URIReference::_setObject));
    g_free(id);
}

bool URIReference::try_attach(char const *uri)
{
    if (uri && uri[0]) {
        try {
            attach(Inkscape::URI(uri));
            return true;
        } catch (Inkscape::BadURIException &e) {
            g_warning("%s", e.what());
        }
    }
    detach();
    return false;
}

void URIReference::detach()
{
    _connection.disconnect();
    delete _uri;
    _uri = nullptr;
    _setObject(nullptr);
}

void URIReference::_setObject(SPObject *obj)
{
    if (obj && !_acceptObject(obj)) {
        obj = nullptr;
    }

    if (obj == _obj)
        return;

    SPObject *old_obj = _obj;
    _obj = obj;

    _release_connection.disconnect();
    if (_obj && (!_owner || !_owner->cloned)) {
        _obj->hrefObject(_owner);
        _release_connection = _obj->connectRelease(sigc::mem_fun(*this, &URIReference::_release));
    }
    _changed_signal.emit(old_obj, _obj);
    if (old_obj && (!_owner || !_owner->cloned)) {
        /* release the old object _after_ the signal emission */
        old_obj->unhrefObject(_owner);
    }
}

/* If an object is deleted, current semantics require that we release
 * it on its "release" signal, rather than later, when its ID is actually
 * unregistered from the document.
 */
void URIReference::_release(SPObject *obj)
{
    g_assert(_obj == obj);
    _setObject(nullptr);
}

} /* namespace Inkscape */



SPObject *sp_css_uri_reference_resolve(SPDocument *document, const gchar *uri)
{
    SPObject *ref = nullptr;

    if (document && uri && (strncmp(uri, "url(", 4) == 0)) {
        auto trimmed = extract_uri(uri);
        if (!trimmed.empty()) {
            ref = sp_uri_reference_resolve(document, trimmed.c_str());
        }
    }

    return ref;
}

SPObject *sp_uri_reference_resolve(SPDocument *document, const gchar *uri)
{
    SPObject *ref = nullptr;

    if (uri && (*uri == '#')) {
        ref = document->getObjectById(uri + 1);
    }

    return ref;
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
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:fileencoding=utf-8 :
