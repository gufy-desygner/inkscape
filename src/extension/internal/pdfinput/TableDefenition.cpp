/*
 * TableDefenition.cpp
 *
 *  Created on: Dec 28, 2021
 *      Author: sergey
 */

#include "TableDefenition.h"
#include "xml/repr.h"
#include "xml/sp-css-attr.h"
#include "sp-item.h"
#include "document.h"
#include "svg/svg.h"

int TableDefenition::tableID = 0;

Inkscape::XML::Node* TableDefenition::getTopBorder(SvgBuilder *builder, int c, int r, Geom::Affine aff)
{
	/*<line class="table-border table-border-v index-r-0 index-c-0 table-border-editor"
	 * stroke="#f40101" stroke-dasharray="0 0" stroke-linecap="round" stroke-width="1" x1="47.5" x2="47.5" y1="321" y2="371"
	 * id="svg_109"></line>
	 */

	NodeList borders;
	TableCell* cell = getCell(c, r);
	if (cell->topLine == nullptr) return nullptr;

	SPCSSAttr* cssStyle = adjustStroke(cell->topLine);

	Inkscape::XML::Node* borderNode = builder->createElement("svg:line");
	std::string classOfBorder("table-border table-border-h index-r-" +
			std::to_string(r) +
			" index-c-" +
			// Bug 10
			//"  table-border-editor");
			std::to_string(c));
	//borderNode->setAttribute("style", style);
	sp_repr_css_set(borderNode, cssStyle, "style");
	delete(cssStyle);

	borderNode->setAttribute("class", classOfBorder.c_str());

	borderNode->setAttribute("x1", doubleToCss(cell->x).c_str());
	borderNode->setAttribute("y1", doubleToCss(cell->y + cell->height).c_str());
	borderNode->setAttribute("x2", doubleToCss(cell->x + cell->width).c_str());
	borderNode->setAttribute("y2", doubleToCss(cell->y + cell->height).c_str());

	return borderNode;
}

Inkscape::XML::Node* TableDefenition::getBottomBorder(SvgBuilder *builder, int c, int r, Geom::Affine aff)
{
	/*<line class="table-border table-border-v index-r-0 index-c-0 table-border-editor"
	 * stroke="#f40101" stroke-dasharray="0 0" stroke-linecap="round" stroke-width="1" x1="47.5" x2="47.5" y1="321" y2="371"
	 * id="svg_109"></line>
	 */

	NodeList borders;
	TableCell* cell = getCell(c, r);
	if (cell->bottomLine == nullptr) return nullptr;

	SPCSSAttr* cssStyle = adjustStroke(cell->bottomLine);

	Inkscape::XML::Node* borderNode = builder->createElement("svg:line");
	std::string classOfBorder("table-border table-border-h index-r-" +
			std::to_string(r + 1) +
			" index-c-" +
			// Bug 10
			//"  table-border-editor");
			std::to_string(c));
	//borderNode->setAttribute("style", style);
	sp_repr_css_set(borderNode, cssStyle, "style");
	delete(cssStyle);
	borderNode->setAttribute("class", classOfBorder.c_str());

	borderNode->setAttribute("x1", doubleToCss(cell->x).c_str());
	borderNode->setAttribute("y1", doubleToCss(cell->y).c_str());
	borderNode->setAttribute("x2", doubleToCss(cell->x + cell->width).c_str());
	borderNode->setAttribute("y2", doubleToCss(cell->y).c_str());

	return borderNode;
}

bool TableDefenition::isHidCell(int c, int r)
{
	TableCell* cell = getCell(c, r);
	return ((! cell->isMax) && cell->mergeIdx > 0);
}

