/*
 * TableRegion.cpp
 *
 *  Created on: Dec 28, 2021
 *      Author: sergey
 */

#include "TableRegion.h"
#include "document.h"

struct CellCoord {
	int col, row;
	CellCoord(int c, int r) :
		col(c),
		row(r)
	{
	}
};

struct SkipCells {
	std::vector<CellCoord> list;

	void addRect(int col1, int row1, int col2, int row2, bool ignoreFirst = false)
	{
		for(int colIdx = col1; colIdx <= col2; colIdx++)
		{
			for(int rowIdx = row1; rowIdx <= row2; rowIdx++)
			{
				if (ignoreFirst && colIdx == col1 && rowIdx == row1) continue;
				list.push_back(CellCoord(colIdx, rowIdx));
			}
		}
	}

	bool isExist(int col, int row)
	{
		for(auto& cell : list)
		{
			if ( cell.col == col && cell.row == row ) return true;
		}
		return false;
	}
};


// inverse order for double sort
static bool tableRowsSorter(const double &a, const double &b)
{
	return a > b;
}

static bool predAproxUniq(const float &a, const float &b)
{
	static float aproxValue = 0.5 * getDpiCoff();
	return approxEqual(a, b, aproxValue); //minimal cell size/resolution.
}

TableRegion::~TableRegion()
{
	if (tableDef)
		delete(tableDef);
	for(auto& line : lines)
	{
		delete line;
	}
}

bool TableRegion::addLine(Inkscape::XML::Node* node)
{
	//printf("node id =%s\n", node->attribute("id"));

	SPPath* spPath = (SPPath*)spDoc->getObjectByRepr(node);
	SPCurve* curve = spPath->getCurve();
	SPObject* spMainNode = spDoc->getObjectByRepr(_builder->getMainNode() );
	Geom::Affine pathAffine = spPath->getRelativeTransform(spMainNode);


	const Geom::PathVector pathArray = curve->get_pathvector();

	for (const Geom::Path& itPath : pathArray)
	{
		double _x1 = 1e6;
		double _y1 = 1e6;
		double _x2 = 0;
		double _y2 = 0;
		for(const Geom::Curve& simplCurve : itPath) {
			TabLine* line = new TabLine(node, simplCurve, spDoc);
			line->affineToMainNode = pathAffine;
			lines.push_back(line);
			if (! line->isTableLine())
			{
				_isTable = false;
			}

			_x1 = _x1 < line->x1 ? _x1 : line->x1;
			_y1 = _y1 < line->y1 ? _y1 : line->y1;
			_x2 = _x2 > line->x2 ? _x2 : line->x2;
			_y2 = _y2 > line->y2 ? _y2 : line->y2;
		}
		if (! _isTable) continue;

		Geom::Point point1(_x1, _y1);
		Geom::Point point2(_x2, _y2);
		point1 = point1 * pathAffine;
		point2 = point2 * pathAffine;
		this->x1 = this->x1 < point1[Geom::X] ? this->x1 : point1[Geom::X];
		this->y1 = this->y1 < point1[Geom::Y] ? this->y1 : point1[Geom::Y];
		this->x2 = this->x2 > point2[Geom::X] ? this->x2 : point2[Geom::X];
		this->y2 = this->y2 > point2[Geom::Y] ? this->y2 : point2[Geom::Y];

		if (itPath.size() == 4 && itPath.closed())
		{
			TabRect* rect = new TabRect(point1, point2, node);
			rects.push_back(rect);
		}
	}

	return isTable();
}

TabRect* TableRegion::matchRect(double _x1, double _y1, double _x2, double _y2)
{
	Geom::Rect inRect(_x1, _y1, _x2, _y2);
	TabRect* possibleRect = nullptr;
	float currentIntersect = 0;
	for(auto& rect : rects)
	{
		// Always use round for better results, this will avoid rects not being mactched!
		Geom::Rect currentRect(round(rect->x1), round(rect->y1), round(rect->x2), round(rect->y2));
		//float intrsectsquare = rectIntersect(inRect, currentRect);
		if (rectIntersect(currentRect, inRect) > 90)
		{
			//if (currentIntersect >= intrsectsquare) continue;
			//currentIntersect = intrsectsquare;

			SPItem* spNode = (SPItem*) _builder->getSpDocument()->getObjectByRepr(rect->node);
			const char* fillStyle = spNode->getStyleProperty("fill", "none");

			if (strncmp(fillStyle, "url(#pattern", 12) == 0)
			{
				throw ExceptionFillPatternDetected();
			}

			if (strncmp(fillStyle, "#ffffff", 7) == 0)
			{
				possibleRect = rect;
				continue;
			}

			if (strncmp(fillStyle, "none", 4) != 0)
				return rect;
		}
	}

	return possibleRect;
}

