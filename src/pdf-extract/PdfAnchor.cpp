/*
 * PdfAnchorsTree.cpp
 *
 *  Created on: 4 февр. 2020 г.
 *      Author: sergey
 */

#include "PdfAnchor.h"

#include "tools.h"

PdfAnchor::PdfAnchor(Object rootAnchor) :
	OutlineItem(rootAnchor.getDict(), rootAnchor.getRefNum(), nullptr, rootAnchor.getDict()->getXRef())
{
	anchor = rootAnchor.copy();
};

PdfAnchor* PdfAnchor::next()
{
	PdfAnchor* result = nullptr;

	if(anchor.isDict())
	{
		Dict* anchorDict = anchor.getDict();
		if(anchorDict->hasKey("Next"))
		{
			//Object obj = ;
			result = new PdfAnchor(anchorDict->lookup("Next"));
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
			result = new PdfAnchor(anchorDict->lookup("First"));
		}
	}

	return result;
}

int PdfAnchor::getKind()
{
	return this->getAction()->getKind();
}

const char* PdfAnchor::getDestName()
{
	Object nameObj;
	getObjectByPath("A/D", &anchor, &nameObj);
	if (nameObj.isString())
	{
		return nameObj.getString()->c_str();
	}

	return nullptr;
}
