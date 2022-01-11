/*
 * TabLine.cpp
 *
 *  Created on: Dec 28, 2021
 *      Author: sergey
 */

#include "TabLine.h"
#include "xml/repr.h"
#include "svg/svg.h"
#include "sp-path.h"
#include "document.h"

//should be classs function
SPCSSAttr* adjustStroke(TabLine* tabLine)
{
	double strokeWidth;
	SPCSSAttr *style = sp_repr_css_attr(tabLine->node, "style");
	const gchar *strokeWidthStr = sp_repr_css_property(style, "stroke-width", "1");
	if (! sp_svg_number_read_d(strokeWidthStr, &strokeWidth))
		strokeWidth = 0;

	if (tabLine->isVertical())
		strokeWidth *= tabLine->affineToMainNode[0];
	else
		strokeWidth *= tabLine->affineToMainNode[3];

	sp_repr_css_set_property(style, "stroke-width", doubleToCss(std::fabs(strokeWidth)).c_str());
	return style;
}

TabLine::TabLine(Inkscape::XML::Node* node, const Geom::Curve& curve, SPDocument* spDoc) :
		isVert(false),
		node(node)
{
	lookLikeTab = false;

	if (strcmp(node->name(), "svg:path") != 0)
		return;

	SPPath* spPath = (SPPath*)spDoc->getObjectByRepr(node);
	spCurve = spPath->getCurve();
	//size_t segmentCount = curve->get_segment_count();
	//if (segmentCount > 1 )
	//	return;

	if (! curve.isLineSegment())
		return;

	Geom::Point start = curve.initialPoint();
	Geom::Point end = curve.finalPoint();

	x1 = start[0];
	x2 = end[0];
	y1 = start[1];
	y2 = end[1];
	//printf("   line (%f %f) (%f %f)\n", x1, y1, x2, y2);
	if (approxEqual(x1, x2) || approxEqual(y1, y2))
		lookLikeTab = true;

	if (approxEqual(x1, x2)) isVert = true;
}

bool TabLine::isVertical()
{
	return approxEqual(x1, x2);
}
