// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Helper function to tie lifetime of a RefPtr-owned object to a managing object
 */
/*
 * Authors:
 *   Daniel Boles <dboles.src+inkscape@gmail.com>
 *
 * Copyright (C) 2023 Daniel Boles
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <map>
#include <vector>
#include <glib.h>
#include <glibmm/objectbase.h>
#include <glibmm/refptr.h>
#include "manage.h"

namespace Inkscape::UI {

using Refs = std::vector<Glib::RefPtr<Glib::ObjectBase const>>;
static auto s_map = std::map<Glib::ObjectBase const *, Refs>{};

[[nodiscard]] static auto
erase(void * const data)
{
	auto const manager = const_cast<Glib::ObjectBase const *>(
			    static_cast<Glib::ObjectBase       *>(data) );
	[[maybe_unused]] auto const count = s_map.erase(manager);
	g_assert(count == 1);
	return data;
}

static void
add_callback(Glib::ObjectBase const &manager)
{
	auto const data = static_cast<void             *>(
			   const_cast<Glib::ObjectBase *>(&manager) );
	manager.add_destroy_notify_callback(data, &erase);
}

void
manage(Glib::RefPtr<Glib::ObjectBase const> managed,
       Glib::ObjectBase const &manager)
{
	g_return_if_fail(static_cast<bool>(managed));

	auto const [it, inserted] = s_map.try_emplace(&manager);
	it->second.push_back( std::move(managed) );

	if (inserted) {
		add_callback(manager);
	}
}

} // namespace Inkscape::UI

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace .0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim:filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:fileencoding=utf-8:textwidth=99:
