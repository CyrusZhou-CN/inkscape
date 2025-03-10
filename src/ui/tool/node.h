// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Editable node and associated data structures.
 */
/* Authors:
 *   Krzysztof Kosiński <tweenk.pl@gmail.com>
 *   Jon A. Cruz <jon@joncruz.org>
 *
 * Copyright (C) 2009 Authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef INKSCAPE_UI_TOOL_NODE_H
#define INKSCAPE_UI_TOOL_NODE_H

#include <cstddef>
#include <iosfwd>
#include <list>
#include <memory>
#include <optional>
#include <2geom/point.h>
#include <boost/noncopyable.hpp>

#include "snap-candidate.h"
#include "ui/tool/selectable-control-point.h"
#include "ui/tool/node-types.h"

namespace Inkscape {
class CanvasItemGroup;
class CanvasItemCurve;

namespace UI {

class PathManipulator;
class MultiPathManipulator;

class Node;
class Handle;
class NodeList;
class SubpathList;
template <typename> class NodeIterator;

std::ostream &operator<<(std::ostream &, NodeType);

struct ListNode
{
    ListNode *ln_next;
    ListNode *ln_prev;
    NodeList *ln_list;
};

struct NodeSharedData
{
    SPDesktop *desktop;
    ControlPointSelection *selection;
    Inkscape::CanvasItemGroup *node_group;
    Inkscape::CanvasItemGroup *handle_group;
    Inkscape::CanvasItemGroup *handle_line_group;
};

class Handle : public ControlPoint
{
public:
    ~Handle() override;

    inline Geom::Point relativePos() const;
    inline double length() const;
    bool isDegenerate() const { return _degenerate; } // True if the handle is retracted, i.e. has zero length.

    void setVisible(bool) override;
    void move(Geom::Point const &p) override;

    void setPosition(Geom::Point const &p) override;
    inline void setRelativePos(Geom::Point const &p);
    void setLength(double len);
    void retract();
    void setDirection(Geom::Point const &from, Geom::Point const &to);
    void setDirection(Geom::Point const &dir);
    Node *parent() { return _parent; }
    Handle *other();
    Handle const *other() const;

    static char const *handle_type_to_localized_string(NodeType type);

protected:
    Handle(NodeSharedData const &data, Geom::Point const &initial_pos, Node *parent);

    virtual void handle_2button_press();
    bool _eventHandler(Inkscape::UI::Tools::ToolBase *event_context, CanvasEvent const &event) override;
    void dragged(Geom::Point &new_pos, MotionEvent const &event) override;
    bool grabbed(MotionEvent const &event) override;
    void ungrabbed(ButtonReleaseEvent const *event) override;
    bool clicked(ButtonReleaseEvent const &event) override;

    Glib::ustring _getTip(unsigned state) const override;
    Glib::ustring _getDragTip(MotionEvent const &event) const override;
    bool _hasDragTips() const override { return true; }

private:
    inline PathManipulator &_pm();
    inline PathManipulator &_pm() const;
    void _update_bspline_handles();
    Node *_parent; // the handle's lifetime does not extend beyond that of the parent node,
    // so a naked pointer is OK and allows setting it during Node's construction
    CanvasItemPtr<CanvasItemCurve> _handle_line;
    bool _degenerate; // True if the handle is retracted, i.e. has zero length. This is used often internally so it makes sense to cache this

    /**
     * Control point of a cubic Bezier curve in a path.
     *
     * Handle keeps the node type invariant only for the opposite handle of the same node.
     * Keeping the invariant on node moves is left to the %Node class.
     */
    static Geom::Point _saved_other_pos;
    static Geom::Point _saved_dir;
    static double _saved_length;
    static bool _drag_out;
    friend class Node;
};

