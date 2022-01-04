/*
 * TextTableDetector.h
 *
 *  Created on: Dec 22, 2021
 *      Author: sergey
 */

#ifndef SRC_EXTENSION_INTERNAL_PDFINPUT_TEXTTABLEDETECTOR_H_
#define SRC_EXTENSION_INTERNAL_PDFINPUT_TEXTTABLEDETECTOR_H_

#include "svg-builder.h"
#include "xml/node.h"
#include "svg-builder.h"

using namespace Inkscape::Extension::Internal;

class TextTableDetector {
private:
	NodeList tspanList;
	SvgBuilder* builder;
	SPDocument* spDoc;
public:
	TextTableDetector(SvgBuilder* _builder);
	virtual ~TextTableDetector();

	void addTspan(Inkscape::XML::Node* tspanNode);

};

#endif /* SRC_EXTENSION_INTERNAL_PDFINPUT_TEXTTABLEDETECTOR_H_ */
