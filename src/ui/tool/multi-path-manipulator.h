// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Multi path manipulator - a tool component that edits multiple paths at once
 */
/* Authors:
 *   Krzysztof Kosiński <tweenk.pl@gmail.com>
 *
 * Copyright (C) 2009 Authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef INKSCAPE_UI_TOOL_MULTI_PATH_MANIPULATOR_H
#define INKSCAPE_UI_TOOL_MULTI_PATH_MANIPULATOR_H

#include <cstddef>
#include <sigc++/connection.h>
#include <2geom/path-sink.h>
#include "commit-events.h"
#include "manipulator.h"
#include "modifier-tracker.h"
#include "node-types.h"
#include "shape-record.h"
#include "ui/tool/path-manipulator.h"

namespace Inkscape {
namespace UI {

class PathManipulator;
class MultiPathManipulator;
struct PathSharedData;

/**
 * Manipulator that manages multiple path manipulators active at the same time.
 */
class MultiPathManipulator : public PointManipulator
{
public:
    MultiPathManipulator(PathSharedData &data);
    ~MultiPathManipulator() override;

    bool event(Inkscape::UI::Tools::ToolBase *tool, CanvasEvent const &event) override;

    bool empty() const { return _mmap.empty(); }
    size_t size() const { return _mmap.size(); }
    void setItems(std::set<ShapeRecord> const &);
    void clear() { _mmap.clear(); }
    void cleanup();

    void selectSubpaths();
    void shiftSelection(int dir);
    void invertSelectionInSubpaths();

    void setNodeType(NodeType t);
    void setSegmentType(SegmentType t);

    void insertNodesAtExtrema(ExtremumType extremum);
    void insertNodes();
    void insertNode(Geom::Point pt);
    void alertLPE();
    void duplicateNodes();
    void copySelectedPath(Geom::PathBuilder *builder);
    void joinNodes();
    void breakNodes();
    void deleteNodes();
    void deleteNodes(NodeDeleteMode mode);
    void joinSegments();
    void deleteSegments();
    void alignNodes(Geom::Dim2 d, AlignTargetNode target = AlignTargetNode::MID_NODE);
    void distributeNodes(Geom::Dim2 d);
    void reverseSubpaths();
    void move(Geom::Point const &delta);
    void scale(Geom::Point const &center, Geom::Point const &scale);

    void showOutline(bool show);
    void showHandles(bool show);
    void showPathDirection(bool show);
    void setLiveOutline(bool set);
    void setLiveObjects(bool set);
    void updateOutlineColors();
    void updateHandles();
    void updatePaths();
    
    /// Emitted whenever the coordinates shown in the status bar need updating.
    sigc::signal<void ()> signal_coords_changed;

private:
    using MapPair = std::pair<ShapeRecord, std::shared_ptr<PathManipulator>>;
    using MapType = std::map<ShapeRecord, std::shared_ptr<PathManipulator>>;

    template <typename R>
    void invokeForAll(R (PathManipulator::*method)()) {
        for (MapType::iterator i = _mmap.begin(); i != _mmap.end(); ) {
            // Sometimes the PathManipulator got freed at loop end, thus
            // invalidating the iterator so make sure that next_i will
            // be a valid iterator and then assign i to it.
            MapType::iterator next_i = i;
            ++next_i;
            // i->second is a std::shared_ptr so try to hold on to it so
            // it won't get freed prematurely by the WriteXML() method or
            // whatever. See https://bugs.launchpad.net/inkscape/+bug/1617615
            // Applicable to empty paths.
            std::shared_ptr<PathManipulator> hold(i->second);
            ((hold.get())->*method)();
            i = next_i;
        }
    }
    template <typename R, typename A>
    void invokeForAll(R (PathManipulator::*method)(A), A a) {
        for (auto & i : _mmap) {
            ((i.second.get())->*method)(a);
        }
    }
    template <typename R, typename A>
    void invokeForAll(R (PathManipulator::*method)(A const &), A const &a) {
        for (auto & i : _mmap) {
            ((i.second.get())->*method)(a);
        }
    }
    template <typename R, typename A, typename B>
    void invokeForAll(R (PathManipulator::*method)(A,B), A a, B b) {
        for (auto & i : _mmap) {
            ((i.second.get())->*method)(a, b);
        }
    }

    void _commit(CommitEvent cps);
    void _done(gchar const *reason, bool alert_LPE = true);
    void _doneWithCleanup(gchar const *reason, bool alert_LPE = false);
    Colors::Color _getOutlineColor(ShapeRole role, SPObject *object);

    MapType _mmap;

public:
    PathSharedData const &_path_data;

private:
    ModifierTracker _tracker;
    bool _show_handles;
    bool _show_outline;
    bool _show_path_direction;
    bool _live_outline;
    bool _live_objects;

    friend class PathManipulator;
};

} // namespace UI
} // namespace Inkscape

#endif // INKSCAPE_UI_TOOL_MULTI_PATH_MANIPULATOR_H

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
