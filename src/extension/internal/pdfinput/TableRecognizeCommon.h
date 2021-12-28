/*
 * TableRecogniseCommon.h
 *
 *  Created on: Dec 28, 2021
 *      Author: sergey
 */

#ifndef SRC_EXTENSION_INTERNAL_PDFINPUT_TABLERECOGNIZECOMMON_H_
#define SRC_EXTENSION_INTERNAL_PDFINPUT_TABLERECOGNIZECOMMON_H_

#include "2geom/2geom.h"
#include "svg/svg.h"

bool approxEqual(const float x, const float y, const float epsilon = 0.05f);
double rectIntersect(const Geom::Rect& main, const Geom::Rect& kind);
std::string doubleToCss(double num);

#endif /* SRC_EXTENSION_INTERNAL_PDFINPUT_TABLERECOGNIZECOMMON_H_ */
