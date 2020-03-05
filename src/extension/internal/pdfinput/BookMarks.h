/*
 * BookMarks.h
 *
 *  Created on: 15 февр. 2020 г.
 *      Author: sergey
 */

#ifndef SRC_EXTENSION_INTERNAL_PDFINPUT_BOOKMARKS_H_
#define SRC_EXTENSION_INTERNAL_PDFINPUT_BOOKMARKS_H_

#include <jsoncpp/json/json.h>
#include "svg-builder.h"
#include "sp-root.h"

class AdobeParagraph;

struct TextRange
{
	int start;
	int end;
};

class TSpanSorter
{
public:
	TSpanSorter(const NodeList& tspanList);
private:
	const NodeList* mTspanList;
	std::vector<TextRange> textRanges;
};

class AdobeTextFrame
{
public:
	AdobeTextFrame(Json::Value jsonFrame);
	int getLeft() const {return left; };
	int getTop() const {return top; };
	int getHeight() const {return height; };
	int getWidth() const {return width; };
	std::vector<AdobeParagraph*> getParagraphs() const { return paragraphs; };
private:

	std::vector<AdobeParagraph*> paragraphs;

	int linkX; //position of PDF link
	int linkY; //position of PDF link
	int left;
	int top;
	int height;
	int width;
	bool ok;
};

class AdobeParagraph
{
public:
	enum {
		ALGN_UNKNOWN,
		ALGN_LEFT,
		ALGN_RIGHT,
		ALGN_CENTER,
	};
	AdobeParagraph(Json::Value paragraf, AdobeTextFrame* frame = nullptr);
	Geom::Point getLinkPoint() {return Geom::Point(x, y); };
	const char* getAlignName();
	const char* getAnchoreName();
	int calcXByAnchore(const Geom::Rect& frameRect);
private:
	static Json::Value getItemVal(Json::Value paragraph, const char* valName);
	void setAlign(const char* alignStr);

	int x;
	int y;
	int start;
	int end;
	uint align;

	AdobeTextFrame* parent;
	bool ok;
};

class AdobeExtraData {
public:
	AdobeExtraData(const char* fileName);
	int getCount() const { return frames.size(); };
	Geom::Rect getFrameRect(int i, Geom::Rect svgDimension);
	std::vector<AdobeParagraph*> getParagraphList(int frameIdx) {return frames[frameIdx]->getParagraphs() ; };
	void MergeWithSvgBuilder(Inkscape::Extension::Internal::SvgBuilder* builder);
	bool isOk() const { return ok; };
private:
	bool ok;
	std::vector<AdobeTextFrame*> frames;
};

#endif /* SRC_EXTENSION_INTERNAL_PDFINPUT_BOOKMARKS_H_ */
