/*
 * TextTableDetector.cpp
 *
 *  Created on: Dec 22, 2021
 *      Author: sergey
 */

#include "TextTableDetector.h"
#include "svg/svg.h"
#include "sp-tspan.h"
#include <vector>
#include "document.h"
#include "xml/repr.h"

TextTableDetector::TextTableDetector(SvgBuilder* _builder) :
builder(_builder)
{
	spDoc = _builder->getSpDocument();
}

TextTableDetector::~TextTableDetector() {
	// TODO Auto-generated destructor stub
}

void TextTableDetector::addTspan(Inkscape::XML::Node* tspanNode)
{
	if (strcmp(tspanNode->name(), "svg:tspan")) return;

	tspanList.push_back(tspanNode);

	Inkscape::XML::Node* textNode = tspanNode->parent();

	SPCSSAttr *textStyle = sp_repr_css_attr(textNode, "style");

	std::vector<SVGLength> charactersX = sp_svg_length_list_read(tspanNode->attribute("data-x"));
	std::vector<SVGLength> charactersDx = sp_svg_length_list_read(tspanNode->attribute("dx"));
	double averageWidth;
	sp_svg_number_read_d(tspanNode->attribute("data-dx"), &averageWidth);
	double endX;
	sp_svg_number_read_d(tspanNode->attribute("data-endX"), &endX);
	SPTSpan* spTspanNode = SP_TSPAN(spDoc->getObjectByRepr(tspanNode));
	Geom::Affine relativeTransform = spTspanNode->getRelativeTransform(SP_ITEM(spDoc->getRoot()));

	bool startNewWord = true;
	for(int xIdx = 0; xIdx < charactersX.size(); xIdx++)
	{

	}
}