Inkscape::XML::Node* TableDefenition::getLeftBorder(SvgBuilder *builder, int c, int r, Geom::Affine aff)
{
	NodeList borders;
	TableCell* cell = getCell(c, r);
	if (cell->leftLine == nullptr) return nullptr;

	SPCSSAttr* cssStyle = adjustStroke(cell->leftLine);

	Inkscape::XML::Node* borderNode = builder->createElement("svg:line");
	std::string classOfBorder("table-border table-border-v index-r-" +
			std::to_string(r) +
			" index-c-" +
			// Bug 10
			//"  table-border-editor");
			std::to_string(c));
	//borderNode->setAttribute("style", style);
	sp_repr_css_set(borderNode, cssStyle, "style");
	delete(cssStyle);

	borderNode->setAttribute("class", classOfBorder.c_str());

	borderNode->setAttribute("x1", doubleToCss(cell->x).c_str());
	borderNode->setAttribute("y2", doubleToCss(cell->y).c_str());
	borderNode->setAttribute("x2", doubleToCss(cell->x).c_str());
	borderNode->setAttribute("y1", doubleToCss(cell->height + cell->y).c_str());

	return borderNode;
}

Inkscape::XML::Node* TableDefenition::getRightBorder(SvgBuilder *builder, int c, int r, Geom::Affine aff)
{
	/*<line class="table-border table-border-v index-r-0 index-c-0 table-border-editor"
	 * stroke="#f40101" stroke-dasharray="0 0" stroke-linecap="round" stroke-width="1" x1="47.5" x2="47.5" y1="321" y2="371"
	 * id="svg_109"></line>
	 */

	NodeList borders;
	TableCell* cell = getCell(c, r);
	if (cell->rightLine == nullptr) return nullptr;

	SPCSSAttr* cssStyle = adjustStroke(cell->rightLine);

	Inkscape::XML::Node* borderNode = builder->createElement("svg:line");
	std::string classOfBorder("table-border table-border-v index-r-" +
			std::to_string(r) +
			" index-c-" +
			// Bug 10
			//"  table-border-editor");
			std::to_string(c + 1));
	//borderNode->setAttribute("style", style);
	sp_repr_css_set(borderNode, cssStyle, "style");
	delete(cssStyle);

	borderNode->setAttribute("class", classOfBorder.c_str());

	borderNode->setAttribute("x1", doubleToCss(cell->x + cell->width).c_str());
	borderNode->setAttribute("y2", doubleToCss(cell->y).c_str());
	borderNode->setAttribute("x2", doubleToCss(cell->x + cell->width).c_str());
	borderNode->setAttribute("y1", doubleToCss(cell->height + cell->y).c_str());

	return borderNode;
}

