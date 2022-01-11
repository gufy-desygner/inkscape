/*
 * TableRectangle.cpp
 *
 *  Created on: Jan 3, 2022
 *      Author: sergey
 */

#include "TableRectangle.h"
#include <glib.h>

void TableRectangle::addLine(Geom::Line line)
{
	double minX = MIN(line.initialPoint().x(), line.finalPoint().x());
	double maxX = MAX(line.initialPoint().x(), line.finalPoint().x());

	double minY = MIN(line.initialPoint().y(), line.finalPoint().y());
	double maxY = MAX(line.initialPoint().y(), line.finalPoint().y());
	if (lines.empty())
	{
		x1 = minX; x2 = maxX;
		y1 = minY; y2 = maxY;
	} else
	{
		x1 = MIN(x1, minX);
		x2 = MAX(x2, maxX);

		y1 = MIN(y1, minY);
		y2 = MAX(y2, maxY);
	}
	lines.push_back(line);
}

int TableRectangle::countOfLines()
{
	return lines.size();
}

double TableRectangle::calcGap()
{
	if (lines.size() < 2) return 0;
	std::vector<double> gaps;
	for(int idx = 1; idx < lines.size(); idx++)
	{
		gaps.push_back(std::fabs(lines[idx -1].initialPoint().y() - lines[idx].initialPoint().y()));
	}

	std::sort(gaps.begin(), gaps.end());
	for(int idx = 1; idx < gaps.size(); idx++)
	{

		if (!(gaps[idx]/gaps[0] < 4))
		{
			return 0;
		}
	}

	return gaps[0];
}