class Node
    : ListNode
    , public SelectableControlPoint
{
public:
    /**
     * Curve endpoint in an editable path.
     *
     * The method move() keeps node type invariants during translations.
     */
    Node(NodeSharedData const &data, Geom::Point const &pos);

    Node(Node const &) = delete;

    void move(Geom::Point const &p) override;
    void transform(Geom::Affine const &m) override;
    void fixNeighbors() override;
    Geom::Rect bounds() const override;

    NodeType type() const { return _type; }

    /**
     * Sets the node type and optionally restores the invariants associated with the given type.
     * @param type The type to set.
     * @param update_handles Whether to restore invariants associated with the given type.
     *                       Passing false is useful e.g. when initially creating the path,
     *                       and when making cusp nodes during some node algorithms.
     *                       Pass true when used in response to an UI node type button.
     */
    void setType(NodeType type, bool update_handles = true);

    void showHandles(bool v);

    void updateHandles();

    /**
     * Pick the best type for this node, based on the position of its handles.
     * This is what assigns types to nodes created using the pen tool.
     */
    void pickBestType(); // automatically determine the type from handle positions

    bool isDegenerate() const { return _front.isDegenerate() && _back.isDegenerate(); }
    bool isEndNode() const;
    Handle *front() { return &_front; }
    Handle *back()  { return &_back;  }

    /**
     * Gets the handle that faces the given adjacent node.
     * Will abort with error if the given node is not adjacent.
     */
    Handle *handleToward(Node *to);

    /**
     * Gets the node in the direction of the given handle.
     * Will abort with error if the handle doesn't belong to this node.
     */
    Node *nodeToward(Handle *h);

    /**
     * Gets the handle that goes in the direction opposite to the given adjacent node.
     * Will abort with error if the given node is not adjacent.
     */
    Handle *handleAwayFrom(Node *to);

    /**
     * Gets the node in the direction opposite to the given handle.
     * Will abort with error if the handle doesn't belong to this node.
     */
    Node *nodeAwayFrom(Handle *h);

    NodeList &nodeList() { return *(static_cast<ListNode*>(this)->ln_list); }
    NodeList &nodeList() const { return *(static_cast<ListNode const*>(this)->ln_list); }

    /**
     * Move the node to the bottom of its canvas group.
     * Useful for node break, to ensure that the selected nodes are above the unselected ones.
     */
    void sink();

    static NodeType parse_nodetype(char x);
    static char const *node_type_to_localized_string(NodeType type);

    // temporarily public
    /** Customized event handler to catch scroll events needed for selection grow/shrink. */
    bool _eventHandler(Inkscape::UI::Tools::ToolBase *event_context, CanvasEvent const &event) override;

    Inkscape::SnapCandidatePoint snapCandidatePoint();

protected:
    void dragged(Geom::Point &new_pos, MotionEvent const &event) override;
    bool grabbed(MotionEvent const &event) override;
    bool clicked(ButtonReleaseEvent const &event) override;

    void _setState(State state) override;
    Glib::ustring _getTip(unsigned state) const override;
    Glib::ustring _getDragTip(MotionEvent const &event) const override;
    bool _hasDragTips() const override { return true; }

private:
    void _updateAutoHandles();

    /**
     * Select or deselect a node in this node's subpath based on its path distance from this node.
     * @param dir If negative, shrink selection by one node; if positive, grow by one node.
     */
    void _linearGrow(int dir);

    Node *_next();
    Node const *_next() const;
    Node *_prev();
    Node const *_prev() const;
    Inkscape::SnapSourceType _snapSourceType() const;
    Inkscape::SnapTargetType _snapTargetType() const;
    inline PathManipulator &_pm();
    inline PathManipulator &_pm() const;

    /** Determine whether two nodes are joined by a linear segment. */
    static bool _is_line_segment(Node *first, Node *second);

    // Handles are always present, but are not visible if they coincide with the node
    // (are degenerate). A segment that has both handles degenerate is always treated
    // as a line segment
    Handle _front; ///< Node handle in the backward direction of the path
    Handle _back; ///< Node handle in the forward direction of the path
    NodeType _type; ///< Type of node - cusp, smooth...
    bool _handles_shown;

    // This is used by fixNeighbors to repair smooth nodes after all move
    // operations have been completed. If this is empty, no fixing is needed.
    std::optional<Geom::Point> _unfixed_pos;

    friend class Handle;
    friend class NodeList;
    friend class NodeIterator<Node>;
    friend class NodeIterator<Node const>;
};

