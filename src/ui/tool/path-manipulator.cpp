// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * Path manipulator - implementation.
 */
/* Authors:
 *   Krzysztof Kosiński <tweenk.pl@gmail.com>
 *   Abhishek Sharma
 *
 * Copyright (C) 2009 Authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <2geom/bezier-utils.h>
#include <2geom/path-sink.h>
#include <2geom/point.h>

#include <utility>
#include <vector>

#include "display/curve.h"
#include "display/control/canvas-item-bpath.h"

#include <2geom/forward.h>
#include "helper/geom.h"

#include "live_effects/lpeobject.h"
#include "live_effects/lpe-powerstroke.h"
#include "live_effects/lpe-bspline.h"
#include "live_effects/parameter/path.h"

#include "object/sp-path.h"
#include "style.h"

#include "ui/icon-names.h"
#include "ui/tool/control-point-selection.h"
#include "ui/tool/curve-drag-point.h"
#include "ui/tool/multi-path-manipulator.h"
#include "ui/tool/node-types.h"
#include "ui/tool/path-manipulator.h"
#include "ui/tools/node-tool.h"
#include "ui/widget/events/canvas-event.h"
#include "path/splinefit/bezier-fit.h"
#include "xml/node-observer.h"

namespace Inkscape::UI {

namespace {

/// Types of path changes that we must react to.
enum PathChange {
    PATH_CHANGE_D,
    PATH_CHANGE_TRANSFORM
};

} // anonymous namespace
static constexpr double BSPLINE_TOL = 0.001;
static constexpr double NO_POWER = 0.0;
static constexpr double DEFAULT_START_POWER = 1.0/3.0;

/**
 * Notifies the path manipulator when something changes the path being edited
 * (e.g. undo / redo)
 */
class PathManipulatorObserver : public Inkscape::XML::NodeObserver {
public:
    PathManipulatorObserver(PathManipulator *p, Inkscape::XML::Node *node)
        : _pm(p)
        , _node(node)
        , _blocked(false)
    {
        Inkscape::GC::anchor(_node);
        _node->addObserver(*this);
    }

    ~PathManipulatorObserver() override {
        _node->removeObserver(*this);
        Inkscape::GC::release(_node);
    }

    void notifyAttributeChanged(Inkscape::XML::Node &/*node*/, GQuark attr,
        Util::ptr_shared, Util::ptr_shared) override
    {
        // do nothing if blocked
        if (_blocked) return;

        GQuark path_d = g_quark_from_static_string("d");
        GQuark path_transform = g_quark_from_static_string("transform");
        GQuark lpe_quark = _pm->_lpe_key.empty() ? 0 : g_quark_from_string(_pm->_lpe_key.data());

        // only react to "d" (path data) and "transform" attribute changes
        if (attr == lpe_quark || attr == path_d) {
            _pm->_externalChange(PATH_CHANGE_D);
        } else if (attr == path_transform) {
            _pm->_externalChange(PATH_CHANGE_TRANSFORM);
        }
    }

    void block() { _blocked = true; }
    void unblock() { _blocked = false; }
private:
    PathManipulator *_pm;
    Inkscape::XML::Node *_node;
    bool _blocked;
};

void build_segment(Geom::PathBuilder &, Node *, Node *);
PathManipulator::PathManipulator(MultiPathManipulator &mpm, SPObject *path,
        Geom::Affine const &et, guint32 outline_color, Glib::ustring lpe_key)
    : PointManipulator(mpm._path_data.node_data.desktop, *mpm._path_data.node_data.selection)
    , _subpaths(*this)
    , _multi_path_manipulator(mpm)
    , _path(path)
    , _dragpoint(new CurveDragPoint(*this))
    , /* XML Tree being used here directly while it shouldn't be*/_observer(new PathManipulatorObserver(this, path->getRepr()))
    , _edit_transform(et)
    , _lpe_key(std::move(lpe_key))
{
    auto lpeobj = cast<LivePathEffectObject>(_path);
    auto pathshadow = cast<SPPath>(_path);
    if (!lpeobj) {
        _i2d_transform = pathshadow->i2dt_affine();
    } else {
        _i2d_transform = Geom::identity();
    }
    _d2i_transform = _i2d_transform.inverse();
    _dragpoint->setVisible(false);

    _getGeometry();

    _outline = make_canvasitem<Inkscape::CanvasItemBpath>(_multi_path_manipulator._path_data.outline_group);
    _outline->set_visible(false);
    _outline->set_stroke(outline_color);
    _outline->set_fill(0x0, SP_WIND_RULE_NONZERO);

    _selection.signal_update.connect(
        sigc::bind(sigc::mem_fun(*this, &PathManipulator::update), false));
    _selection.signal_selection_changed.connect(
        sigc::mem_fun(*this, &PathManipulator::_selectionChangedM));
    _desktop->signal_zoom_changed.connect(
        sigc::hide( sigc::mem_fun(*this, &PathManipulator::_updateOutlineOnZoomChange)));

    //Define if the path is BSpline on construction
    _recalculateIsBSpline();
    _createControlPointsFromGeometry();
}

PathManipulator::~PathManipulator()
{
    delete _dragpoint;
    delete _observer;
    _outline.reset();
    clear();
}

/** Handle motion events to update the position of the curve drag point. */
bool PathManipulator::event(Tools::ToolBase *, CanvasEvent const &event)
{
    if (empty()) {
        return false;
    }

    inspect_event(event,
        [&] (MotionEvent const &event) {
            _updateDragPoint(event.pos);
        },
        [&] (CanvasEvent const &event) {}
    );

    return false;
}

/** Check whether the manipulator has any nodes. */
bool PathManipulator::empty() {
    return !_path || _subpaths.empty();
}

/** Update the display and the outline of the path.
 * \param alert_LPE if true, alerts an applied LPE to what the path is going to be changed to, so it can adjust its parameters for nicer user interfacing
 */
void PathManipulator::update(bool alert_LPE)
{
    _observer->block();
    _createGeometryFromControlPoints(alert_LPE);
    _observer->unblock();
}

/** Store the changes to the path in XML. */
void PathManipulator::writeXML()
{
    if (!_live_outline)
        _updateOutline();

    _setGeometry();
    if (!_path) {
        return;
    }

    XML::Node *node = _getXMLNode();
    if (!node) {
        return;
    }

    _observer->block();
    if (!empty()) {
        _path->updateRepr();
        node->setAttribute(_nodetypesKey(), _createTypeString());
    } else {
        // this manipulator will have to be destroyed right after this call
        node->removeObserver(*_observer);
        _path->deleteObject(true, true);
        _path = nullptr;
    }
    _observer->unblock();
}

/** Remove all nodes from the path. */
void PathManipulator::clear()
{
    // no longer necessary since nodes remove themselves from selection on destruction
    //_removeNodesFromSelection();
    _subpaths.clear();
}

/** Select all nodes in subpaths that have something selected. */
void PathManipulator::selectSubpaths()
{
    for (auto & _subpath : _subpaths) {
        NodeList::iterator sp_start = _subpath->begin(), sp_end = _subpath->end();
        for (NodeList::iterator j = sp_start; j != sp_end; ++j) {
            if (j->selected()) {
                // if at least one of the nodes from this subpath is selected,
                // select all nodes from this subpath
                for (NodeList::iterator ins = sp_start; ins != sp_end; ++ins)
                    _selection.insert(ins.ptr());
                continue;
            }
        }
    }
}

/** Invert selection in the selected subpaths. */
void PathManipulator::invertSelectionInSubpaths()
{
    for (auto & _subpath : _subpaths) {
        for (NodeList::iterator j = _subpath->begin(); j != _subpath->end(); ++j) {
            if (j->selected()) {
                // found selected node - invert selection in this subpath
                for (NodeList::iterator k = _subpath->begin(); k != _subpath->end(); ++k) {
                    if (k->selected()) _selection.erase(k.ptr());
                    else _selection.insert(k.ptr());
                }
                // next subpath
                break;
            }
        }
    }
}

