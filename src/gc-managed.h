/*
 * Inkscape::GC::Managed - base class for GC-managed objects
 *
 * Copyright 2004 MenTaLguY <mental@rydia.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * See the file COPYING for details.
 *
 */

#ifndef SEEN_INKSCAPE_GC_MANAGED_H
#define SEEN_INKSCAPE_GC_MANAGED_H

#include "gc-core.h"

namespace Inkscape {

namespace GC {

/** @brief A base class for objects for whom the normal new and delete
  *        operators should use the garbage-collected allocator
  */
template <ScanPolicy default_scan=SCANNED,
          CollectionPolicy default_collect=AUTO>
class Managed {
public:
    /** @brief Registers a pointer to be cleared when this object becomes
      *        inaccessible.
      */
    template <typename T>
    void clearOnceInaccessible(T **p_ptr) {
        Core::general_register_disappearing_link(
            reinterpret_cast<void **>(p_ptr), Core::base(this)
        );
    }

    /** @brief Cancels the registration of a pointer, so it will not be
      *        cleared when this object becomes inacessible.
      */
    template <typename T>
    void cancelClearOnceInaccessible(T **p_ptr) {
        Core::unregister_disappearing_link(
            reinterpret_cast<void **>(p_ptr)
        );
    }

    void *operator new(std::size_t size,
                       ScanPolicy scan=default_scan,
                       CollectionPolicy collect=default_collect)
    throw (std::bad_alloc)
    {
        return ::operator new(size, scan, collect);
    }

    void *operator new[](std::size_t size,
                         ScanPolicy scan=default_scan,
                         CollectionPolicy collect=default_collect)
    throw (std::bad_alloc)
    {
        return ::operator new[](size, scan, collect);
    }

    void operator delete(void *p) { return ::operator delete(p, GC); }
};

}

}

#endif
/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=c++:expandtab:shiftwidth=4:tabstop=8:softtabstop=4 :
