/*
 * BookMarks.cpp
 *
 *  Created on: 15 февр. 2020 г.
 *      Author: sergey
 */

#include "BookMarks.h"
#include <iostream>
#include <fstream>
#include "svg-builder.h"
#include "document.h"
#include "sp-item-group.h"
#include "text-editing.h"
#include "xml/repr.h"
#include "sp-tspan.h"

BookMarks::BookMarks(const char* fileName)
{
    //root;   // will contains the root value after parsing.
    Json::Reader reader;
    std::ifstream bookMarksList(fileName, std::ifstream::binary);
    ok = reader.parse( bookMarksList, root, false );
    if (! ok) return;

    ok = (root.isArray());
    if (! ok) return;

    Json::Value item;
    ok = false;
    for(int i = 0; i< root.size(); i++)
    {
    	item = root[i];
    	if (! item.isObject()) return;
    	if (item.isMember("title"))
    	{
    		std::string title = item.get("title", "" ).asString();
    		if (title.compare("WeBrandExport") == 0) {
    			ok = true;
    			break;
    		}
    	}
    }

    if (! ok) return;
    ok = item.isMember("kinds");
    if (! ok) return;

    weBrandBooks = item.get("kinds", "");
    ok = weBrandBooks.isArray();
}

Json::Value BookMarks::getItemVal(int i, const char* valName) const
{
	Json::Value bookMark = weBrandBooks[i];
	if (bookMark.isMember(valName))
	{
		Json::Value result = bookMark.get(valName, Json::Value::null);
		return result;
	}

	Json::Value val = bookMark.get("title", Json::Value::null);
	if (! val.isString())
		return Json::nullValue;

	Json::Value root;
	Json::Reader reader;
	std::string titleVal = val.asString();
	const bool parsed = reader.parse(titleVal.c_str() , root, false );
	if (! parsed)
		return Json::nullValue;

	if (! root.isMember("tspan"))
		return Json::nullValue;
	Json::Value tspanObj = root.get("tspan", Json::Value::null);
	if (! tspanObj.isObject())
		return Json::nullValue;;

	if (tspanObj.isMember(valName))
	{
		Json::Value result = tspanObj.get(valName, Json::Value::null);
		return result;
	}

	return Json::nullValue;
}

std::string BookMarks::getItemValStr(int i, const char* valName) const
{
	Json::Value rawVal = getItemVal(i, valName);
	if (! rawVal.isString())
		return nullptr;

	return rawVal.asString();
}

double BookMarks::getItemValD(int i, const char* valName, double defVal) const
{
	Json::Value bookMark = weBrandBooks[i];
	if (bookMark.isMember(valName))
	{
		Json::Value val = bookMark.get(valName, Json::Value::null);
		if (! val.isNumeric())
			return defVal;
		return val.asDouble();
	}

	Json::Value val = bookMark.get("title", Json::Value::null);
	if (! val.isString())
		return defVal;

	Json::Value root;
	Json::Reader reader;
	std::string titleVal = val.asString();
	const bool parsed = reader.parse(titleVal.c_str() , root, false );
	if (! parsed)
		return defVal;

	if (! root.isMember("tspan"))
		return defVal;
	Json::Value tspanObj = root.get("tspan", Json::Value::null);
	if (! tspanObj.isObject())
		return defVal;

	if (tspanObj.isMember(valName))
	{
		Json::Value val = tspanObj.get(valName, Json::Value::null);
		if (! val.isNumeric())
			return defVal;
		return val.asDouble();
	}

	return defVal;
}

void BookMarks::MergeWithSvgBuilder(Inkscape::Extension::Internal::SvgBuilder* builder)
{
	NodeList listOfTSpan;
	builder->getNodeListByTag("svg:tspan", &listOfTSpan);


	SPDocument* spDoc = builder->getSpDocument();
	SPRoot* spRoot = spDoc->getRoot();
	te_update_layout_now_recursive(spRoot);

	Inkscape::XML::Node* mainGroup = builder->getMainNode();
	const char* mainGId = mainGroup->attribute("id");
	SPItem* groupSpMain = (SPItem*)spDoc->getObjectById(mainGId);
	Geom::Affine rootAffine = groupSpMain->transform;
	Geom::Rect dimension = spDoc->getViewBox();
	Geom::Affine revertTransform = rootAffine.inverse();
	dimension = dimension * revertTransform;

	for(int i = 0; i < getCount(); i++)
	{
		Json::Value book = getItem(i);

		double x = getItemValD(i, "x", -1);
		double y = getItemValD(i, "y", -1);
		double left = getItemValD(i, "left", 0);
		double top = getItemValD(i, "top", 0);
		double w = getItemValD(i, "width", 0);
		double h = getItemValD(i, "height", 0);

		left += dimension.width()/2;
		top += dimension.height()/2;
		Geom::Rect r1(left, top, left + w, top + h);
		Geom::Rect r = r1 * rootAffine;
		Geom::Point linkPoint(x, y);
		linkPoint = linkPoint * rootAffine;

		double destToPoint = -1;
		Inkscape::XML::Node* resultNode = nullptr;
		for(auto tspanNode : listOfTSpan)
		{
			SPTSpan* tspanSpNode = (SPTSpan*)spDoc->getObjectById(tspanNode->attribute("id"));

			Geom::OptRect tspanRect = tspanSpNode->documentGeometricBounds();
			Geom::Rect tspanBBox = tspanRect.get();
			//if (tspanRect.intersects(r))
			if (r.intersects(tspanBBox))
			{
				double xCatet = tspanBBox[Geom::X][0] - linkPoint.x();
				double yCatet = tspanBBox[Geom::Y][0] - linkPoint.y();
				double sqrDest = xCatet * xCatet + yCatet * yCatet;
				if (sqrDest < destToPoint || destToPoint < 0)
				{
					destToPoint = sqrDest;
					resultNode = tspanNode;
				}
			}

		}
		if (resultNode) {
			std::string alignStr = getItemValStr(i, "align");
			if (alignStr.compare("center") == 0)
				resultNode->setAttribute("data-align", "middle");
			if (alignStr.compare("right") == 0)
				resultNode->setAttribute("data-align", "right");
		}
	}
}