/** Insert a new node in the middle of each selected segment. */
void PathManipulator::insertNodes()
{
    if (_selection.size() < 2) return;

    for (auto & _subpath : _subpaths) {
        for (NodeList::iterator j = _subpath->begin(); j != _subpath->end(); ++j) {
            NodeList::iterator k = j.next();
            if (k && j->selected() && k->selected()) {
                j = subdivideSegment(j, 0.5);
                _selection.insert(j.ptr());
            }
        }
    }
}

void PathManipulator::insertNode(Geom::Point pt)
{
    Geom::Coord dist = _updateDragPoint(pt);
    if (dist < 1e-5) { // 1e-6 is too small, as observed occasionally when inserting a node at a snapped intersection of paths
        insertNode(_dragpoint->getIterator(), _dragpoint->getTimeValue(), true);
    }
}

void PathManipulator::insertNode(NodeList::iterator first, double t, bool take_selection)
{
    NodeList::iterator inserted = subdivideSegment(first, t);
    if (take_selection) {
        _selection.clear();
    }
    _selection.insert(inserted.ptr());

    update(true);
    _commit(_("Add node"));
}

static void
add_or_replace_if_extremum(std::vector< std::pair<NodeList::iterator, double> > &vec,
                           double & extrvalue, double testvalue, NodeList::iterator const& node, double t)
{
    if (testvalue > extrvalue) {
        // replace all extreme nodes with the new one
        vec.clear();
        vec.emplace_back( node, t );
        extrvalue = testvalue;
    } else if ( Geom::are_near(testvalue, extrvalue) ) {
        // very rare but: extremum node at the same extreme value!!! so add it to the list
        vec.emplace_back( node, t );
    }
}

/** Insert a new node at the extremum of the selected segments. */
void PathManipulator::insertNodeAtExtremum(ExtremumType extremum)
{
    if (_selection.size() < 2) return;

    double sign    = (extremum == EXTR_MIN_X || extremum == EXTR_MIN_Y) ? -1. : 1.;
    Geom::Dim2 dim = (extremum == EXTR_MIN_X || extremum == EXTR_MAX_X) ? Geom::X : Geom::Y;

    for (auto & _subpath : _subpaths) {
        Geom::Coord extrvalue = - Geom::infinity();
        std::vector< std::pair<NodeList::iterator, double> > extremum_vector;

        for (NodeList::iterator first = _subpath->begin(); first != _subpath->end(); ++first) {
            NodeList::iterator second = first.next();
            if (second && first->selected() && second->selected()) {
                add_or_replace_if_extremum(extremum_vector, extrvalue, sign * first->position()[dim], first, 0.);
                add_or_replace_if_extremum(extremum_vector, extrvalue, sign * second->position()[dim], first, 1.);
                if (first->front()->isDegenerate() && second->back()->isDegenerate()) {
                    // a line segment has is extrema at the start and end, no node should be added
                    continue;
                } else {
                    // build 1D cubic bezier curve
                    Geom::Bezier temp1d(first->position()[dim], first->front()->position()[dim],
                                        second->back()->position()[dim], second->position()[dim]);
                    // and determine extremum
                    Geom::Bezier deriv1d = derivative(temp1d);
                    std::vector<double> rs = deriv1d.roots();
                    for (double & r : rs) {
                        add_or_replace_if_extremum(extremum_vector, extrvalue, sign * temp1d.valueAt(r), first, r);
                    }
                }
            }
        }

        for (auto & i : extremum_vector) {
            // don't insert node at the start or end of a segment, i.e. round values for extr_t
            double t = i.second;
            if ( !Geom::are_near(t - std::floor(t+0.5),0.) )  //  std::floor(t+0.5) is another way of writing round(t)
            {
                _selection.insert( subdivideSegment(i.first, t).ptr() );
            }
        }
    }
}

/** Insert new nodes exactly at the positions of selected nodes while preserving shape.
 * This is equivalent to breaking, except that it doesn't split into subpaths. */
void PathManipulator::duplicateNodes()
{
    if (_selection.empty()) return;

    for (auto & _subpath : _subpaths) {
        for (NodeList::iterator j = _subpath->begin(); j != _subpath->end(); ++j) {
            if (j->selected()) {
                NodeList::iterator k = j.next();
                Node *n = new Node(_multi_path_manipulator._path_data.node_data, j->position());

                if (k) {
                    // Move the new node to the bottom of the Z-order. This way you can drag all
                    // nodes that were selected before this operation without deselecting
                    // everything because there is a new node above.
                    n->sink();
                }

                n->front()->setPosition(j->front()->position());
                j->front()->retract();
                j->setType(NODE_CUSP, false);
                _subpath->insert(k, n);

                if (k) {
                    // We need to manually call the selection change callback to refresh
                    // the handle display correctly.
                    // This call changes num_selected, but we call this once for a selected node
                    // and once for an unselected node, so in the end the number stays correct.
                    _selectionChanged(j.ptr(), true);
                    _selectionChanged(n, false);
                } else {
                    // select the new end node instead of the node just before it
                    _selection.erase(j.ptr());
                    _selection.insert(n);
                    break; // this was the end node, nothing more to do
                }
            }
        }
    }
}

/**
  * Copy the selected nodes using the PathBuilder
  *
  * @param builder[out] Selected nodes will be appended to this Path builder
  *                     in pixel coordinates with all transforms applied.
  */
void PathManipulator::copySelectedPath(Geom::PathBuilder *builder)
{
    // Ignore LivePathEffect paths
    if (!_path || cast<LivePathEffectObject>(_path))
        return;
    // Rebuild the selected parts of each subpath
    for (auto &subpath : _subpaths) {
        Node *prev = nullptr;
        bool is_last_node = false;
        for (auto &node : *subpath) {
            if (node.selected()) {
                // The node positions are already transformed
                if (!builder->inPath() || !prev) {
                    builder->moveTo(node.position());
                } else {
                    build_segment(*builder, prev, &node);
                }
                prev = &node;
                is_last_node = true;
            } else {
                is_last_node = false;
            }
        }

        // Complete the path, especially for closed sub paths where the last node is selected
        if (subpath->closed() && is_last_node) {
            if (!prev->front()->isDegenerate() || !subpath->begin()->back()->isDegenerate())   {
                build_segment(*builder, prev, subpath->begin().ptr());
            }
            // if that segment is linear, we just call closePath().
            builder->closePath();
        }
    }
    builder->flush();
}

/** Replace contiguous selections of nodes in each subpath with one node. */
void PathManipulator::weldNodes(NodeList::iterator preserve_pos)
{
    if (_selection.size() < 2) return;
    hideDragPoint();

    bool pos_valid = preserve_pos;
    for (auto sp : _subpaths) {
        unsigned num_selected = 0, num_unselected = 0;
        for (auto & j : *sp) {
            if (j.selected()) ++num_selected;
            else ++num_unselected;
        }
        if (num_selected < 2) continue;
        if (num_unselected == 0) {
            // if all nodes in a subpath are selected, the operation doesn't make much sense
            continue;
        }

        // Start from unselected node in closed paths, so that we don't start in the middle
        // of a selection
        NodeList::iterator sel_beg = sp->begin(), sel_end;
        if (sp->closed()) {
            while (sel_beg->selected()) ++sel_beg;
        }

        // Work loop
        while (num_selected > 0) {
            // Find selected node
            while (sel_beg && !sel_beg->selected()) sel_beg = sel_beg.next();
            if (!sel_beg) throw std::logic_error("Join nodes: end of open path reached, "
                "but there are still nodes to process!");

            // note: this is initialized to zero, because the loop below counts sel_beg as well
            // the loop conditions are simpler that way
            unsigned num_points = 0;
            bool use_pos = false;
            Geom::Point back_pos, front_pos;
            back_pos = sel_beg->back()->position();

            for (sel_end = sel_beg; sel_end && sel_end->selected(); sel_end = sel_end.next()) {
                ++num_points;
                front_pos = sel_end->front()->position();
                if (pos_valid && sel_end == preserve_pos) use_pos = true;
            }
            if (num_points > 1) {
                Geom::Point joined_pos;
                if (use_pos) {
                    joined_pos = preserve_pos->position();
                    pos_valid = false;
                } else {
                    joined_pos = Geom::middle_point(back_pos, front_pos);
                }
                sel_beg->setType(NODE_CUSP, false);
                sel_beg->move(joined_pos);
                // do not move handles if they aren't degenerate
                if (!sel_beg->back()->isDegenerate()) {
                    sel_beg->back()->setPosition(back_pos);
                }
                if (!sel_end.prev()->front()->isDegenerate()) {
                    sel_beg->front()->setPosition(front_pos);
                }
                sel_beg = sel_beg.next();
                while (sel_beg != sel_end) {
                    NodeList::iterator next = sel_beg.next();
                    sp->erase(sel_beg);
                    sel_beg = next;
                    --num_selected;
                }
            }
            --num_selected; // for the joined node or single selected node
        }
    }
}

