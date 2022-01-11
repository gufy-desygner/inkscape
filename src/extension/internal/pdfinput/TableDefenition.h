/*
 * TableDefenition.h
 *
 *  Created on: Dec 28, 2021
 *      Author: sergey
 */

#ifndef SRC_EXTENSION_INTERNAL_PDFINPUT_TABLEDEFENITION_H_
#define SRC_EXTENSION_INTERNAL_PDFINPUT_TABLEDEFENITION_H_

#include "svg-builder.h"
#include "TabLine.h"
#include "TabRect.h"
#include "xml/node.h"
#include "2geom/2geom.h"

using namespace Inkscape::Extension::Internal;

struct TableCell {
	double x, y, width, height;
	 TabLine *topLine;
	 TabLine *bottomLine;
	 TabLine *leftLine;
	 TabLine *rightLine;
	 TabRect *rect;
	 int mergeIdx;
	 bool isMax;
	 int maxCol;
	 int maxRow;
	 TableCell* cellMax;
};

class TableDefenition
{
private:
	TableCell* _cells;

	std::vector<Inkscape::XML::Node*> unsupportedTextList;

	int countCol, countRow;

	bool isHidCell(int c, int r);
	Inkscape::XML::Node* cellRender(SvgBuilder *builder, int c, int r, Geom::Affine aff);
	Inkscape::XML::Node* getLeftBorder(SvgBuilder *builder, int c, int r, Geom::Affine aff);
	Inkscape::XML::Node* getTopBorder(SvgBuilder *builder, int c, int r, Geom::Affine aff);
	Inkscape::XML::Node* getBottomBorder(SvgBuilder *builder, int c, int r, Geom::Affine aff);
	Inkscape::XML::Node* getRightBorder(SvgBuilder *builder, int c, int r, Geom::Affine aff);


public:
	double x, y, width, height;
	TableDefenition(unsigned int width, unsigned int height) :
		countCol(width),
		countRow(height),
		x(0),
		y(0),
		width(0),
		height(0)
	{
		_cells = new TableCell[countCol*countRow];

	}
	void setStroke(int xIdx, int yIdx, TabLine *topLine, TabLine *bottomLine, TabLine *leftLine, TabLine *rightLine);
	void setVertex(int xIdx, int yIdx, double xStart, double yStart, double xEnd, double yEnd);
	TableCell* getCell(int xIdx, int yIdx);
	void setMergeIdx(int col1, int row1, int mergeIdx);
	void setMergeIdx(int col1, int row1, int col2, int row2, int mergeIdx);
	void setRect(int col, int row, TabRect* rect);

	std::vector<Inkscape::XML::Node*> getUnsupportedTextInTable() { return unsupportedTextList; }

	Inkscape::XML::Node* render(SvgBuilder* builder, Geom::Affine aff);
};


#endif /* SRC_EXTENSION_INTERNAL_PDFINPUT_TABLEDEFENITION_H_ */
