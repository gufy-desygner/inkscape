/*
 * TableRecogniseCommon.cpp
 *
 *  Created on: Dec 28, 2021
 *      Author: sergey
 */

#include "TableRecognizeCommon.h"
#include <math.h>
#include <string>
#include "svg/css-ostringstream.h"

std::string doubleToCss(double num)
{
	Inkscape::CSSOStringStream os;
	os << num;
	return os.str();
}

/**
 * @describe how big part of kind rectangle intersected with main rectangle
 * @param allowLine  - line can has zero square IMRE-418
 * @return percent
 */
double rectIntersect(const Geom::Rect& main, const Geom::Rect& kind, bool allowLine)
{
	if (! main.intersects(kind)) return 0;

	float widthOfChild = std::fabs(kind[Geom::X][0] - kind[Geom::X][1]);
	float heightOfChild = std::fabs(kind[Geom::Y][0] - kind[Geom::Y][1]);

	bool isLine = (allowLine && (widthOfChild < 0.001 || heightOfChild < 0.001));

	double squareOfKind = 0;

	if (isLine) {
		// length of line at this case
		squareOfKind = (widthOfChild < 0.001 ? heightOfChild : widthOfChild);
	}
	else {
		squareOfKind = widthOfChild * heightOfChild;
	}
	if (squareOfKind == 0)
		return 0;

	double x0 = (kind[Geom::X][0] < main[Geom::X][0]) ? main[Geom::X][0] : kind[Geom::X][0];
	double x1 = (kind[Geom::X][1] > main[Geom::X][1]) ? main[Geom::X][1] : kind[Geom::X][1];

	double y0 = (kind[Geom::Y][0] < main[Geom::Y][0]) ? main[Geom::Y][0] : kind[Geom::Y][0];
	double y1 = (kind[Geom::Y][1] > main[Geom::Y][1]) ? main[Geom::Y][1] : kind[Geom::Y][1];

	double squareOfintersects;
	if (isLine) {
		if (widthOfChild < 0.001) {
			squareOfintersects = 1 * std::fabs(y1 - y0);
		} else {
			squareOfintersects = std::fabs(x1 - x0) * 1;
		}
	} else {
		squareOfintersects = std::fabs(x1 - x0) * std::fabs(y1 - y0);
	}

	return (squareOfintersects/squareOfKind) * 100;
}

bool isNotTable(Inkscape::XML::Node *node)
{
	const char* classes = node->attribute("class");
	if (classes == nullptr) return true;
	return (strcmp(classes, "table") != 0);
}

bool isTableNode(Inkscape::XML::Node* node)
{
	if (node == nullptr) return false;
	const char* className = node->attribute("class");
	if (className == nullptr) return false;
	return (strcmp(className,"table") == 0);
}