/** Remove nodes in the middle of selected segments. */
void PathManipulator::weldSegments()
{
    if (_selection.size() < 2) return;
    hideDragPoint();

    for (auto sp : _subpaths) {
        unsigned num_selected = 0, num_unselected = 0;
        for (auto & j : *sp) {
            if (j.selected()) ++num_selected;
            else ++num_unselected;
        }

        // if 2 or fewer nodes are selected, there can't be any middle points to remove.
        if (num_selected <= 2) continue;

        if (num_unselected == 0 && sp->closed()) {
            // if all nodes in a closed subpath are selected, the operation doesn't make much sense
            continue;
        }

        // Start from unselected node in closed paths, so that we don't start in the middle
        // of a selection
        NodeList::iterator sel_beg = sp->begin(), sel_end;
        if (sp->closed()) {
            while (sel_beg->selected()) ++sel_beg;
        }

        // Work loop
        while (num_selected > 0) {
            // Find selected node
            while (sel_beg && !sel_beg->selected()) sel_beg = sel_beg.next();
            if (!sel_beg) throw std::logic_error("Join nodes: end of open path reached, "
                "but there are still nodes to process!");

            // note: this is initialized to zero, because the loop below counts sel_beg as well
            // the loop conditions are simpler that way
            unsigned num_points = 0;

            // find the end of selected segment
            for (sel_end = sel_beg; sel_end && sel_end->selected(); sel_end = sel_end.next()) {
                ++num_points;
            }
            if (num_points > 2) {
                // remove nodes in the middle
                // TODO: fit bezier to the former shape
                sel_beg = sel_beg.next();
                while (sel_beg != sel_end.prev()) {
                    NodeList::iterator next = sel_beg.next();
                    sp->erase(sel_beg);
                    sel_beg = next;
                }
            }
            sel_beg = sel_end;
            // decrease num_selected by the number of points processed
            num_selected -= num_points;
        }
    }
}

/** Break the subpath at selected nodes. It also works for single node closed paths. */
void PathManipulator::breakNodes(bool new_nodes)
{
    for (SubpathList::iterator i = _subpaths.begin(); i != _subpaths.end(); ++i) {
        SubpathPtr sp = *i;
        NodeList::iterator cur = sp->begin(), end = sp->end();
        if (!sp->closed()) {
            // Each open path must have at least two nodes so no checks are required.
            // For 2-node open paths, cur == end
            ++cur;
            --end;
        }
        for (; cur != end; ++cur) {
            if (!cur->selected()) continue;
            SubpathPtr ins;
            bool becomes_open = false;

            if (sp->closed()) {
                // Move the node to break at to the beginning of path
                if (cur != sp->begin())
                    sp->splice(sp->begin(), *sp, cur, sp->end());
                sp->setClosed(false);
                ins = sp;
                becomes_open = true;
            } else {
                SubpathPtr new_sp(new NodeList(_subpaths));
                new_sp->splice(new_sp->end(), *sp, sp->begin(), cur);
                _subpaths.insert(i, new_sp);
                ins = new_sp;
            }

            // When cutting nodes, we don't want to make new nodes, they'd just be deleted anyway
            if (new_nodes) {
                Node *n = new Node(_multi_path_manipulator._path_data.node_data, cur->position());
                ins->insert(ins->end(), n);
                cur->setType(NODE_CUSP, false);
                n->back()->setRelativePos(cur->back()->relativePos());
                cur->back()->retract();
                n->sink();
            } else {
                cur->setType(NODE_CUSP, false);
                cur->back()->retract();
            }

            if (becomes_open) {
                cur = sp->begin(); // this will be increased to ++sp->begin()
                end = --sp->end();
            }
        }
    }
}

/** Delete selected nodes in the path, optionally substituting deleted segments with bezier curves
 * in a way that attempts to preserve the original shape of the curve. */
void PathManipulator::deleteNodes(NodeDeleteMode delete_mode)
{
    if (_selection.empty()) return;
    hideDragPoint();

    if (delete_mode == NodeDeleteMode::gap_nodes) {
        // Break nodes first to create gaps
        breakNodes(false);
    } else if (delete_mode == NodeDeleteMode::gap_lines) {
        // Delete nodes but preserve end points
        _deleteSegments(true);
        return;
    }

    for (SubpathList::iterator i = _subpaths.begin(); i != _subpaths.end();) {
        SubpathPtr sp = *i;

        // If there are less than 2 unselected nodes in an open subpath or no unselected nodes
        // in a closed one, delete entire subpath.
        unsigned num_unselected = 0, num_selected = 0;
        for (auto & j : *sp) {
            if (j.selected()) ++num_selected;
            else ++num_unselected;
        }
        if (num_selected == 0) {
            ++i;
            continue;
        }
        if (sp->closed() ? (num_unselected < 1) : (num_unselected < 2)) {
            _subpaths.erase(i++);
            continue;
        }

        // In closed paths, start from an unselected node - otherwise we might start in the middle
        // of a selected stretch and the resulting bezier fit would be suboptimal
        NodeList::iterator sel_beg = sp->begin(), sel_end;
        if (sp->closed()) {
            while (sel_beg->selected()) ++sel_beg;
        }
        sel_end = sel_beg;
        
        while (num_selected > 0) {
            while (sel_beg && !sel_beg->selected()) {
                sel_beg = sel_beg.next();
            }
            sel_end = sel_beg;

            while (sel_end && sel_end->selected()) {
                sel_end = sel_end.next();
            }
            
            num_selected -= _deleteStretch(sel_beg, sel_end, delete_mode);
            sel_beg = sel_end;
        }
        ++i;
    }
}

double get_angle(const Geom::Point& p0, const Geom::Point& p1, const Geom::Point& p2) {
    auto d1 = p1 - p0;
    auto d2 = p1 - p2;
    if (d1.isZero() || d2.isZero()) return M_PI;

    auto a1 = atan2(d1);
    auto a2 = atan2(d2);
    return a1 - a2;
}

/**
 * Delete nodes between the two iterators.
 * The given range can cross the beginning of the subpath in closed subpaths.
 * @param start      Beginning of the range to delete
 * @param end        End of the range
 * @param mode       Various delete methods, see NodeDeleteMode enum
 *
 * @return Number of deleted nodes
 */
