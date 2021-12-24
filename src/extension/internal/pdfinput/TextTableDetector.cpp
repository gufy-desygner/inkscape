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

	std::vector<SVGLength> charactersX = sp_svg_length_list_read(tspanNode->attribute("data-x"));
	SPTSpan* spTspanNode = SP_TSPAN(spDoc->getObjectByRepr(tspanNode));
	Geom::Affine relativeTransform = spTspanNode->getRelativeTransform(spDoc->getRoot());

}

