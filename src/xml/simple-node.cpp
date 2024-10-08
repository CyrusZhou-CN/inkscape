// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 *  Garbage collected XML node implementation
 *//*
 * Authors: see git history
 *
 * Copyright (C) 2018 Authors
 * Copyright 2003-2005 MenTaLguY <mental@rydia.net>
 * Copyright 2003 Nathan Hurst
 * Copyright 1999-2003 Lauris Kaplinski
 * Copyright 2000-2002 Ximian Inc.
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

// clang-format off
#include "xml/node.h"
#include "xml/simple-node.h"
// clang-format on

#include <algorithm>
#include <cstring>
#include <string>

#include <glib.h>

#include "preferences.h"

#include "xml/node-fns.h"
#include "debug/event-tracker.h"
#include "debug/simple-event.h"
#include "util/format.h"

#include "attribute-rel-util.h"

namespace Inkscape {

namespace XML {

namespace {

std::shared_ptr<std::string> stringify_node(Node const &node) {
    gchar *string;
    switch (node.type()) {
    case NodeType::ELEMENT_NODE: {
        char const *id=node.attribute("id");
        if (id) {
            string = g_strdup_printf("element(%p)=%s(#%s)", &node, node.name(), id);
        } else {
            string = g_strdup_printf("element(%p)=%s", &node, node.name());
        }
    } break;
    case NodeType::TEXT_NODE:
        string = g_strdup_printf("text(%p)=%s", &node, node.content());
        break;
    case NodeType::COMMENT_NODE:
        string = g_strdup_printf("comment(%p)=<!--%s-->", &node, node.content());
        break;
    case NodeType::DOCUMENT_NODE:
        string = g_strdup_printf("document(%p)", &node);
        break;
    default:
        string = g_strdup_printf("unknown(%p)", &node);
    }
    std::shared_ptr<std::string> result = std::make_shared<std::string>(string);
    g_free(string);
    return result;
}

typedef Debug::SimpleEvent<Debug::Event::XML> DebugXML;

class DebugXMLNode : public DebugXML {
public:
    DebugXMLNode(Node const &node, char const *name)
    : DebugXML(name)
    {
        _addProperty("node", stringify_node(node));
    }
};

class DebugAddChild : public DebugXMLNode {
public:
    DebugAddChild(Node const &node, Node const &child, Node const *prev)
    : DebugXMLNode(node, "add-child")
    {
        _addProperty("child", stringify_node(child));
        _addProperty("position", prev ? prev->position() + 1 : 0 );
    }
};

class DebugRemoveChild : public DebugXMLNode {
public:
    DebugRemoveChild(Node const &node, Node const &child)
    : DebugXMLNode(node, "remove-child")
    {
        _addProperty("child", stringify_node(child));
    }
};

class DebugSetChildPosition : public DebugXMLNode {
public:
    DebugSetChildPosition(Node const &node, Node const &child,
                          Node const *old_prev, Node const *new_prev)
    : DebugXMLNode(node, "set-child-position")
    {
        _addProperty("child", stringify_node(child));

        unsigned old_position = ( old_prev ? old_prev->position() : 0 );
        unsigned position = ( new_prev ? new_prev->position() : 0 );
        if ( position > old_position ) {
            --position;
        }

        _addProperty("position", position);
    }
};

class DebugSetContent : public DebugXMLNode
{
public:
    DebugSetContent(Node const &node, Util::ptr_shared content)
        : DebugXMLNode(node, "set-content")
    {
        _addProperty("content", content.pointer());
    }
};

class DebugClearContent : public DebugXMLNode
{
public:
    DebugClearContent(Node const &node)
        : DebugXMLNode(node, "clear-content")
    {}
};

class DebugSetAttribute : public DebugXMLNode
{
public:
    DebugSetAttribute(Node const &node, GQuark name, Util::ptr_shared value)
        : DebugXMLNode(node, "set-attribute")
    {
        _addProperty("name", g_quark_to_string(name));
        _addProperty("value", value.pointer());
    }
};

class DebugClearAttribute : public DebugXMLNode
{
public:
    DebugClearAttribute(Node const &node, GQuark name)
        : DebugXMLNode(node, "clear-attribute")
    {
        _addProperty("name", g_quark_to_string(name));
    }
};

class DebugSetElementName : public DebugXMLNode
{
public:
    DebugSetElementName(Node const &node, GQuark name)
        : DebugXMLNode(node, "set-name")
    {
        _addProperty("name", g_quark_to_string(name));
    }
};

} // namespace

using Util::ptr_shared;
using Util::share_string;
using Util::share_unsafe;

SimpleNode::SimpleNode(int code, Document *document)
    : _name(code)
{
    g_assert(document != nullptr);

    this->_document = document;
    this->_parent = this->_next = this->_prev = nullptr;
    this->_first_child = this->_last_child = nullptr;

    _observers.add(_subtree_observers);
}

SimpleNode::SimpleNode(SimpleNode const &node, Document *document)
    : _cached_position(node._cached_position)
    , _name(node._name)
    , _content(node._content)
    , _child_count(node._child_count)
    , _cached_positions_valid(node._cached_positions_valid)
{
    g_assert(document != nullptr);

    _document = document;
    _parent = _next = _prev = nullptr;
    _first_child = _last_child = nullptr;

    for ( SimpleNode *child = node._first_child ;
          child != nullptr ; child = child->_next )
    {
        SimpleNode *child_copy=dynamic_cast<SimpleNode *>(child->duplicate(document));

        child_copy->_setParent(this);
        if (_last_child) { // not the first iteration
            _last_child->_next = child_copy;
            child_copy->_prev = _last_child;
        } else {
            _first_child = child_copy;
        }
        _last_child = child_copy;

        child_copy->release(); // release to avoid a leak
    }

    _attributes = node._attributes;

    _observers.add(_subtree_observers);
}

gchar const *SimpleNode::name() const {
    return g_quark_to_string(_name);
}

gchar const *SimpleNode::content() const {
    return this->_content;
}

gchar const *SimpleNode::attribute(gchar const *name) const {
    g_return_val_if_fail(name != nullptr, NULL);

    GQuark const key = g_quark_from_string(name);

    for (const auto & iter : _attributes)
    {
        if ( iter.key == key ) {
            return iter.value;
        }
    }

    return nullptr;
}

unsigned SimpleNode::position() const {
    g_return_val_if_fail(_parent != nullptr, 0);
    return _parent->_childPosition(*this);
}

unsigned SimpleNode::_childPosition(SimpleNode const &child) const {
    if (!_cached_positions_valid) {
        unsigned position=0;
        for ( SimpleNode *sibling = _first_child ;
              sibling ; sibling = sibling->_next )
        {
            sibling->_cached_position = position;
            position++;
        }
        _cached_positions_valid = true;
    }
    return child._cached_position;
}

Node *SimpleNode::nthChild(unsigned index) {
    SimpleNode *child = _first_child;
    for ( ; index > 0 && child ; child = child->_next ) {
        index--;
    }
    return child;
}

bool SimpleNode::matchAttributeName(gchar const *partial_name) const {
    g_return_val_if_fail(partial_name != nullptr, false);

    for ( const auto & iter : _attributes )
    {
        gchar const *name = g_quark_to_string(iter.key);
        if (std::strstr(name, partial_name)) {
            return true;
        }
    }

    return false;
}

void SimpleNode::_setParent(SimpleNode *parent) {
    if (_parent) {
        _subtree_observers.remove(_parent->_subtree_observers);
    }
    _parent = parent;
    if (parent) {
        _subtree_observers.add(parent->_subtree_observers);
    }
}

void SimpleNode::setContent(gchar const *content) {
    ptr_shared old_content=_content;
    ptr_shared new_content = ( content ? share_string(content) : ptr_shared() );

    Debug::EventTracker<> tracker;
    if (new_content) {
        tracker.set<DebugSetContent>(*this, new_content);
    } else {
        tracker.set<DebugClearContent>(*this);
    }

    _content = new_content;

    if ( _content != old_content ) {
        _document->logger()->notifyContentChanged(*this, old_content, _content);
        _observers.notifyContentChanged(*this, old_content, _content);
    }
}

void
SimpleNode::setAttributeImpl(gchar const *name, gchar const *value)
{
    g_return_if_fail(name && *name);

    // sanity check: `name` must not contain whitespace
    g_assert(std::none_of(name, name + strlen(name), [](char c) { return g_ascii_isspace(c); }));

    // Check usefulness of attributes on elements in the svg namespace, optionally don't add them to tree.
    Glib::ustring element = g_quark_to_string(_name);
    //g_message("setAttribute:  %s: %s: %s", element.c_str(), name, value);
    gchar* cleaned_value = g_strdup( value );

    // Only check elements in SVG name space and don't block setting attribute to NULL.
    // .raw() is only for performance reasons because Glib::ustring.== is slow
    if (value != nullptr && element.raw().starts_with("svg:")) {

        Inkscape::Preferences *prefs = Inkscape::Preferences::get();
        if( prefs->getBool("/options/svgoutput/check_on_editing") ) {

            gchar const *id_char = attribute("id");
            Glib::ustring id = (id_char == nullptr ? "" : id_char );
            unsigned int flags = sp_attribute_clean_get_prefs();
            bool attr_warn   = flags & SP_ATTRCLEAN_ATTR_WARN;
            bool attr_remove = flags & SP_ATTRCLEAN_ATTR_REMOVE;

            // Check attributes
            if( (attr_warn || attr_remove) && value != nullptr ) {
                bool is_useful = sp_attribute_check_attribute( element, id, name, attr_warn );
                if( !is_useful && attr_remove ) {
                    g_free( cleaned_value );
                    return; // Don't add to tree.
                }
            }

            // Check style properties -- Note: if element is not yet inserted into
            // tree (and thus has no parent), default values will not be tested.
            if( !strcmp( name, "style" ) && (flags >= SP_ATTRCLEAN_STYLE_WARN) ) {
                g_free( cleaned_value );
                cleaned_value = g_strdup( sp_attribute_clean_style( this, value, flags ).c_str() );
                // if( g_strcmp0( value, cleaned_value ) ) {
                //     g_warning( "SimpleNode::setAttribute: %s", id.c_str() );
                //     g_warning( "     original: %s", value);
                //     g_warning( "      cleaned: %s", cleaned_value);
                // }
            }
        }
    }

    GQuark const key = g_quark_from_string(name);

    AttributeRecord *ref = nullptr;
    for ( auto & existing : _attributes ) {
        if ( existing.key == key ) {
            ref = &existing;
            break;
        }
    }
    Debug::EventTracker<> tracker;

    ptr_shared old_value=( ref ? ref->value : ptr_shared() );

    ptr_shared new_value=ptr_shared();
    if (cleaned_value) { // set value of attribute
        new_value = share_string(cleaned_value);
        tracker.set<DebugSetAttribute>(*this, key, new_value);
        if (!ref) {
	    _attributes.emplace_back(key, new_value);
        } else {
            ref->value = new_value;
        }
    } else { //clearing attribute
        tracker.set<DebugClearAttribute>(*this, key);
        if (ref) {
	    _attributes.erase(std::find(_attributes.begin(),_attributes.end(),(*ref)));
        }
    }

    if ( new_value != old_value && (!old_value || !new_value || strcmp(old_value, new_value))) {
        _document->logger()->notifyAttributeChanged(*this, key, old_value, new_value);
        _observers.notifyAttributeChanged(*this, key, old_value, new_value);
        //g_warning( "setAttribute notified: %s: %s: %s: %s", name, element.c_str(), old_value, new_value ); 
    }
    g_free( cleaned_value );
}

void SimpleNode::setCodeUnsafe(int code) {
    GQuark old_code = static_cast<GQuark>(_name);
    GQuark new_code = static_cast<GQuark>(code);

    Debug::EventTracker<> tracker;
    tracker.set<DebugSetElementName>(*this, new_code);

    _name = static_cast<int>(new_code);

    if (new_code != old_code) {
        _document->logger()->notifyElementNameChanged(*this, old_code, new_code);
        _observers.notifyElementNameChanged(*this, old_code, new_code);
    }
}

void SimpleNode::addChild(Node *generic_child, Node *generic_ref) {
    g_assert(generic_child);
    g_assert(generic_child->document() == _document);
    g_assert(!generic_ref || generic_ref->document() == _document);

    SimpleNode *child=dynamic_cast<SimpleNode *>(generic_child);
    SimpleNode *ref=dynamic_cast<SimpleNode *>(generic_ref);

    g_assert(!ref || ref->_parent == this);
    g_assert(!child->_parent);

    Debug::EventTracker<DebugAddChild> tracker(*this, *child, ref);

    SimpleNode *next;
    if (ref) {
        next = ref->_next;
        ref->_next = child;

        child->_prev = ref;
    } else {
        if(_first_child) _first_child->_prev = child;
        next = _first_child;
        _first_child = child;
    }

    if (!next) { // appending?
        _last_child = child;
        // set cached position if possible when appending
        if (!ref) {
            // if !next && !ref, child is sole child
            child->_cached_position = 0;
            _cached_positions_valid = true;
        } else if (_cached_positions_valid) {
            child->_cached_position = ref->_cached_position + 1;
        }
    } else {
        next->_prev = child;
        // invalidate cached positions otherwise
        _cached_positions_valid = false;
    }

    child->_setParent(this);
    child->_next = next;
    _child_count++;

    _document->logger()->notifyChildAdded(*this, *child, ref);
    _observers.notifyChildAdded(*this, *child, ref);
}

void SimpleNode::removeChild(Node *generic_child) {
    g_assert(generic_child);
    g_assert(generic_child->document() == _document);

    SimpleNode *child=dynamic_cast<SimpleNode *>(generic_child);
    SimpleNode *ref=child->_prev;
    SimpleNode *next = child->_next;

    g_assert(child->_parent == this);

    Debug::EventTracker<DebugRemoveChild> tracker(*this, *child);

    if (ref) {
        ref->_next = next;
    } else {
        _first_child = next;
    }
    if (next) { // removing the last child?
        next->_prev = ref;
    } else {
        // removing any other child invalidates the cached positions
        _last_child = ref;
        _cached_positions_valid = false;
    }

    child->_next = nullptr;
    child->_prev = nullptr;
    child->_setParent(nullptr);
    _child_count--;

    _document->logger()->notifyChildRemoved(*this, *child, ref);
    _observers.notifyChildRemoved(*this, *child, ref);
}

void SimpleNode::changeOrder(Node *generic_child, Node *generic_ref) {
    g_assert(generic_child);
    g_assert(generic_child->document() == this->_document);
    g_assert(!generic_ref || generic_ref->document() == this->_document);

    SimpleNode *const child=dynamic_cast<SimpleNode *>(generic_child);
    SimpleNode *const ref=dynamic_cast<SimpleNode *>(generic_ref);

    g_return_if_fail(child->parent() == this);
    g_return_if_fail(child != ref);
    g_return_if_fail(!ref || ref->parent() == this);

    SimpleNode *const prev= child->_prev;

    Debug::EventTracker<DebugSetChildPosition> tracker(*this, *child, prev, ref);

    if (prev == ref) { return; }

    SimpleNode *next;

    /* Remove from old position. */
    next = child->_next;
    if (prev) {
        prev->_next = next;
    } else {
        _first_child = next;
    }
    if (next) {
        next->_prev = prev;
    } else {
        _last_child = prev;
    }

    /* Insert at new position. */
    if (ref) {
        next = ref->_next;
        ref->_next = child;
    } else {
        next = _first_child;
        _first_child = child;
    }

    child->_prev = ref;
    child->_next = next;

    if (next) {
        next->_prev = child;
    } else {
        _last_child = child;
    }

    _cached_positions_valid = false;

    _document->logger()->notifyChildOrderChanged(*this, *child, prev, ref);
    _observers.notifyChildOrderChanged(*this, *child, prev, ref);
}

void SimpleNode::setPosition(int pos) {
    g_return_if_fail(_parent != nullptr);

    // a position beyond the end of the list means the end of the list;
    // a negative position is the same as an infinitely large position

    SimpleNode *ref=nullptr;
    for ( SimpleNode *sibling = _parent->_first_child ;
          sibling && pos ; sibling = sibling->_next )
    {
        if ( sibling != this ) {
            ref = sibling;
            pos--;
        }
    }

    _parent->changeOrder(this, ref);
}

void SimpleNode::synthesizeEvents(NodeObserver &observer)
{
    for (auto const &iter : _attributes) {
        observer.notifyAttributeChanged(*this, iter.key, Util::ptr_shared(), iter.value);
    }

    SimpleNode *ref = nullptr;
    for (auto child = this->_first_child; child; child = child->_next) {
        observer.notifyChildAdded(*this, *child, ref);
        ref = child;
    }

    observer.notifyContentChanged(*this, Util::ptr_shared(), this->_content);
}

void SimpleNode::recursivePrintTree(unsigned level) {

    if (level == 0) {
        std::cout << "XML Node Tree" << std::endl;
    }
    std::cout << "XML: ";
    for (unsigned i = 0; i < level; ++i) {
        std::cout << "  ";
    }
    char const *id=attribute("id");
    if (id) {
        std::cout << id << std::endl;
    } else {
        std::cout << name() << std::endl;
    }
    for (SimpleNode *child = _first_child; child != nullptr; child = child->_next) {
        child->recursivePrintTree( level+1 );
    }
}

Node *SimpleNode::root() {
    Node *parent=this;
    while (parent->parent()) {
        parent = parent->parent();
    }

    if ( parent->type() == NodeType::DOCUMENT_NODE ) {
        for ( Node *child = _document->firstChild() ;
              child ; child = child->next() )
        {
            if ( child->type() == NodeType::ELEMENT_NODE ) {
                return child;
            }
        }
        return nullptr;
    } else if ( parent->type() == NodeType::ELEMENT_NODE ) {
        return parent;
    } else {
        return nullptr;
    }
}

void SimpleNode::cleanOriginal(Node *src, gchar const *key){
    std::vector<Node *> to_delete;
    for ( Node *child = this->firstChild() ; child != nullptr ; child = child->next() )
    {
        gchar const *id = child->attribute(key);
        if (id) {
            Node *rch = sp_repr_lookup_child(src, key, id);
            if (rch) {
                child->cleanOriginal(rch, key);
            } else {
                to_delete.push_back(child);
            }
        } else {
            to_delete.push_back(child);
        }
    }
    for (auto & i : to_delete) {
        removeChild(i);
    }
}
bool string_equal(const gchar *a,const gchar *b) {
    return g_strcmp0(a, b) == 0;
}

bool SimpleNode::equal(Node const *other, bool recursive, bool skip_ids) {
    if (!other || !string_equal(name(), other->name())) {
        return false;
    }
    if (!string_equal(content(), other->content())) {
        return false;
    }
    const AttributeVector & orig_attrs = attributeList();
    const AttributeVector & other_attrs = other->attributeList();
    size_t sizeorig = orig_attrs.size();
    size_t sizeother = other_attrs.size();
    if (sizeother != sizeorig) {
        return false;
    }
    for (size_t i = 0; i < sizeorig; i++) {
        const gchar * key_orig = g_quark_to_string(orig_attrs[i].key);
        if (skip_ids && string_equal(key_orig, "id")) {
            continue;
        }
        const gchar * key_other = g_quark_to_string(other_attrs[i].key);
        if (!string_equal(key_orig, key_other) || 
            !string_equal(orig_attrs[i].value, other_attrs[i].value) != 0) 
        {
            return false;
        }
    }
    if (recursive) {
        //NOTE: for faster the childs need to be in the same order
        Node const *other_child = other->firstChild();
        Node *child = firstChild();
        while (child && other_child) {
            if (!child->equal(other_child, recursive, skip_ids)) {
                return false;
            }
            child = child->next();
            other_child = other_child->next();
        }
        if ((!child && other_child) || (child && !other_child)) {
            return false;
        }
    }
    return true;
}

void SimpleNode::mergeFrom(Node const *src, gchar const *key, bool extension, bool clean) {
    g_return_if_fail(src != nullptr);
    g_return_if_fail(key != nullptr);
    g_assert(src != this);
    
    Node * srcp = const_cast<Node *>(src);
    if (srcp->equal(this, true)) {
        return;
    }
    setContent(src->content());
    if(_parent) {
        setPosition(src->position());
    }

    if (clean) {
        cleanOriginal(srcp, key);
    }

    for ( Node const *child = src->firstChild() ; child != nullptr ; child = child->next() )
    {
        gchar const *id = child->attribute(key);
        if (id) {
            Node *rch=sp_repr_lookup_child(this, key, id);
            if (rch && (!extension || rch->equal(child, false))) {
                rch->mergeFrom(child, key, extension);
                continue;
            } else {
                if(rch) {
                    removeChild(rch);
                }
            }
        }
        {
            guint pos = child->position();
            Node *rch=child->duplicate(_document);
            addChildAtPos(rch, pos);
            rch->release();
        }
    }

    for ( const auto & iter : src->attributeList() )
    {
        setAttribute(g_quark_to_string(iter.key), iter.value);
    }
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