unsigned PathManipulator::_deleteStretch(NodeList::iterator start, NodeList::iterator end, NodeDeleteMode mode)
{
    unsigned const samples_per_segment = 10;
    double const t_step = 1.0 / samples_per_segment;

    unsigned del_len = 0;
    for (NodeList::iterator i = start; i != end; i = i.next()) {
        ++del_len;
    }
    if (del_len == 0) return 0;

    bool keep_shape = mode == NodeDeleteMode::automatic || mode == NodeDeleteMode::curve_fit;

    if ((mode == NodeDeleteMode::automatic || mode == NodeDeleteMode::inverse_auto) && start.prev() && end) {
        auto angle_flat = Inkscape::Preferences::get()->getDoubleLimited("/tools/node/flat-cusp-angle", 135, 1, 180);
        for (NodeList::iterator cur = start; cur != end; cur = cur.next()) {
            auto back =  cur->back() ->isDegenerate() ? cur.prev()->position() : cur->back() ->position();
            auto front = cur->front()->isDegenerate() ? cur.next()->position() : cur->front()->position();
            auto angle = get_angle(back, cur->position(), front);
            auto a = fmod(fabs(angle), 2*M_PI);
            auto diff = fabs(a - M_PI);
            auto tolerance = (180 - angle_flat) * M_PI / 180;
            bool flat = diff < tolerance; // flat if *somewhat* close to 180 degrees (+-tolerance)
            if (!flat && Geom::distance(back, front) > 1) {
                // detected a cusp, so we'll try to remove nodes and insert line segment, rather than fitting a curve
                // if in auto mode, or the opposite in inverse_auto
                keep_shape = !keep_shape;
                break;
            }
        }
    }

    // set surrounding node types to cusp if:
    // we are deleting at the end or beginning of an open path
    if (!end && start.prev()) {
        auto p = start.prev();
        p->setType(NODE_CUSP, false);
        p->front()->retract();
    }
    if (!start.prev() && end) {
        end->setType(NODE_CUSP, false);
        end->back()->retract();
    }

    if (keep_shape && start.prev() && end) {
        std::vector<InputPoint> input;
        Geom::Point result[4];
        Geom::LineSegment s;
        unsigned seg = 0;

        for (NodeList::iterator cur = start.prev(); cur != end; cur = cur.next()) {
            auto bc = Geom::CubicBezier(cur->position(), cur->front()->position(), cur.next()->back()->position(), cur.next()->position());
            for (unsigned s = 0; s < samples_per_segment; ++s) {
                auto t = t_step * s;
                input.emplace_back(bc.pointAt(t), t);
            }
            ++seg;
        }
        // Fill last point
        // last point + its slope
        input.emplace_back(end->position(), Geom::Point(), end->back()->position(), 1.0);

        // get slope for the first point
        input.front() = InputPoint(start.prev()->position(), start.prev()->front()->position(), Geom::Point(), 0.0);

        // Compute replacement bezier curve
        bezier_fit(result, input);

        start.prev()->front()->setPosition(result[1]);
        end->back()->setPosition(result[2]);
    }

    // We can't use nl->erase(start, end), because it would break when the stretch
    // crosses the beginning of a closed subpath
    NodeList &nl = start->nodeList();
    while (start != end) {
        NodeList::iterator next = start.next();
        nl.erase(start);
        start = next;
    }
    // if we are removing, we readjust the handlers
    if (!keep_shape && _isBSpline()){
        if(start.prev()){
            double bspline_weight = _bsplineHandlePosition(start.prev()->back(), false);
            start.prev()->front()->setPosition(_bsplineHandleReposition(start.prev()->front(), bspline_weight));
        }
        if(end){
            double bspline_weight = _bsplineHandlePosition(end->front(), false);
            end->back()->setPosition(_bsplineHandleReposition(end->back(),bspline_weight));
        }
    } else if (mode == NodeDeleteMode::line_segment) {
        // Handle line straigtening
        if (start.prev()) {
            start.prev()->setType(NodeType::NODE_CUSP);
            start.prev()->front()->move(start.prev()->position());
        }
        if (end) {
            end->setType(NodeType::NODE_CUSP);
            end->back()->move(end->position());
        }
    }

    return del_len;
}

void PathManipulator::deleteSegments()
{
    _deleteSegments(false);
}

/** Removes selected segments */
void PathManipulator::_deleteSegments(bool delete_singles)
{
    if (_selection.empty()) return;
    hideDragPoint();

    for (SubpathList::iterator i = _subpaths.begin(); i != _subpaths.end();) {
        SubpathPtr sp = *i;
        bool has_unselected = false;
        unsigned num_selected = 0;
        for (auto & j : *sp) {
            if (j.selected()) {
                ++num_selected;
            } else {
                has_unselected = true;
            }
        }
        if (!has_unselected) {
            _subpaths.erase(i++);
            continue;
        }

        NodeList::iterator sel_beg = sp->begin();
        if (sp->closed()) {
            while (sel_beg && sel_beg->selected()) ++sel_beg;
        }
        while (num_selected > 0) {
            if (!sel_beg->selected()) {
                sel_beg = sel_beg.next();
                continue;
            }
            NodeList::iterator sel_end = sel_beg;
            unsigned num_points = 0;
            while (sel_end && sel_end->selected()) {
                sel_end = sel_end.next();
                ++num_points;
            }
            if (num_points >= (2 - delete_singles)) {
                // Retract end handles
                sel_end.prev()->setType(NODE_CUSP, false);
                sel_end.prev()->back()->retract();
                sel_beg->setType(NODE_CUSP, false);
                sel_beg->front()->retract();
                if (sp->closed()) {
                    // In closed paths, relocate the beginning of the path to the last selected
                    // node and then unclose it. Remove the nodes from the first selected node
                    // to the new end of path.
                    if (sel_end.prev() != sp->begin())
                        sp->splice(sp->begin(), *sp, sel_end.prev(), sp->end());
                    sp->setClosed(false);
                    sp->erase(sel_beg.next(), sp->end());
                } else {
                    // for open paths:
                    // 1. At end or beginning, delete including the node on the end or beginning
                    // 2. In the middle, delete only inner nodes
                    if (sel_beg == sp->begin()) {
                        sp->erase(sp->begin(), sel_end.prev());
                    } else if (sel_end == sp->end()) {
                        sp->erase(sel_beg.next(), sp->end());
                    } else {
                        SubpathPtr new_sp(new NodeList(_subpaths));
                        new_sp->splice(new_sp->end(), *sp, sp->begin(), sel_beg.next());
                        _subpaths.insert(i, new_sp);
                        if (sel_end.prev())
                            sp->erase(sp->begin(), sel_end.prev());
                    }
                }
            }
            sel_beg = sel_end;
            num_selected -= num_points;
        }
        ++i;
    }
}

/** Reverse subpaths of the path.
 * @param selected_only If true, only paths that have at least one selected node
 *                      will be reversed. Otherwise all subpaths will be reversed. */
void PathManipulator::reverseSubpaths(bool selected_only)
{
    for (auto & _subpath : _subpaths) {
        if (selected_only) {
            for (NodeList::iterator j = _subpath->begin(); j != _subpath->end(); ++j) {
                if (j->selected()) {
                    _subpath->reverse();
                    break; // continue with the next subpath
                }
            }
        } else {
            _subpath->reverse();
        }
    }
}

/** Make selected segments curves / lines. */
void PathManipulator::setSegmentType(SegmentType type)
{
    if (_selection.empty()) return;
    for (auto & _subpath : _subpaths) {
        for (NodeList::iterator j = _subpath->begin(); j != _subpath->end(); ++j) {
            NodeList::iterator k = j.next();
            if (!(k && j->selected() && k->selected())) continue;
            switch (type) {
            case SEGMENT_STRAIGHT:
                if (j->front()->isDegenerate() && k->back()->isDegenerate())
                    break;
                j->front()->move(j->position());
                k->back()->move(k->position());
                break;
            case SEGMENT_CUBIC_BEZIER:
                if (!j->front()->isDegenerate() || !k->back()->isDegenerate())
                    break;
                // move both handles to 1/3 of the line
                j->front()->move(j->position() + (k->position() - j->position()) / 3);
                k->back()->move(k->position() + (j->position() - k->position()) / 3);
                break;
            }
        }
    }
}

void PathManipulator::scaleHandle(Node *n, int which, int dir, bool pixel)
{
    if (n->type() == NODE_SYMMETRIC || n->type() == NODE_AUTO) {
        n->setType(NODE_SMOOTH);
    }
    Handle *h = _chooseHandle(n, which);
    double length_change;

    if (pixel) {
        length_change = 1.0 / _desktop->current_zoom() * dir;
    } else {
        Inkscape::Preferences *prefs = Inkscape::Preferences::get();
        length_change = prefs->getDoubleLimited("/options/defaultscale/value", 2, 1, 1000, "px");
        length_change *= dir;
    }

    Geom::Point relpos;
    if (h->isDegenerate()) {
        if (dir < 0) return;
        Node *nh = n->nodeToward(h);
        if (!nh) return;
        relpos = Geom::unit_vector(nh->position() - n->position()) * length_change;
    } else {
        relpos = h->relativePos();
        double rellen = relpos.length();
        relpos *= ((rellen + length_change) / rellen);
    }
    h->setRelativePos(relpos);
    update();
    gchar const *key = which < 0 ? "handle:scale:left" : "handle:scale:right";
    _commit(_("Scale handle"), key);
}

