/*
 * PagesCatalog.cpp
 *
 *  Created on: 2 февр. 2020 г.
 *      Author: sergey
 */

#include "PagesCatalog.h"

PagesCatalog::PagesCatalog(XRef* docXRef)
{
	const int rootGen = docXRef->getRootGen();
	const int rootNum = docXRef->getRootNum();
	Object rootObj = docXRef->fetch(rootNum, rootGen);

	if (rootObj.isDict())
	{
		Dict* rootDict = rootObj.getDict();
		const int rootDictLen = rootDict->getLength();
		if (rootDict->hasKey("Pages"))
		{
			Object pagesObj = rootDict->lookup("Pages");

			addPages(&pagesObj);
		}
	}
}

/**
 * Can receive Pages dictionary or Kids array of Pages Dictionary, or Catalog object
 */
bool PagesCatalog::addPages(Object* obj)
{
	switch(obj->getType())
	{
	case ObjType::objDict : // we are thinking it is Pages obect
	{
		Dict* dictPages = obj->getDict();
		if (dictPages->hasKey("Kids"))
		{
			Object arrayObj = dictPages->lookup("Kids");
			if (arrayObj.isArray())
				return addArray(arrayObj.getArray());
			else return false;

		} else
			return false;
		break;
	}
	case ObjType::objArray : // we are thinking it is Kids array of Pages object
		return addArray(obj->getArray());
	default :
		return false;
	}

	return true;
}

bool PagesCatalog::addArray(Array* pagesArray)
{
	const int arrLen = pagesArray->getLength();

	for(int i = 0; i < arrLen; i++)
	{
		Object page = pagesArray->getNF(i).copy();

		if (page.isRef())
			pagesRef.push_back(page.getRef());
		else
			return false;
	}
}