Inkscape::XML::Node* TableDefenition::cellRender(SvgBuilder *builder, int c, int r, Geom::Affine aff)
{
	TableCell* cell = getCell(c, r);

    //printf("+++++++++++++++++++++++++++++++++++++++++++++++++++++++\n");
	//printf("cell: r = %d c = %d x = %f y = %f h = %f w = %f\n", r, c, cell->x, cell->y, cell->height, cell->width);

	Inkscape::XML::Node* nodeCellIdx = builder->createElement("svg:g");
	std::string classForNodeIdx("table-cell index-r-" + std::to_string(r) + " index-c-" + std::to_string(c));
	if (cell->mergeIdx > 0) {
		classForNodeIdx.append(" m-" + std::to_string(cell->mergeIdx));
		if (cell->isMax) {
			classForNodeIdx.append(" table-cell-max" );
		} else {
			classForNodeIdx.append(" table-cell-min" );
		}
	}
	nodeCellIdx->setAttribute("class", classForNodeIdx.c_str());

	Inkscape::XML::Node* nodeTextAttribs = builder->createElement("svg:g");
	// Bug 5
	//std::string classForNodeTextAttribs("textarea subelement active original-font-size-24");
	std::string classForNodeTextAttribs("textarea subelement active");
	nodeTextAttribs->setAttribute("class", classForNodeTextAttribs.c_str());

	Inkscape::XML::Node* nodeCellRect = builder->createElement("svg:rect");
	nodeCellRect->setAttribute("x", doubleToCss(cell->x).c_str());
	nodeCellRect->setAttribute("y", doubleToCss(cell->y).c_str());
	nodeCellRect->setAttribute("width", doubleToCss(cell->width).c_str());
	nodeCellRect->setAttribute("height", doubleToCss(cell->height).c_str());
	nodeCellRect->setAttribute("fill", "none");
	SPDocument* spDoc = builder->getSpDocument();
	if (cell->rect != nullptr && cell->rect->node != nullptr)
	{

		SPItem* spNode = (SPItem*) spDoc->getObjectByRepr(cell->rect->node);
		const char* fillStyle = spNode->getStyleProperty("fill", "none");
		nodeCellRect->setAttribute("fill", fillStyle);
	}

	// Bug 6
	//nodeCellRect->setAttribute("stroke-width", "1");
	//nodeCellRect->setAttribute("stroke", "blue");

	nodeTextAttribs->appendChild(nodeCellRect);
	nodeTextAttribs->setAttribute("data-table-id", std::to_string(tableID));
	//printf("[%d,%d] (x=%f, y=%f, w=%f, h=%f) #lines = %d\n", r, c, cell->x, cell->y, cell->width, cell->height, nLinesInCell);

	std::vector<SvgTextPosition> textInAreaList;
	if (!(cell->mergeIdx > 0 && cell->isMax == false))
	{
		textInAreaList = builder->getTextInArea(cell->x, cell->y, cell->x + cell->width, cell->y + cell->height);
	}
	// Even if the cell doesnt contain any text,
	// We need to add <g class="text"><text></text></g>
	// TODO: reverify this, we're setting the text only in the TOP LEFT cell for now.
	if (textInAreaList.size() == 0 || cell->mergeIdx > 0 && cell->isMax == false) {
		Inkscape::XML::Node* stringNode = builder->createTextNode("");
		Inkscape::XML::Node* tSpanNode = builder->createElement("svg:tspan");
		tSpanNode->setAttribute("x", "0");
		tSpanNode->setAttribute("y", "0");

		Inkscape::XML::Node* tTextNode = builder->createElement("svg:text");

		tTextNode->setAttribute("style", "fill:none;font-size:1px");
		Geom::Affine textTransformation(1, 0, 0, -1, cell->x, cell->y+1);
		gchar* trensformMatrix = sp_svg_transform_write(textTransformation);
		tTextNode->setAttribute("transform", trensformMatrix);
		free(trensformMatrix);

		tSpanNode->appendChild(stringNode);
		tTextNode->appendChild(tSpanNode);
		nodeTextAttribs->addChild(tTextNode, nodeCellRect);
		nodeTextAttribs->setAttribute("font-size", "1");
	} else {
		size_t nLinesInCell = textInAreaList.size();
		for (int idxList = 0; idxList < nLinesInCell; idxList++)
		{
			Inkscape::XML::Node* newTextNode = textInAreaList[idxList].ptextNode->parent()->duplicate(spDoc->getReprDoc());
			newTextNode->setAttribute("transform", sp_svg_transform_write(textInAreaList[idxList].affine));
			Inkscape::XML::Node* child = newTextNode->firstChild();
			while(child)
			{
				Inkscape::XML::Node* nextChild = child->next();
				child->parent()->removeChild(child);
				child = nextChild;
			}
			textInAreaList[idxList].ptextNode->parent()->removeChild(textInAreaList[idxList].ptextNode);

			// create new representation
			newTextNode->appendChild(textInAreaList[idxList].ptextNode);
			nodeTextAttribs->addChild(newTextNode, nodeCellRect);
		}
	}

	nodeCellIdx->appendChild(nodeTextAttribs);

	return nodeCellIdx;
}