void PathManipulator::rotateHandle(Node *n, int which, int dir, bool pixel)
{
    if (n->type() != NODE_CUSP) {
        n->setType(NODE_CUSP);
    }
    Handle *h = _chooseHandle(n, which);
    if (h->isDegenerate()) return;

    double angle;
    if (pixel) {
        // Rotate by "one pixel"
        angle = atan2(1.0 / _desktop->current_zoom(), h->length()) * dir;
    } else {
        Inkscape::Preferences *prefs = Inkscape::Preferences::get();
        int snaps = prefs->getIntLimited("/options/rotationsnapsperpi/value", 12, 1, 1000);
        angle = M_PI * dir / snaps;
    }

    h->setRelativePos(h->relativePos() * Geom::Rotate(angle));
    update();
    gchar const *key = which < 0 ? "handle:rotate:left" : "handle:rotate:right";
    _commit(_("Rotate handle"), key);
}

Handle *PathManipulator::_chooseHandle(Node *n, int which)
{
    NodeList::iterator i = NodeList::get_iterator(n);
    Node *prev = i.prev().ptr();
    Node *next = i.next().ptr();

    // on an endnode, the remaining handle automatically wins
    if (!next) return n->back();
    if (!prev) return n->front();

    // compare X coord offline segments
    Geom::Point npos = next->position();
    Geom::Point ppos = prev->position();
    if (which < 0) {
        // pick left handle.
        // we just swap the handles and pick the right handle below.
        std::swap(npos, ppos);
    }

    if (npos[Geom::X] >= ppos[Geom::X]) {
        return n->front();
    } else {
        return n->back();
    }
}

/** Set the visibility of handles. */
void PathManipulator::showHandles(bool show)
{
    if (show == _show_handles) return;
    if (show) {
        for (auto & _subpath : _subpaths) {
            for (NodeList::iterator j = _subpath->begin(); j != _subpath->end(); ++j) {
                if (!j->selected()) continue;
                j->showHandles(true);
                if (j.prev()) j.prev()->showHandles(true);
                if (j.next()) j.next()->showHandles(true);
            }
        }
    } else {
        for (auto & _subpath : _subpaths) {
            for (auto & j : *_subpath) {
                j.showHandles(false);
            }
        }
    }
    _show_handles = show;
}

/** Set the visibility of outline. */
void PathManipulator::showOutline(bool show)
{
    if (show == _show_outline) return;
    _show_outline = show;
    _updateOutline();
}

void PathManipulator::showPathDirection(bool show)
{
    if (show == _show_path_direction) return;
    _show_path_direction = show;
    _updateOutline();
}

void PathManipulator::setLiveOutline(bool set)
{
    _live_outline = set;
}

void PathManipulator::setLiveObjects(bool set)
{
    _live_objects = set;
}

void PathManipulator::updateHandles()
{
    for (auto & _subpath : _subpaths) {
        for (auto & j : *_subpath) {
            j.updateHandles();
        }
    }
}

void PathManipulator::setControlsTransform(Geom::Affine const &tnew)
{
    Geom::Affine delta = _i2d_transform.inverse() * _edit_transform.inverse() * tnew * _i2d_transform;
    _edit_transform = tnew;
    for (auto & _subpath : _subpaths) {
        for (auto & j : *_subpath) {
            j.transform(delta);
        }
    }
    _createGeometryFromControlPoints();
}

/** Hide the curve drag point until the next motion event.
 * This should be called at the beginning of every method that can delete nodes.
 * Otherwise the invalidated iterator in the dragpoint can cause crashes. */
void PathManipulator::hideDragPoint()
{
    _dragpoint->setVisible(false);
    _dragpoint->setIterator(NodeList::iterator());
}

/** Insert a node in the segment beginning with the supplied iterator,
 * at the given time value */
NodeList::iterator PathManipulator::subdivideSegment(NodeList::iterator first, double t)
{
    if (!first) throw std::invalid_argument("Subdivide after invalid iterator");
    NodeList &list = NodeList::get(first);
    NodeList::iterator second = first.next();
    if (!second) throw std::invalid_argument("Subdivide after last node in open path");
    if (first->type() == NODE_SYMMETRIC)
        first->setType(NODE_SMOOTH, false);
    if (second->type() == NODE_SYMMETRIC)
        second->setType(NODE_SMOOTH, false);

    // We need to insert the segment after 'first'. We can't simply use 'second'
    // as the point of insertion, because when 'first' is the last node of closed path,
    // the new node will be inserted as the first node instead.
    NodeList::iterator insert_at = first;
    ++insert_at;

    NodeList::iterator inserted;
    if (first->front()->isDegenerate() && second->back()->isDegenerate()) {
        // for a line segment, insert a cusp node
        Node *n = new Node(_multi_path_manipulator._path_data.node_data,
            Geom::lerp(t, first->position(), second->position()));
        n->setType(NODE_CUSP, false);
        inserted = list.insert(insert_at, n);
    } else {
        // build bezier curve and subdivide
        Geom::CubicBezier temp(first->position(), first->front()->position(),
            second->back()->position(), second->position());
        std::pair<Geom::CubicBezier, Geom::CubicBezier> div = temp.subdivide(t);
        std::vector<Geom::Point> seg1 = div.first.controlPoints(), seg2 = div.second.controlPoints();

        // set new handle positions
        Node *n = new Node(_multi_path_manipulator._path_data.node_data, seg2[0]);
        if(!_isBSpline()){
            n->back()->setPosition(seg1[2]);
            n->front()->setPosition(seg2[1]);
            n->setType(NODE_SMOOTH, false);
        } else {
            Geom::D2< Geom::SBasis > sbasis_inside_nodes;
            SPCurve line_inside_nodes;
            if(second->back()->isDegenerate()){
                line_inside_nodes.moveto(n->position());
                line_inside_nodes.lineto(second->position());
                sbasis_inside_nodes = line_inside_nodes.first_segment()->toSBasis();
                Geom::Point next = sbasis_inside_nodes.valueAt(DEFAULT_START_POWER);
                line_inside_nodes.reset();
                n->front()->setPosition(next);
            }else{
                n->front()->setPosition(seg2[1]);
            }
            if(first->front()->isDegenerate()){
                line_inside_nodes.moveto(n->position());
                line_inside_nodes.lineto(first->position());
                sbasis_inside_nodes = line_inside_nodes.first_segment()->toSBasis();
                Geom::Point previous = sbasis_inside_nodes.valueAt(DEFAULT_START_POWER);
                n->back()->setPosition(previous);
            }else{
                n->back()->setPosition(seg1[2]);
            }
            n->setType(NODE_CUSP, false);
        }
        inserted = list.insert(insert_at, n);

        first->front()->move(seg1[1]);
        second->back()->move(seg2[2]);
    }
    return inserted;
}

/** Find the node that is closest/farthest from the origin
 * @param origin Point of reference
 * @param search_selected Consider selected nodes
 * @param search_unselected Consider unselected nodes
 * @param closest If true, return closest node, if false, return farthest
 * @return The matching node, or an empty iterator if none found
 */
NodeList::iterator PathManipulator::extremeNode(NodeList::iterator origin, bool search_selected,
    bool search_unselected, bool closest)
{
    NodeList::iterator match;
    double extr_dist = closest ? HUGE_VAL : -HUGE_VAL;
    if (_selection.empty() && !search_unselected) return match;

    for (auto & _subpath : _subpaths) {
        for (NodeList::iterator j = _subpath->begin(); j != _subpath->end(); ++j) {
            if(j->selected()) {
                if (!search_selected) continue;
            } else {
                if (!search_unselected) continue;
            }
            double dist = Geom::distance(j->position(), origin->position());
            bool cond = closest ? (dist < extr_dist) : (dist > extr_dist);
            if (cond) {
                match = j;
                extr_dist = dist;
            }
        }
    }
    return match;
}

