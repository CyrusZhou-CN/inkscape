/*
 * Inkscape::AST - Abstract Syntax Tree in a Database
 *
 * Authors:
 *   MenTaLguY <mental@rydia.net>
 *
 * Copyright (C) 2003 MenTaLguY
 *
 * Released under GNU GPL, read the file 'COPYING' for more information
 */

#ifndef SEEN_INKSCAPE_AST_C_ARRAY_H
#define SEEN_INKSCAPE_AST_C_ARRAY_H

#include <new>
#include <gc/gc_cpp.h>
#include <stdexcept>
#include <cstdlib>
#include <cstring>
#include <glib/glib.h>
#include <libxml/tree.h>

namespace Inkscape {
namespace AST {

template <typename T>
struct CArray {
public:
    static CArray const &create_unsafe(T const *array) throw() {
        return *reinterpret_cast<CArray<T> const *>(array);
    }

    T const *toPointer() const throw() {
        return reinterpret_cast<T const *>(this);
    }

    T const &operator[](int i) const throw() {
        return reinterpret_cast<T const *>(this)[i];
    }

    operator T const *() const throw() {
        return toPointer();
    }

private:
    void operator=(CArray<T> const &);
};

};
};

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
