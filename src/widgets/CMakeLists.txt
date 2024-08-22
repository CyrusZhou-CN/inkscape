# SPDX-License-Identifier: GPL-2.0-or-later

set(widgets_SRC
	desktop-widget.cpp
	paintdef.cpp
	sp-attribute-widget.cpp
	sp-xmlview-tree.cpp
	spw-utilities.cpp

	# -------
	# Headers
	desktop-widget.h
	paintdef.h
	sp-attribute-widget.h
	sp-xmlview-tree.h
	spw-utilities.h
	widget-sizes.h
)

add_inkscape_source("${widgets_SRC}")