/* Called when a process updates the path in-situe */
void PathManipulator::updatePath()
{
    _externalChange(PATH_CHANGE_D);
}

/** Called by the XML observer when something else than us modifies the path. */
void PathManipulator::_externalChange(unsigned type)
{
    hideDragPoint();

    switch (type) {
    case PATH_CHANGE_D: {
        _getGeometry();

        // ugly: stored offsets of selected nodes in a vector
        // vector<bool> should be specialized so that it takes only 1 bit per value
        std::vector<bool> selpos;
        for (auto & _subpath : _subpaths) {
            for (auto & j : *_subpath) {
                selpos.push_back(j.selected());
            }
        }
        unsigned size = selpos.size(), curpos = 0;

        _createControlPointsFromGeometry();

        for (auto & _subpath : _subpaths) {
            for (NodeList::iterator j = _subpath->begin(); j != _subpath->end(); ++j) {
                if (curpos >= size) goto end_restore;
                if (selpos[curpos]) _selection.insert(j.ptr());
                ++curpos;
            }
        }
        end_restore:

        _updateOutline();
        } break;
    case PATH_CHANGE_TRANSFORM: {
        auto path = cast<SPPath>(_path);
        if (path) {
            Geom::Affine i2d_change = _d2i_transform;
            _i2d_transform = path->i2dt_affine();
            _d2i_transform = _i2d_transform.inverse();
            i2d_change *= _i2d_transform;
            for (auto & _subpath : _subpaths) {
                for (auto & j : *_subpath) {
                    j.transform(i2d_change);
                }
            }
            _updateOutline();
        }
        } break;
    default: break;
    }
}

Geom::Affine PathManipulator::_getTransform() const
{
    return _i2d_transform * _edit_transform;
}

/** Create nodes and handles based on the XML of the edited path. */
void PathManipulator::_createControlPointsFromGeometry()
{
    clear();

    // sanitize pathvector and store it in SPCurve,
    // so that _updateDragPoint doesn't crash on paths with naked movetos
    Geom::PathVector pathv;
    if (_is_bspline) {
        pathv = pathv_to_cubicbezier(_spcurve.get_pathvector(), false);
    } else {
        pathv = pathv_to_linear_and_cubic_beziers(_spcurve.get_pathvector());
    }
    for (Geom::PathVector::iterator i = pathv.begin(); i != pathv.end(); ) {
        // NOTE: this utilizes the fact that Geom::PathVector is an std::vector.
        // When we erase an element, the next one slides into position,
        // so we do not increment the iterator even though it is theoretically invalidated.
        if (i->empty()) {
            i = pathv.erase(i);
        } else {
            ++i;
        }
    }
    if (pathv.empty()) {
        return;
    }
    _spcurve = SPCurve(pathv);

    pathv *= _getTransform();

    // in this loop, we know that there are no zero-segment subpaths
    for (auto & pit : pathv) {
        // prepare new subpath
        SubpathPtr subpath(new NodeList(_subpaths));
        _subpaths.push_back(subpath);

        Node *previous_node = new Node(_multi_path_manipulator._path_data.node_data, pit.initialPoint());
        subpath->push_back(previous_node);

        bool closed = pit.closed();

        for (Geom::Path::iterator cit = pit.begin(); cit != pit.end(); ++cit) {
            Geom::Point pos = cit->finalPoint();
            Node *current_node;
            // if the closing segment is degenerate and the path is closed, we need to move
            // the handle of the first node instead of creating a new one
            if (closed && cit == --(pit.end())) {
                current_node = subpath->begin().get_pointer();
            } else {
                /* regardless of segment type, create a new node at the end
                 * of this segment (unless this is the last segment of a closed path
                 * with a degenerate closing segment */
                current_node = new Node(_multi_path_manipulator._path_data.node_data, pos);
                subpath->push_back(current_node);
            }
            // if this is a bezier segment, move handles appropriately
            // TODO: I don't know why the dynamic cast below doesn't want to work
            //       when I replace BezierCurve with CubicBezier. Might be a bug
            //       somewhere in pathv_to_linear_and_cubic_beziers
            Geom::BezierCurve const *bezier = dynamic_cast<Geom::BezierCurve const*>(&*cit);
            if (bezier && bezier->order() == 3)
            {
                previous_node->front()->setPosition((*bezier)[1]);
                current_node ->back() ->setPosition((*bezier)[2]);
            }
            previous_node = current_node;
        }
        // If the path is closed, make the list cyclic
        if (pit.closed()) subpath->setClosed(true);
    }

    // we need to set the nodetypes after all the handles are in place,
    // so that pickBestType works correctly
    // TODO maybe migrate to inkscape:node-types?
    // TODO move this into SPPath - do not manipulate directly

    //XML Tree being used here directly while it shouldn't be.
    gchar const *nts_raw = _path ? _path->getRepr()->attribute(_nodetypesKey().data()) : nullptr;
    /* Calculate the needed length of the nodetype string.
     * For closed paths, the entry is duplicated for the starting node,
     * so we can just use the count of segments including the closing one
     * to include the extra end node. */
    /* pad the string to required length with a bogus value.
     * 'b' and any other letter not recognized by the parser causes the best fit to be set
     * as the node type */
    auto const *tsi = nts_raw ? nts_raw : "";
    for (auto & _subpath : _subpaths) {
        for (auto & j : *_subpath) {
            char nodetype = (*tsi) ? (*tsi++) : 'b';
            j.setType(Node::parse_nodetype(nodetype), false);
        }
        if (_subpath->closed() && *tsi) {
            // STUPIDITY ALERT: it seems we need to use the duplicate type symbol instead of
            // the first one to remain backward compatible.
            _subpath->begin()->setType(Node::parse_nodetype(*tsi++), false);
        }
    }
}

//determines if the trace has a bspline effect and the number of steps that it takes
int PathManipulator::_bsplineGetSteps() const {

    LivePathEffect::LPEBSpline const *lpe_bsp = nullptr;

    auto path = cast<SPLPEItem>(_path);
    if (path){
        if(path->hasPathEffect()){
            Inkscape::LivePathEffect::Effect const *this_effect =
                path->getFirstPathEffectOfType(Inkscape::LivePathEffect::BSPLINE);
            if(this_effect){
                lpe_bsp = dynamic_cast<LivePathEffect::LPEBSpline const*>(this_effect->getLPEObj()->get_lpe());
            }
        }
    }
    int steps = 0;
    if(lpe_bsp){
        steps = lpe_bsp->steps+1;
    }
    return steps;
}

// determines if the trace has bspline effect
void PathManipulator::_recalculateIsBSpline(){
    auto path = cast<SPPath>(_path);
    if (path && path->hasPathEffect()) {
        Inkscape::LivePathEffect::Effect const *this_effect =
            path->getFirstPathEffectOfType(Inkscape::LivePathEffect::BSPLINE);
        if(this_effect){
            _is_bspline = true;
            return;
        }
    }
    _is_bspline = false;
}

bool PathManipulator::_isBSpline() const {
    return  _is_bspline;
}

// returns the corresponding strength to the position of the handlers
double PathManipulator::_bsplineHandlePosition(Handle *h, bool check_other)
{
    using Geom::X;
    using Geom::Y;
    double pos = NO_POWER;
    Node *n = h->parent();
    Node * next_node = nullptr;
    next_node = n->nodeToward(h);
    if(next_node){
        SPCurve line_inside_nodes;
        line_inside_nodes.moveto(n->position());
        line_inside_nodes.lineto(next_node->position());
        if(!are_near(h->position(), n->position())){
            pos = Geom::nearest_time(h->position(), *line_inside_nodes.first_segment());
        }
    }
    if (Geom::are_near(pos, NO_POWER, BSPLINE_TOL) && check_other){
        return _bsplineHandlePosition(h->other(), false);
    }
    return pos;
}

// give the location for the handler in the corresponding position
Geom::Point PathManipulator::_bsplineHandleReposition(Handle *h, bool check_other)
{
    double pos = this->_bsplineHandlePosition(h, check_other);
    return _bsplineHandleReposition(h,pos);
}

