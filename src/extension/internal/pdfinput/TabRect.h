/*
 * TabRect.h
 *
 *  Created on: Dec 28, 2021
 *      Author: sergey
 */

#ifndef SRC_EXTENSION_INTERNAL_PDFINPUT_TABRECT_H_
#define SRC_EXTENSION_INTERNAL_PDFINPUT_TABRECT_H_

#include "xml/node.h"
#include "2geom/2geom.h"

class TabRect {
public:
	Inkscape::XML::Node* node;
	double x1, x2, y1, y2;
	TabRect(double x1, double y1, double x2, double y2, Inkscape::XML::Node* node);
	TabRect(Geom::Point point1, Geom::Point point2, Inkscape::XML::Node* _node);
};

#endif /* SRC_EXTENSION_INTERNAL_PDFINPUT_TABRECT_H_ */