Inkscape::XML::Node* TableDefenition::render(SvgBuilder *builder, Geom::Affine aff)
{
	Inkscape::XML::Node* nodeTable = builder->createElement("svg:g");
	nodeTable->setAttribute("class", "table");

	Inkscape::XML::Node* nodeTableRect = builder->createElement("svg:rect");
	nodeTableRect->setAttribute("x", doubleToCss(x).c_str());
	nodeTableRect->setAttribute("y", doubleToCss(y).c_str());
	nodeTableRect->setAttribute("width", doubleToCss(width).c_str());
	nodeTableRect->setAttribute("height", doubleToCss(height).c_str());
	nodeTableRect->setAttribute("fill", "none");
	// g.textarea rect must set fill="none" or (for example) fill="#fff000".
	// Currently it uses inline style which is not correct (the editor doesn't allow this rect have style)
	//nodeTableRect->setAttribute("stroke-width", "0");
	nodeTable->appendChild(nodeTableRect);

	Inkscape::XML::Node* nodeBorders = builder->createElement("svg:g");
	nodeBorders->setAttribute("class", "table-borders");

	for (int rowIdx = 0; rowIdx < countRow; rowIdx++)
	{
		Inkscape::XML::Node* nodeRow = builder->createElement("svg:g");
		std::string rowCalsses("table-row index-"); rowCalsses.append(std::to_string(rowIdx));
		nodeRow->setAttribute("class", rowCalsses.c_str());

		nodeTable->appendChild(nodeRow);

		for (int colIdx = 0; colIdx < countCol; colIdx++)
		{
			TableCell* cell = getCell(colIdx, rowIdx);
			nodeRow->appendChild(cellRender(builder, colIdx, rowIdx, aff));
			Inkscape::XML::Node* leftLine = getLeftBorder(builder, colIdx, rowIdx, aff);
			if (leftLine && (! isHidCell(colIdx, rowIdx)))
				nodeBorders->appendChild(leftLine);

			Inkscape::XML::Node* topLine = getTopBorder(builder, colIdx, rowIdx, aff);
			if (topLine && (! isHidCell(colIdx, rowIdx)))
				nodeBorders->appendChild(topLine);

			if (rowIdx == (countRow - 1))
			{
				Inkscape::XML::Node* bottomLine = nullptr;
				if (isHidCell(colIdx, rowIdx))
				{
					if (colIdx == cell->maxCol)
						bottomLine = getBottomBorder(builder, cell->maxCol, cell->maxRow, aff);
				} else {
					bottomLine = getBottomBorder(builder, colIdx, rowIdx, aff);
				}
				if (bottomLine)
					nodeBorders->appendChild(bottomLine);
			}

			if (colIdx == (countCol- 1))
			{
				Inkscape::XML::Node* rightLine = nullptr;
				if (isHidCell(colIdx, rowIdx))
				{
					if (rowIdx == cell->maxRow)
							rightLine = getRightBorder(builder, cell->maxCol, cell->maxRow, aff);
				} else {
					rightLine = getRightBorder(builder, colIdx, rowIdx, aff);
				}
				if (rightLine)
					nodeBorders->appendChild(rightLine);
			}
		}
	}

	nodeTable->appendChild(nodeBorders);
	builder->removeNodesByTextPositionList();

	return nodeTable;
}

TableCell* TableDefenition::getCell(int xIdx, int yIdx)
{
	return &_cells[yIdx * countCol + xIdx];
}

void TableDefenition::setStroke(int xIdx, int yIdx, TabLine *topLine, TabLine *bottomLine, TabLine *leftLine, TabLine *rightLine)
{
	TableCell *cell = getCell(xIdx, yIdx);
	cell->topLine = topLine;
	cell->bottomLine = bottomLine;
	cell->leftLine = leftLine;
	cell->rightLine = rightLine;
}

void TableDefenition::setVertex(int xIdx, int yIdx, double xStart, double yStart, double xEnd, double yEnd)
{
	TableCell *cell = getCell(xIdx, yIdx);
	cell->x = xStart;
	cell->width = xEnd - xStart;
	cell->y = yStart;
	cell->height = yEnd - yStart;
}

void TableDefenition::setRect(int col, int row, TabRect* rect)
{
	TableCell* cell = getCell(col, row);
	cell->rect = rect;
}

void TableDefenition::setMergeIdx(int col1, int row1, int mergeIdx)
{
	TableCell* cell = getCell(col1, row1);
	cell->mergeIdx = mergeIdx;
}

void TableDefenition::setMergeIdx(int col1, int row1, int col2, int row2, int mergeIdx)
{
	TableCell* cellMax = getCell(col1, row1);
	for(int colIdx = col1; colIdx <= col2; colIdx++)
	{
		for(int rowIdx = row1; rowIdx <= row2; rowIdx++)
		{
			TableCell* cell = getCell(colIdx, rowIdx);
			if (colIdx == col1 && rowIdx == row1)
			{
				cell->isMax = true;
			} else
			{
				cell->maxCol = col1;
				cell->maxRow = row1;
				cell->cellMax = cellMax;
				cell->isMax = false;
			}
			cell->mergeIdx = mergeIdx;
		}
	}
}