// give the location for the handler to the specified position
Geom::Point PathManipulator::_bsplineHandleReposition(Handle *h,double pos){
    using Geom::X;
    using Geom::Y;
    Geom::Point ret = h->position();
    Node *n = h->parent();
    Geom::D2< Geom::SBasis > sbasis_inside_nodes;
    SPCurve line_inside_nodes;
    Node * next_node = nullptr;
    next_node = n->nodeToward(h);
    if(next_node && !Geom::are_near(pos, NO_POWER, BSPLINE_TOL)){
        line_inside_nodes.moveto(n->position());
        line_inside_nodes.lineto(next_node->position());
        sbasis_inside_nodes = line_inside_nodes.first_segment()->toSBasis();
        ret = sbasis_inside_nodes.valueAt(pos);
    } else{
        if(Geom::are_near(pos, NO_POWER, BSPLINE_TOL)){
            ret = n->position();
        }
    }
    return ret;
}

/** Construct the geometric representation of nodes and handles, update the outline
 * and display
 * \param alert_LPE if true, first the LPE is warned what the new path is going to be before updating it
 */
void PathManipulator::_createGeometryFromControlPoints(bool alert_LPE)
{
    Geom::PathBuilder builder;
    //Refresh if is bspline some times -think on path change selection, this value get lost
    _recalculateIsBSpline();
    for (std::list<SubpathPtr>::iterator spi = _subpaths.begin(); spi != _subpaths.end(); ) {
        SubpathPtr subpath = *spi;
        if (subpath->empty()) {
            _subpaths.erase(spi++);
            continue;
        }
        NodeList::iterator prev = subpath->begin();
        builder.moveTo(prev->position());
        for (NodeList::iterator i = ++subpath->begin(); i != subpath->end(); ++i) {
            build_segment(builder, prev.ptr(), i.ptr());
            prev = i;
        }
        if (subpath->closed()) {
            // Here we link the last and first node if the path is closed.
            // If the last segment is Bezier, we add it.
            if (!prev->front()->isDegenerate() || !subpath->begin()->back()->isDegenerate()) {
                build_segment(builder, prev.ptr(), subpath->begin().ptr());
            }
            // if that segment is linear, we just call closePath().
            builder.closePath();
        }
        ++spi;
    }
    builder.flush();
    Geom::PathVector pathv = builder.peek() * _getTransform().inverse();
    for (Geom::PathVector::iterator i = pathv.begin(); i != pathv.end(); ) {
        // NOTE: this utilizes the fact that Geom::PathVector is an std::vector.
        // When we erase an element, the next one slides into position,
        // so we do not increment the iterator even though it is theoretically invalidated.
        if (i->empty()) {
            i = pathv.erase(i);
        } else {
            ++i;
        }
    }
    if (pathv.empty()) {
        return;
    }

    if (_spcurve.get_pathvector() == pathv) {
        return;
    }
    _spcurve = SPCurve(pathv);
    if (alert_LPE) {
        /// \todo note that _path can be an Inkscape::LivePathEffect::Effect* too, kind of confusing, rework member naming?
        auto path = cast<SPPath>(_path);
        if (path && path->hasPathEffect()) {
            Inkscape::LivePathEffect::Effect *this_effect = 
                path->getFirstPathEffectOfType(Inkscape::LivePathEffect::POWERSTROKE);
            LivePathEffect::LPEPowerStroke *lpe_pwr = dynamic_cast<LivePathEffect::LPEPowerStroke*>(this_effect);
            if (lpe_pwr) {
               lpe_pwr->adjustForNewPath();
            }
        }
    }
    if (_live_outline) {
        _updateOutline();
    }
    if (_live_objects) {
        _setGeometry();
    }
}

/** Build one segment of the geometric representation.
 * @relates PathManipulator */
void build_segment(Geom::PathBuilder &builder, Node *prev_node, Node *cur_node)
{
    if (cur_node->back()->isDegenerate() && prev_node->front()->isDegenerate())
    {
        // NOTE: It seems like the renderer cannot correctly handle vline / hline segments,
        // and trying to display a path using them results in funny artifacts.
        builder.lineTo(cur_node->position());
    } else {
        // this is a bezier segment
        builder.curveTo(
            prev_node->front()->position(),
            cur_node->back()->position(),
            cur_node->position());
    }
}

/** Construct a node type string to store in the sodipodi:nodetypes attribute. */
std::string PathManipulator::_createTypeString()
{
    // precondition: no single-node subpaths
    std::stringstream tstr;
    for (auto & _subpath : _subpaths) {
        for (auto & j : *_subpath) {
            tstr << j.type();
        }
        // nodestring format peculiarity: first node is counted twice for closed paths
        if (_subpath->closed()) tstr << _subpath->begin()->type();
    }
    return tstr.str();
}

/** Update the path outline. */
void PathManipulator::_updateOutline()
{
    if (!_show_outline) {
        _outline->set_visible(false);
        return;
    }

    auto pv = _spcurve.get_pathvector() * _getTransform();
    // This SPCurve thing has to be killed with extreme prejudice
    if (_show_path_direction) {
        // To show the direction, we append additional subpaths which consist of a single
        // linear segment that starts at the time value of 0.5 and extends for 10 pixels
        // at an angle 150 degrees from the unit tangent. This creates the appearance
        // of little 'harpoons' that show the direction of the subpaths.
        auto rot_scale_w2d = Geom::Rotate(210.0 / 180.0 * M_PI) * Geom::Scale(10.0) * _desktop->w2d();
        Geom::PathVector arrows;
        for (auto & path : pv) {
            for (Geom::Path::iterator j = path.begin(); j != path.end_default(); ++j) {
                Geom::Point at = j->pointAt(0.5);
                Geom::Point ut = j->unitTangentAt(0.5);
                Geom::Point arrow_end = at + (Geom::unit_vector(_desktop->d2w(ut)) * rot_scale_w2d);

                Geom::Path arrow(at);
                arrow.appendNew<Geom::LineSegment>(arrow_end);
                arrows.push_back(arrow);
            }
        }
        pv.insert(pv.end(), arrows.begin(), arrows.end());
    }
    auto tmp = SPCurve(std::move(pv));
    _outline->set_bpath(&tmp);
    _outline->set_visible(true);
}

/** Retrieve the geometry of the edited object from the object tree */
void PathManipulator::_getGeometry()
{
    using namespace Inkscape::LivePathEffect;
    auto lpeobj = cast<LivePathEffectObject>(_path);
    auto path = cast<SPPath>(_path);
    if (lpeobj) {
        Effect *lpe = lpeobj->get_lpe();
        if (lpe) {
            PathParam *pathparam = dynamic_cast<PathParam *>(lpe->getParameter(_lpe_key.data()));
            _spcurve = SPCurve(pathparam->get_pathvector());
        }
    } else if (path) {
        if (path->curveForEdit()) {
            _spcurve = *path->curveForEdit();
        } else {
            _spcurve = SPCurve();
        }
    }
}

/** Set the geometry of the edited object in the object tree, but do not commit to XML */
void PathManipulator::_setGeometry()
{
    using namespace Inkscape::LivePathEffect;
    auto lpeobj = cast<LivePathEffectObject>(_path);
    auto path = cast<SPPath>(_path);
    if (lpeobj) {
        // copied from nodepath.cpp
        // NOTE: if we are editing an LPE param, _path is not actually an SPPath, it is
        // a LivePathEffectObject. (mad laughter)
        Effect *lpe = lpeobj->get_lpe();
        if (lpe) {
            PathParam *pathparam = dynamic_cast<PathParam *>(lpe->getParameter(_lpe_key.data()));
            if (pathparam->get_pathvector() == _spcurve.get_pathvector()) {
                return; //False we dont update LPE
            }
            pathparam->set_new_value(_spcurve.get_pathvector(), false);
            lpeobj->requestModified(SP_OBJECT_MODIFIED_FLAG);
        }
    } else if (path) {
        // return true to leave the decision on empty to the caller.
        // Maybe the path become empty and we want to update to empty
        if (empty()) return;
        if (path->curveBeforeLPE()) {
            path->setCurveBeforeLPE(&_spcurve);
            if (path->hasPathEffectRecursive()) {
                sp_lpe_item_update_patheffect(path, true, false);
            }
        } else {
            path->setCurve(&_spcurve);
        }
    }
}

