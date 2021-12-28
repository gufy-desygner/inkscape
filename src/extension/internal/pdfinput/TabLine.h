/*
 * TabLine.h
 *
 *  Created on: Dec 28, 2021
 *      Author: sergey
 */

#ifndef SRC_EXTENSION_INTERNAL_PDFINPUT_TABLINE_H_
#define SRC_EXTENSION_INTERNAL_PDFINPUT_TABLINE_H_

#include "TableRecognizeCommon.h"
#include "2geom/2geom.h"
#include "display/curve.h"
#include "xml/node.h"
#include "svg-builder.h"


class TabLine {
	private:
		bool isVert;
		bool lookLikeTab;
		SPCurve* spCurve;
	public:
		Inkscape::XML::Node* node;
		Geom::Affine affineToMainNode;
		double x1, x2, y1, y2;
		TabLine(Inkscape::XML::Node* node, const Geom::Curve& curve, SPDocument* spDoc);
		bool intersectRect(Geom::Rect rect);
		Inkscape::XML::Node* searchByPoint(double xMediane, double yMediane, bool isVerticale);
		size_t curveSegmentsCount() { return spCurve->get_segment_count(); };


		bool isTableLine() { return  lookLikeTab; }
		bool isVertical() { return approxEqual(x1, x2); }
};

//should be classs function
SPCSSAttr* adjustStroke(TabLine* tabLine);

#endif /* SRC_EXTENSION_INTERNAL_PDFINPUT_TABLINE_H_ */
