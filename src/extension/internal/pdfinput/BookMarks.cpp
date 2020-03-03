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


AdobeTextFrame::AdobeTextFrame(Json::Value jsonFrame) :
	top(0),
	left(0),
	height(0),
	width(0)
{
	ok = true;

	linkX = jsonFrame.get("x", Json::intValue).asInt(); // todo:: check type of value before
	linkY = jsonFrame.get("y", Json::intValue).asInt(); // todo:: check type of value before

	if (! jsonFrame.isMember("title"))
	{
		ok = false;
		return;
	}

	std::string frameInnerDataStr = jsonFrame.get("title", Json::nullValue).asString();
	Json::Value frameInnerDataJson;
	Json::Reader reader;
	const bool parsed = reader.parse(frameInnerDataStr.c_str() , frameInnerDataJson, false );
	if (! parsed)
	{
		ok = false;
		return;
	}

	Json::Value::Members keys = frameInnerDataJson.getMemberNames();
	if (keys.size() > 0)
	{
		if (strstr(keys[0].c_str(), "Frame_"))
		{
			Json::Value frameData = frameInnerDataJson.get(keys[0].c_str(), Json::nullValue);

			Json::Value leftJson = frameData.get("left", Json::nullValue);
			if (! leftJson.isNumeric())
			{
				ok = false;
				return;
			}
			left = leftJson.asInt();

			Json::Value topJson = frameData.get("top", Json::nullValue);
			if (! topJson.isNumeric())
			{
				ok = false;
				return;
			}
			top = topJson.asInt();

			Json::Value widthJson = frameData.get("width", Json::nullValue);
			if (! widthJson.isNumeric())
			{
				ok = false;
				return;
			}
			width = widthJson.asInt();

			Json::Value heightJson = frameData.get("height", Json::nullValue);
			if (! heightJson.isNumeric())
			{
				ok = false;
				return;
			}
			height = heightJson.asInt();

			Json::Value paragraphsJson = jsonFrame.get("kinds", Json::nullValue);
			if (! paragraphsJson.isArray())
			{
				ok = false;
				return;
			}

			for(int paragraphIdx = 0; paragraphIdx < paragraphsJson.size(); ++paragraphIdx )
			{
				AdobeParagraph* paragraph = new AdobeParagraph(paragraphsJson[paragraphIdx], this); //clean up
				paragraphs.push_back(paragraph);
			}

		}
	}
}

//============================================================================
AdobeParagraph::AdobeParagraph(Json::Value paragraph, AdobeTextFrame* frame) :
		parent(frame)
{
	ok = true;

	Json::Value xJson = getItemVal(paragraph, "x");
	Json::Value yJson = getItemVal(paragraph, "y");
	Json::Value alignJson = getItemVal(paragraph, "align");
	Json::Value startJson = getItemVal(paragraph, "start");
	Json::Value endJson = getItemVal(paragraph, "End");
	if (!(xJson.isNumeric() && yJson.isNumeric() && startJson.isNumeric() && endJson.isNumeric() && alignJson.isString()))
	{
		ok = false;
		return;
	}
	x = xJson.asInt();
	y = yJson.asInt();
	start = startJson.asInt();
	end = endJson.asInt();
	setAlign(alignJson.asString().c_str());
}

const char* AdobeParagraph::getAlignName()
{
	switch(align)
	{
	case ALGN_UNKNOWN: return "unknown";
	case ALGN_CENTER: return "center";
	case ALGN_LEFT: return "left";
	case ALGN_RIGHT: return "right";
	}
	return "unknown";
}

void AdobeParagraph::setAlign(const char* alignStr)
{
	align = ALGN_UNKNOWN;
	if (strcasecmp(alignStr, "center") == 0)
		align = ALGN_CENTER;
	if (strcasecmp(alignStr, "left") == 0)
		align = ALGN_LEFT;
	if (strcasecmp(alignStr, "right") == 0)
		align = ALGN_RIGHT;
}

