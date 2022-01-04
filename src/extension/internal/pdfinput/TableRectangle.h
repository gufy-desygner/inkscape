/*
 * TableRectangle.h
 *
 *  Created on: Jan 3, 2022
 *      Author: sergey
 */

#ifndef SRC_EXTENSION_INTERNAL_PDFINPUT_TABLERECTANGLE_H_
#define SRC_EXTENSION_INTERNAL_PDFINPUT_TABLERECTANGLE_H_

#include "2geom/2geom.h"

class TableRectangle {
private:

	std::vector<Geom::Line> lines;
public:
	double x1, y1, x2, y2;
	void addLine(Geom::Line line);

	int countOfLines();

	double calcGap();
};

#endif /* SRC_EXTENSION_INTERNAL_PDFINPUT_TABLERECTANGLE_H_ */
