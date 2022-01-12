/*
 * TableRegion.h
 *
 *  Created on: Dec 28, 2021
 *      Author: sergey
 */

#ifndef SRC_EXTENSION_INTERNAL_PDFINPUT_TABLEREGION_H_
#define SRC_EXTENSION_INTERNAL_PDFINPUT_TABLEREGION_H_

#include "TableDefenition.h"
#include "sp-path.h"
#include <exception>

struct ExceptionFillPatternDetected : public std::exception
{
	const char * what () const throw ()
    {
    	return "Fill pattern detected. Probaly is not table";
    }
};

class TableRegion
{
private:

	double x1, x2, y1, y2;
	SvgBuilder *_builder;
	SPDocument* spDoc;
	bool _isTable;
	TableDefenition* tableDef;
public:
	std::vector<TabLine*> lines;
	std::vector<TabRect*> rects;
	TableRegion(SvgBuilder *builder) :
		_builder(builder),
		_isTable(true),
		tableDef(nullptr),
		x1(1e5), // should bee enormous big
		x2(0),
		y1(1e5),
		y2(0)
	{
		spDoc = _builder->getSpDocument();
	}
	~TableRegion();

	TabLine* searchByPoint(double xMediane, double yMediane, bool isVerticale);
	TabRect* matchRect(double xStart, double yStart, double xEnd, double yEnd);
	bool recIntersectLine(Geom::Rect rect);
	Geom::Rect getBBox();
	double getAreaSize();

	bool addLine(Inkscape::XML::Node* node);

	bool isTable(){	return _isTable;	}
	bool buildKnote(SvgBuilder *builder);
	bool checkTableLimits();
	Inkscape::XML::Node* render(SvgBuilder *builder, Geom::Affine aff);

	std::vector<Inkscape::XML::Node*> getUnsupportedTextInTable() { return tableDef->getUnsupportedTextInTable(); }
};
typedef std::vector<TableRegion*> TableList;

#endif /* SRC_EXTENSION_INTERNAL_PDFINPUT_TABLEREGION_H_ */