/// Iterator for editable nodes
/** Use this class for all operations that require some knowledge about the node's
 * neighbors. It is a bidirectional iterator.
 *
 * Because paths can be cyclic, node iterators have two different ways to
 * increment and decrement them. When using ++/--, the end iterator will eventually
 * be returned. When using advance()/retreat(), the end iterator will only be returned
 * when the path is open. If it's closed, calling advance() will cycle indefinitely.
 * This is particularly useful for cases where the adjacency of nodes is more important
 * than their sequence order.
 *
 * When @a i is a node iterator, then:
 * - <code>++i</code> moves the iterator to the next node in sequence order;
 * - <code>--i</code> moves the iterator to the previous node in sequence order;
 * - <code>i.next()</code> returns the next node with wrap-around;
 * - <code>i.prev()</code> returns the previous node with wrap-around;
 * - <code>i.advance()</code> moves the iterator to the next node with wrap-around;
 * - <code>i.retreat()</code> moves the iterator to the previous node with wrap-around.
 *
 * next() and prev() do not change their iterator. They can return the end iterator
 * if the path is open.
 *
 * Unlike most other iterators, you can check whether you've reached the end of the list
 * without having access to the iterator's container.
 * Simply use <code>if (i) { ...</code>
 * */
template <typename N>
class NodeIterator
    : public boost::bidirectional_iterator_helper<NodeIterator<N>, N, std::ptrdiff_t, N*, N&>
{
public:
    using self = NodeIterator;
    NodeIterator()
        : _node(nullptr)
    {}
    // default copy, default assign

    self &operator++() {
        _node = (_node?_node->ln_next:nullptr);
        return *this;
    }
    self &operator--() {
        _node = (_node?_node->ln_prev:nullptr);
        return *this;
    }
    bool operator==(self const &other) const { return _node == other._node; }
    N &operator*() const { return *static_cast<N*>(_node); }
    inline operator bool() const; // define after NodeList
    /// Get a pointer to the underlying node. Equivalent to <code>&*i</code>.
    N *get_pointer() const { return static_cast<N*>(_node); }
    /// @see get_pointer()
    N *ptr() const { return static_cast<N*>(_node); }

    self next() const {
        self r(*this);
        r.advance();
        return r;
    }
    self prev() const {
        self r(*this);
        r.retreat();
        return r;
    }
    self &advance();
    self &retreat();

private:
    NodeIterator(ListNode const *n)
        : _node(const_cast<ListNode*>(n))
    {}
    ListNode *_node;
    friend class NodeList;
};

