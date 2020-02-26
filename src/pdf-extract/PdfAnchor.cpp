/*
 * PdfAnchorsTree.cpp
 *
 *  Created on: 4 февр. 2020 г.
 *      Author: sergey
 */

#include "PdfAnchor.h"

#include "tools.h"

PdfAnchor::PdfAnchor(Object rootAnchor) :
	OutlineItem(rootAnchor.getDict(), rootAnchor.getDict()->getXRef())
{
	anchor = rootAnchor;
};

PdfAnchor* PdfAnchor::next()
{
	PdfAnchor* result = nullptr;

	if(anchor.isDict())
	{
		Dict* anchorDict = anchor.getDict();
		if(anchorDict->hasKey("Next"))
		{
			Object obj;
			anchorDict->lookup("Next", &obj);
			result = new PdfAnchor(obj);
		}
	}

	return result;
}

PdfAnchor* PdfAnchor::firstChild()
{
	PdfAnchor* result = nullptr;

	if(anchor.isDict())
	{
		Dict* anchorDict = anchor.getDict();
		if(anchorDict->hasKey("First"))
		{
			Object obj;
			anchorDict->lookup("First", &obj);
			result = new PdfAnchor(obj);
		}
	}

	return result;
}

int PdfAnchor::getKind()
{
	return this->getAction()->getKind();
}

char* PdfAnchor::getDestName()
{
	Object nameObj;
	getObjectByPath("A/D", &anchor, &nameObj);
	if (nameObj.isString())
	{
		return nameObj.getString()->getCString();
	}

	return nullptr;
}