TabLine* TableRegion::searchByPoint(double xCoord, double yCoord, bool isVerticale)
{

	TabLine* rectLine = nullptr;
	for(auto& line : lines)
	{
		if (isVerticale)
		{
			if (! line->isVertical()) continue;
			if (! approxEqual(line->x1, xCoord, 0.51)) continue;
			if (line->y1 > yCoord || line->y2 < yCoord) continue;

			size_t segmentCount = line->curveSegmentsCount();
			if (segmentCount > 1) rectLine = line; // simple lines is more contrast boorder
			else return line;
		} else {
			if (line->isVertical()) continue;
			if (! approxEqual(line->y1, yCoord, 0.51)) continue;
			if (line->x1 > xCoord || line->x2 < xCoord) continue;

			size_t segmentCount = line->curveSegmentsCount();
			if (segmentCount > 1) rectLine = line; // simple lines is more contrast boorder
			else return line;
		}
	}

	return rectLine;
}

bool TableRegion::buildKnote(SvgBuilder *builder)
{
	std::vector<double> xList;
	std::vector<double> yList;

	SPDocument* spDoc = builder->getSpDocument();
	SPObject* spMainNode = spDoc->getObjectByRepr( builder->getMainNode() );
	SkipCells skipCell;

// calculate simple grid
	for(auto& line : this->lines)
	{
		SPPath* spLine = (SPPath*)spDoc->getObjectByRepr(line->node);
		Geom::Affine lineAffine = spLine->getRelativeTransform(spMainNode);
		Geom::Point firstPoint(line->x1, line->y1);
		Geom::Point secondPoint(line->x2, line->y2);

		firstPoint = firstPoint * lineAffine;
		secondPoint = secondPoint * lineAffine;

		line->x1 = firstPoint.x() < secondPoint.x() ? firstPoint.x() : secondPoint.x();
		line->y1 = firstPoint.y() < secondPoint.y() ? firstPoint.y() : secondPoint.y();
		line->x2 = firstPoint.x() > secondPoint.x() ? firstPoint.x() : secondPoint.x();
		line->y2 = firstPoint.y() > secondPoint.y() ? firstPoint.y() : secondPoint.y();

		if (approxEqual(line->x1, line->x2))
		    xList.push_back(line->x1);

        if (approxEqual(line->y1, line->y2))
	    	yList.push_back(line->y1);

	}

	if (xList.empty() || yList.empty()) return false;

	std::sort(xList.begin(), xList.end());
	std::sort(yList.begin(), yList.end(), &tableRowsSorter); // invert sort order

	if (! approxEqual(this->x1, xList.front(), 5 * getDpiCoff()))
		xList.push_back(this->x1);
	if (! approxEqual(this->x2, xList.back(), 5 * getDpiCoff()))
		xList.push_back(this->x2);

	if (! approxEqual(this->y1, yList.back(), 5 * getDpiCoff()))
		yList.push_back(this->y1);
	if (! approxEqual(this->y2, yList.front(), 5 * getDpiCoff()))
		yList.push_back(this->y2);
	std::sort(xList.begin(), xList.end());
	std::sort(yList.begin(), yList.end(), &tableRowsSorter); // invert sort order

	auto lastX = std::unique(xList.begin(), xList.end(), predAproxUniq);
	auto lastY = std::unique(yList.begin(), yList.end(), predAproxUniq);
	xList.erase(lastX, xList.end());
	yList.erase(lastY, yList.end());

	if (((yList.size() -1) * (xList.size() - 1)) < 4 )
		return false;

/*
	for(auto& xPos : xList)
	{
		printf("%f ", xPos);
	}
	printf("\n");

	for(auto& yPos : yList)
	{
		printf("%f ", yPos);
	}
	printf("\n");
*/
	tableDef = new TableDefenition(xList.size() -1 , yList.size() -1);
	double xStart, yStart, xEnd, yEnd;
	xStart = xList[0];
	yStart = yList[0];


// set table size
	tableDef->x = xList[0];
	tableDef->y = yList[yList.size() - 1];
	tableDef->width = xList[xList.size() - 1] - xStart;
	tableDef->height = yStart - yList[yList.size() - 1];

	std::vector<SvgTextPosition> textList = _builder->getTextInArea(tableDef->x, tableDef->y,
			tableDef->x + tableDef->width, tableDef->y + tableDef->height, true);

	if (textList.empty()) return false;

	for(int yIdx = 1; yIdx < yList.size() ; yIdx++)
	{
		xStart = xList[0];
		for(int xIdx = 1; xIdx < xList.size() ; xIdx++)
		{
			double xMediane = (xList[xIdx] - xStart)/2 + xStart;
			double yMediane = (yStart - yList[yIdx])/2 + yList[yIdx];
			TabLine* topLine = searchByPoint(xMediane, yStart, false);
			TabLine* leftLine = searchByPoint(xStart, yMediane, true);

			TabLine* rightLine = searchByPoint(xList[xIdx], yMediane, true);
			int xShift = 0;
			while(rightLine == nullptr && (xIdx + 1 + xShift) < xList.size())
			{
				xShift++;
				rightLine = searchByPoint(xList[xIdx + xShift], yMediane, true);
			}

			TabLine* bottomLine = searchByPoint(xMediane, yList[yIdx], false);
			int yShift = 0;
			while(bottomLine == nullptr && (yIdx + 1 + yShift) < yList.size())
			{
				yShift++;
				bottomLine = searchByPoint(xMediane, yList[yIdx + yShift], false);
			}

			if (skipCell.isExist(xIdx - 1, yIdx - 1))
			{
				tableDef->setStroke(xIdx - 1, yIdx - 1, topLine, bottomLine, leftLine, rightLine);
				tableDef->setVertex(xIdx - 1, yIdx - 1, xStart, yList[yIdx], xList[xIdx], yStart);
				xStart = xList[xIdx];
				tableDef->setRect(xIdx -1, yIdx -1, nullptr);

				continue;
			} else {
				tableDef->setStroke(xIdx - 1, yIdx - 1, topLine, bottomLine, leftLine, rightLine);
				tableDef->setVertex(xIdx - 1, yIdx - 1, xStart, yList[yIdx + yShift], xList[xIdx + xShift], yStart);
			}

			tableDef->setMergeIdx(xIdx - 1, yIdx - 1, -1);

			static int mergeIdx = 0;
			mergeIdx++;

			if (xShift >0 || yShift >0)
			{
				tableDef->setMergeIdx(xIdx -1, yIdx -1, xIdx + xShift -1, yIdx + yShift -1, mergeIdx);
				skipCell.addRect(xIdx -1, yIdx -1, xIdx + xShift -1, yIdx + yShift -1, true);
			}

			TabRect* rect;
			try
			{
				rect = matchRect(xStart, yList[yIdx], xList[xIdx], yStart);
			}
			catch (ExceptionFillPatternDetected& e)
			{
				return false;
			}

			tableDef->setRect(xIdx -1, yIdx -1, rect);

			xStart = xList[xIdx];
		}
		yStart = yList[yIdx];
	}
	return true;
}