Json::Value AdobeParagraph::getItemVal(Json::Value paragraph, const char* valName)
{
	if (paragraph.isMember(valName))
	{
		Json::Value result = paragraph.get(valName, Json::Value::null);
		return result;
	}

	Json::Value val = paragraph.get("title", Json::Value::null);
	if (! val.isString())
		return Json::nullValue;

	Json::Value root;
	Json::Reader reader;
	std::string titleVal = val.asString();
	const bool parsed = reader.parse(titleVal.c_str() , root, false );
	if (! parsed)
		return Json::nullValue;

	if (! root.isMember("paragraph"))
		return Json::nullValue;
	Json::Value tspanObj = root.get("paragraph", Json::Value::null);
	if (! tspanObj.isObject())
		return Json::nullValue;;

	if (tspanObj.isMember(valName))
	{
		Json::Value result = tspanObj.get(valName, Json::Value::null);
		return result;
	}

	return Json::nullValue;
}

//============================================================================

AdobeExtraData::AdobeExtraData(const char* fileName)
{
    //root;   // will contains the root value after parsing.
    Json::Reader reader;
    std::ifstream bookMarksList(fileName, std::ifstream::binary);
    Json::Value root;
    ok = reader.parse( bookMarksList, root, false );
    if (! ok) return;

    ok = (root.isArray());
    if (! ok) return;

    Json::Value item;
    ok = false;
    for(int i = 0; i < root.size(); i++)
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
    const Json::Value& storyListJson = item.get("kinds", Json::nullValue);
    ok = storyListJson.isArray();
    if (! ok) return;

    for(int i = 0; i < storyListJson.size(); ++i)
    {
    	if (! storyListJson[i].isMember("kinds"))
    		 continue;
    	const Json::Value& frameListJson = storyListJson[i].get("kinds", Json::nullValue);
    	if (! frameListJson.isArray())
    	    continue;
        for(int f = 0; f < frameListJson.size(); ++f)
        {
        	if (! frameListJson[f].isMember("kinds")) // no need frames without paragraphs
        	    continue;

        	AdobeTextFrame* frame = new AdobeTextFrame(frameListJson[f]); //todo: cleanup memory
        	frames.push_back(frame);
        }
    }
}

Geom::Rect AdobeExtraData::getFrameRect(int i, Geom::Rect svgDimension)
{
	AdobeTextFrame* frame = frames[i];

	double left = frame->getLeft();
	double top = frame->getTop();

	const double w = frame->getWidth();
	const double h = frame->getHeight();

	left += svgDimension.width()/2;
	top += svgDimension.height()/2;
	return Geom::Rect(left, top, left + w, top + h);
}

void AdobeExtraData::MergeWithSvgBuilder(Inkscape::Extension::Internal::SvgBuilder* builder)
{
	NodeList listOfText;
	builder->getNodeListByTag("svg:text", &listOfText);


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
		Geom::Rect r = getFrameRect(i, dimension) * rootAffine;

		for(auto textNode : listOfText)
		{
			SPTSpan* textSpNode = (SPTSpan*)spDoc->getObjectById(textNode->attribute("id"));

			Geom::OptRect textRect = textSpNode->documentGeometricBounds();
			Geom::Rect textBBox = textRect.get();

			if (r.intersects(textBBox))
			{
				NodeList listOfTSpan;
				builder->getNodeListByTag("svg:tspan", &listOfTSpan/*, textNode*/);

				for(auto paragraf : getParagraphList(i)){
					double destToPoint = -1;
					Inkscape::XML::Node* resultNode = nullptr;
					Geom::Point linkPoint = paragraf->getLinkPoint() * rootAffine;
					for(auto tspanNode : listOfTSpan)
					{
						SPTSpan* tspanSpNode = (SPTSpan*)spDoc->getObjectById(tspanNode->attribute("id"));

						Geom::OptRect tspanRect = tspanSpNode->documentGeometricBounds();
						Geom::Rect tspanBBox = tspanRect.get();

						double xCatet = tspanBBox[Geom::X][0] - linkPoint.x();
						double yCatet = tspanBBox[Geom::Y][0] - linkPoint.y();
						double sqrDest = xCatet * xCatet + yCatet * yCatet;
						if (sqrDest < destToPoint || destToPoint < 0)
						{
							destToPoint = sqrDest;
							resultNode = tspanNode;
						}
					}
					if (resultNode)
						resultNode->setAttribute("data-align", paragraf->getAlignName());
				}
				break;
			}
		}
	}
}