/** Figure out in what attribute to store the nodetype string. */
Glib::ustring PathManipulator::_nodetypesKey()
{
    auto lpeobj = cast<LivePathEffectObject>(_path);
    if (!lpeobj) {
        return "sodipodi:nodetypes";
    } else {
        return _lpe_key + "-nodetypes";
    }
}

/** Return the XML node we are editing.
 * This method is wrong but necessary at the moment. */
Inkscape::XML::Node *PathManipulator::_getXMLNode()
{
    //XML Tree being used here directly while it shouldn't be.
    auto lpeobj = cast<LivePathEffectObject>(_path);
    if (!lpeobj)
        return _path->getRepr();
    //XML Tree being used here directly while it shouldn't be.
    return lpeobj->getRepr();
}

bool PathManipulator::_nodeClicked(Node *n, ButtonReleaseEvent const &event)
{
    if (event.button != 1) return false;
    if (mod_alt(event) && mod_ctrl(event)) {
        // Ctrl+Alt+click: delete nodes
        hideDragPoint();
        NodeList::iterator iter = NodeList::get_iterator(n);
        NodeList &nl = iter->nodeList();

        if (nl.size() <= 1 || (nl.size() <= 2 && !nl.closed())) {
            // Removing last node of closed path - delete it
            nl.kill();
        } else {
            // In other cases, delete the node under cursor
            _deleteStretch(iter, iter.next(), NodeDeleteMode::curve_fit);
        }

        if (!empty()) { 
            update(true);
        }

        // We need to call MPM's method because it could have been our last node
        _multi_path_manipulator._doneWithCleanup(_("Delete node"));

        return true;
    } else if (mod_ctrl(event)) {
        // Ctrl+click: cycle between node types
        if (!n->isEndNode()) {
            n->setType(static_cast<NodeType>((n->type() + 1) % NODE_LAST_REAL_TYPE));
            update();
            _commit(_("Cycle node type"));
        }
        return true;
    }
    return false;
}

void PathManipulator::_handleGrabbed()
{
    _selection.hideTransformHandles();
}

void PathManipulator::_handleUngrabbed()
{
    _selection.restoreTransformHandles();
    _commit(_("Drag handle"));
}

bool PathManipulator::_handleClicked(Handle *h, ButtonReleaseEvent const &event)
{
    // retracting by Alt+Click
    if (event.button == 1 && mod_alt(event)) {
        h->move(h->parent()->position());
        update();
        _commit(_("Retract handle"));
        return true;
    }
    return false;
}

void PathManipulator::_selectionChangedM(std::vector<SelectableControlPoint *> pvec, bool selected) {
    for (auto & n : pvec) {
        _selectionChanged(n, selected);
    }
}

void PathManipulator::_selectionChanged(SelectableControlPoint *p, bool selected)
{
    // don't do anything if we do not show handles
    if (!_show_handles) return;

    // only do something if a node changed selection state
    Node *node = dynamic_cast<Node*>(p);
    if (!node) return;

    // update handle display
    NodeList::iterator iters[5];
    iters[2] = NodeList::get_iterator(node);
    iters[1] = iters[2].prev();
    iters[3] = iters[2].next();
    if (selected) {
        // selection - show handles on this node and adjacent ones
        node->showHandles(true);
        if (iters[1]) iters[1]->showHandles(true);
        if (iters[3]) iters[3]->showHandles(true);
    } else {
        /* Deselection is more complex.
         * The change might affect 3 nodes - this one and two adjacent.
         * If the node and both its neighbors are deselected, hide handles.
         * Otherwise, leave as is. */
        if (iters[1]) iters[0] = iters[1].prev();
        if (iters[3]) iters[4] = iters[3].next();
        bool nodesel[5];
        for (int i = 0; i < 5; ++i) {
            nodesel[i] = iters[i] && iters[i]->selected();
        }
        for (int i = 1; i < 4; ++i) {
            if (iters[i] && !nodesel[i-1] && !nodesel[i] && !nodesel[i+1]) {
                iters[i]->showHandles(false);
            }
        }
    }
}

/** Removes all nodes belonging to this manipulator from the control point selection */
void PathManipulator::_removeNodesFromSelection()
{
    // remove this manipulator's nodes from selection
    for (auto & _subpath : _subpaths) {
        for (NodeList::iterator j = _subpath->begin(); j != _subpath->end(); ++j) {
            _selection.erase(j.get_pointer());
        }
    }
}

/** Update the XML representation and put the specified annotation on the undo stack */
void PathManipulator::_commit(Glib::ustring const &annotation)
{
    writeXML();
    if (_desktop) {
        DocumentUndo::done(_desktop->getDocument(), annotation.data(), INKSCAPE_ICON("tool-node-editor"));
    }
}

void PathManipulator::_commit(Glib::ustring const &annotation, gchar const *key)
{
    writeXML();
    DocumentUndo::maybeDone(_desktop->getDocument(), key, annotation.data(), INKSCAPE_ICON("tool-node-editor"));
}

/** Update the position of the curve drag point such that it is over the nearest
 * point of the path. */
Geom::Coord PathManipulator::_updateDragPoint(Geom::Point const &evp)
{
    auto &m = _desktop->getNamedView()->snap_manager;
    m.setup(_desktop);
    Inkscape::SnapCandidatePoint scp(_desktop->w2d(evp), Inkscape::SNAPSOURCE_OTHER_HANDLE);
    Inkscape::SnappedPoint sp = m.freeSnap(scp, Geom::OptRect(), false);
    m.unSetup();

    Geom::Coord dist = HUGE_VAL;

    Geom::Affine to_desktop = _getTransform();
    Geom::PathVector pv = _spcurve.get_pathvector();
    std::optional<Geom::PathVectorTime> pvp = pv.nearestTime(sp.getPoint() * to_desktop.inverse());
    if (!pvp) return dist;

    Geom::Point nearest_pt_dt = pv.pointAt(*pvp) * to_desktop;
    Geom::Point nearest_pt = _desktop->d2w(nearest_pt_dt);
    dist = Geom::distance(_desktop->d2w(sp.getPoint()), nearest_pt);
    double stroke_tolerance = _getStrokeTolerance();

    bool drag_point_updated = false;
    if (dist < stroke_tolerance) {

        double fracpart = pvp->t;
        std::list<SubpathPtr>::iterator spi = _subpaths.begin();
        for (unsigned i = 0; i < pvp->path_index; ++i, ++spi) {}
        NodeList::iterator first = (*spi)->before(pvp->asPathTime());

        if (first && first.next() &&
            fracpart != 0.0 &&
            fracpart != 1.0) {

            drag_point_updated = true;
            // stroke_tolerance is at least two.
            int tolerance = std::max(2, (int)stroke_tolerance);
            _dragpoint->setPosition(_desktop->w2d(nearest_pt));
            _dragpoint->setSize(2 * tolerance - 1);
            _dragpoint->setTimeValue(fracpart);
            _dragpoint->setIterator(first);
        }
    }

    _dragpoint->setVisible(drag_point_updated);

    return dist;
}

/// This is called on zoom change to update the direction arrows
void PathManipulator::_updateOutlineOnZoomChange()
{
    if (_show_path_direction) _updateOutline();
}

/** Compute the radius from the edge of the path where clicks should initiate a curve drag
 * or segment selection, in window coordinates. */
double PathManipulator::_getStrokeTolerance()
{
    /* Stroke event tolerance is equal to half the stroke's width plus the global
     * drag tolerance setting.  */
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    double ret = prefs->getIntLimited("/options/dragtolerance/value", 2, 0, 100);
    if (_path && _path->style && !_path->style->stroke.isNone()) {
        ret += _path->style->stroke_width.computed * 0.5
            * _getTransform().descrim() // scale to desktop coords
            * _desktop->current_zoom(); // == _d2w.descrim() - scale to window coords
    }
    return ret;
}

} // namespace Inkscape::UI

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