Inkscape::XML::Node* TableRegion::render(SvgBuilder *builder, Geom::Affine aff)
{
	Inkscape::XML::Node* result = tableDef->render(builder, aff);
	NodeList deleteNode;
	for(auto line : lines)
	{
		deleteNode.push_back(line->node);
		//line->node->parent()->removeChild(line->node);
		//delete(line->node);
	}

	std::sort(deleteNode.begin(), deleteNode.end());
	auto lastNode = std::unique(deleteNode.begin(), deleteNode.end());
	deleteNode.erase(lastNode, deleteNode.end());

	for(auto& node : deleteNode)
	{
		node->parent()->removeChild(node);
		delete(node);
	}

	return result;
}


Geom::Rect TableRegion::getBBox()
{
	return Geom::Rect(x1, y1, x2, y2);
}

double TableRegion::getAreaSize()
{
	return std::fabs((x1 - x2) * (y1 - y2));
}

bool TabLine::intersectRect(Geom::Rect rect)
{
	Geom::Rect line(Geom::Point(x1, y1) * affineToMainNode,
			Geom::Point(x2, y2) * affineToMainNode);
	return line.intersects(rect);

}

bool TableRegion::recIntersectLine(Geom::Rect rect)
{
	for(auto& line : lines)
	{
		if (line->intersectRect(rect))
			return true;
	}
	return false;
}

bool TableRegion::checkTableLimits()
{
	int freeLines = lines.size() - rects.size() * 4;
	if (freeLines < 2 && rects.size() < 4)
		return false;

	return true;
}


