/*
 * TabRect.cpp
 *
 *  Created on: Dec 28, 2021
 *      Author: sergey
 */

#include "TabRect.h"

TabRect::TabRect(double _x1, double _y1, double _x2, double _y2, Inkscape::XML::Node* _node) :
	x1(_x1),
	x2(_x2),
	y1(_y1),
	y2(_y2),
	node(_node)
{

}

TabRect::TabRect(Geom::Point point1, Geom::Point point2, Inkscape::XML::Node* _node) :
	x1(point1.x()),
	x2(point2.x()),
	y1(point1.y()),
	y2(point2.y()),
	node(_node)
{


}
