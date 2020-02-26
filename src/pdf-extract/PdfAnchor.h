/*
 * PdfAnchorsTree.h
 *
 *  Created on: 4 февр. 2020 г.
 *      Author: sergey
 */

#ifndef SUBPROJECTS__PDF_EXTRACT_PDFANCHORSTREE_H_
#define SUBPROJECTS__PDF_EXTRACT_PDFANCHORSTREE_H_

#include "tools.h"
#include "Outline.h"
#include "Link.h"

class PdfAnchor: public OutlineItem {
public:
	PdfAnchor(Object rootAnchor);
	PdfAnchor* next();
	PdfAnchor* firstChild();
	int getKind();
	char* getDestName();
private:
	Object anchor;
};

#endif /* SUBPROJECTS__PDF_EXTRACT_PDFANCHORSTREE_H_ */