class NodeList
    : ListNode
    , boost::noncopyable
{
public:
    using size_type = std::size_t;
    using reference = Node &;
    using const_reference = Node const &;
    using pointer = Node *;
    using const_pointer = Node const *;
    using value_type = Node;
    using iterator = NodeIterator<value_type>;
    using const_iterator = NodeIterator<value_type const>;

    // TODO Lame. Make this private and make SubpathList a factory
    /**
     * An editable list of nodes representing a subpath.
     *
     * It can optionally be cyclic to represent a closed path.
     * The list has iterators that act like plain node iterators, but can also be used
     * to obtain shared pointers to nodes.
     */
    NodeList(SubpathList &_list);
    ~NodeList();

    // iterators
    iterator begin() { return iterator(ln_next); }
    iterator end() { return iterator(this); }
    const_iterator begin() const { return const_iterator(ln_next); }
    const_iterator end() const { return const_iterator(this); }

    // size
    bool empty() const;
    size_type size() const;

    // extra node-specific methods
    bool closed() const { return _closed; }

    /**
     * A subpath is degenerate if it has no segments - either one node in an open path
     * or no nodes in a closed path.
     */
    bool degenerate() const;

    void setClosed(bool c) { _closed = c; }
    iterator before(double t, double *fracpart = nullptr);
    iterator before(Geom::PathTime const &pvp);
    const_iterator before(double t, double *fracpart = nullptr) const {
        return const_cast<NodeList *>(this)->before(t, fracpart)._node;
    }
    const_iterator before(Geom::PathTime const &pvp) const {
        return const_cast<NodeList *>(this)->before(pvp)._node;
    }

    // list operations

    /** insert a node before pos. */
    iterator insert(iterator pos, Node *x);

    template <class InputIterator>
    void insert(iterator pos, InputIterator first, InputIterator last) {
        for (; first != last; ++first) insert(pos, *first);
    }
    void splice(iterator pos, NodeList &list);
    void splice(iterator pos, NodeList &list, iterator i);
    void splice(iterator pos, NodeList &list, iterator first, iterator last);
    void reverse();
    void shift(int n);
    void push_front(Node *x) { insert(begin(), x); }
    void pop_front() { erase(begin()); }
    void push_back(Node *x) { insert(end(), x); }
    void pop_back() { erase(--end()); }
    void clear();
    iterator erase(iterator pos);
    iterator erase(iterator first, iterator last) {
        NodeList::iterator ret = first;
        while (first != last) ret = erase(first++);
        return ret;
    }

    // member access - undefined results when the list is empty
    Node &front() { return *static_cast<Node*>(ln_next); }
    Node &back() { return *static_cast<Node*>(ln_prev); }

    // HACK remove this subpath from its path. This will be removed later.
    void kill();

    SubpathList const &subpathList() const { return _list; }
    SubpathList       &subpathList()       { return _list; }

    static iterator get_iterator(Node *n) { return iterator(n); }
    static const_iterator get_iterator(Node const *n) { return const_iterator(n); }
    static NodeList &get(Node *n);
    static NodeList &get(iterator const &i);

private:
    SubpathList &_list;
    bool _closed = false;

    friend class Node;
    friend class Handle; // required to access handle and handle line groups
    friend class NodeIterator<Node>;
    friend class NodeIterator<Node const>;
};

/**
 * List of node lists. Represents an editable path.
 * Editable path composed of one or more subpaths.
 */
class SubpathList : public std::list<std::shared_ptr<NodeList>>
{
public:
    using list_type = std::list<std::shared_ptr<NodeList>>;

    SubpathList(PathManipulator &pm) : _path_manipulator(pm) {}

    PathManipulator const &pm() const { return _path_manipulator; }
    PathManipulator       &pm()       { return _path_manipulator; }

private:
    list_type _nodelists;
    PathManipulator &_path_manipulator;
    friend class NodeList;
    friend class Node;
    friend class Handle;
};

// define inline Handle funcs after definition of Node
inline Geom::Point Handle::relativePos() const {
    return position() - _parent->position();
}
inline void Handle::setRelativePos(Geom::Point const &p) {
    setPosition(_parent->position() + p);
}
inline double Handle::length() const {
    return relativePos().length();
}
inline PathManipulator &Handle::_pm() {
    return _parent->_pm();
}
inline PathManipulator &Handle::_pm() const {
    return _parent->_pm();
}
inline PathManipulator &Node::_pm() {
    return nodeList().subpathList().pm();
}

inline PathManipulator &Node::_pm() const {
    return nodeList().subpathList().pm();
}

// definitions for node iterator
template <typename N>
NodeIterator<N>::operator bool() const {
    return _node && static_cast<ListNode*>(_node->ln_list) != _node;
}
template <typename N>
NodeIterator<N> &NodeIterator<N>::advance() {
    ++(*this);
    if (G_UNLIKELY(!*this) && _node->ln_list->closed()) ++(*this);
    return *this;
}
template <typename N>
NodeIterator<N> &NodeIterator<N>::retreat() {
    --(*this);
    if (G_UNLIKELY(!*this) && _node->ln_list->closed()) --(*this);
    return *this;
}

} // namespace UI
} // namespace Inkscape

#endif // INKSCAPE_UI_TOOL_NODE_H

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
