 /*
 * Native PDF import using libpoppler.
 * 
 * Authors:
 *   miklos erdelyi
 *   Jon A. Cruz <jon@joncruz.org>
 *
 * Copyright (C) 2007 Authors
 *
 * Released under GNU GPL, read the file 'COPYING' for more information
 *
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <string> 
#include <math.h>
#include <cairo.h>
#include "display/drawing-context.h"
#include "display/drawing.h"


//#define HAVE_POPPLER
#ifdef HAVE_POPPLER

#include <png.h>
#include "svg-builder.h"
#include "pdf-parser.h"
#include "shared_opt.h"


#include "document-private.h"
#include "xml/document.h"
#include "xml/node.h"
#include "xml/repr.h"
#include "svg/svg.h"
#include "svg/path-string.h"
#include "svg/css-ostringstream.h"
#include "svg/svg-color.h"
#include "color.h"
#include "util/units.h"
#include "io/stringstream.h"
#include "io/base64stream.h"
#include "display/nr-filter-utils.h"
#include "libnrtype/font-instance.h"
#include "shared_opt.h"
#include "sp-clippath.h"

#include "Function.h"
#include "GfxState.h"
#include "GfxFont.h"
#include "Stream.h"
#include "Page.h"
#include "UnicodeMap.h"
#include "GlobalParams.h"
#include "png-merge.h"
#include "xml/sp-css-attr.h"
#include "sp-path.h"
#include "display/curve.h"
#include "2geom/path.h"
#include "2geom/path-intersection.h"
#include "text-editing.h"
//#include "helper/png-write.h"
//#include "display/cairo-utils.h"


namespace Inkscape {
namespace Extension {
namespace Internal {

//#define IFTRACE(_code)  _code
#define IFTRACE(_code)

#define TRACE(_args) IFTRACE(g_print _args)

/**
 * \struct SvgTransparencyGroup
 * \brief Holds information about a PDF transparency group
 */
struct SvgTransparencyGroup {
    double bbox[6]; // TODO should this be 4?
    Inkscape::XML::Node *container;

    bool isolated;
    bool knockout;
    bool for_softmask;

    SvgTransparencyGroup *next;
};

/**
 * \class SvgBuilder
 * 
 */

SvgBuilder::SvgBuilder(SPDocument *document, gchar *docname, XRef *xref)
{
    _is_top_level = true;
    _countOfImages = 0;
    _doc = document;
    _docname = docname;
    _xref = xref;
    _xml_doc = _doc->getReprDoc();
    _container = _root = _doc->rroot;
    _root->setAttribute("xml:space", "preserve");
    _init();

    // Set default preference settings
    _preferences = _xml_doc->createElement("svgbuilder:prefs");
    _preferences->setAttribute("embedImages", "1");
    _preferences->setAttribute("localFonts", "1");
    glyphs_for_clips = g_ptr_array_new();
}

SvgBuilder::SvgBuilder(SvgBuilder *parent, Inkscape::XML::Node *root) {
    _is_top_level = false;
    _doc = parent->_doc;
    _docname = parent->_docname;
    _xref = parent->_xref;
    _xml_doc = parent->_xml_doc;
    _preferences = parent->_preferences;
    _container = this->_root = root;
    glyphs_for_clips = g_ptr_array_new();
    _init();
}

SvgBuilder::~SvgBuilder() {
	g_ptr_array_free(glyphs_for_clips, true);
}

void SvgBuilder::_init() {
    _font_style = NULL;
    _current_font = NULL;
    _font_specification = NULL;
    _font_scaling = 1;
    _need_font_update = true;
    _in_text_object = false;
    _invalidated_style = true;
    _current_state = NULL;
    _width = 0;
    _height = 0;
    _countOfPath = 0;
    _countOfImages = 0;

    // Fill _availableFontNames (Bug LP #179589) (code cfr. FontLister)
    if (! sp_original_fonts_sh) {
    	std::vector<PangoFontFamily *> families;
		font_factory::Default()->GetUIFamilies(families);
		for ( std::vector<PangoFontFamily *>::iterator iter = families.begin();
			  iter != families.end(); ++iter ) {
			_availableFontNames.push_back(pango_font_family_get_name(*iter));
		}
    }

    _transp_group_stack = NULL;
    SvgGraphicsState initial_state;
    initial_state.softmask = NULL;
    initial_state.group_depth = 0;
    _state_stack.push_back(initial_state);
    _node_stack.push_back(_container);

    _ttm[0] = 1; _ttm[1] = 0; _ttm[2] = 0; _ttm[3] = 1; _ttm[4] = 0; _ttm[5] = 0;
    _ttm_is_set = false;
}

// compare all component except "translate"
static bool isCompatibleAffine(Geom::Affine firstAffine, Geom::Affine secondAffine) {
	for(int i = 0; i < 4; i++) {
		if (firstAffine[i] != secondAffine[i])
			return false;
	}
	return true;
}

// SVG:TEXT merger
// Merge text node and change TSPAN x,y
void SvgBuilder::mergeTextNodesToFirst(NodeList &listNodes)
{
	if (listNodes.size() == 0) return;
	// load first text node
	Inkscape::XML::Node *mainTextNode = listNodes[0];
	SPItem *mainSpText = (SPItem*)this->getSpDocument()->getObjectByRepr(mainTextNode);
	Geom::Affine mainAffine = mainSpText->transform;

	// process for each text node
	for(int i = 1; i < listNodes.size(); i++) {
		// load current text node
		Inkscape::XML::Node *currentTextNode = listNodes[i];
		SPItem *currentSpText = (SPItem*)this->getSpDocument()->getObjectByRepr(currentTextNode);
		Geom::Affine currentAffine = currentSpText->transform;

		bool styleIsEqualent = (0 == strcmp(mainTextNode->attribute("style"),
				currentTextNode->attribute("style")));
		if (isCompatibleAffine(mainAffine, currentAffine) && styleIsEqualent) {
			Inkscape::XML::Node *currentTspan = currentTextNode->firstChild();

			// scan TSPAN nodes
			while(currentTspan) {
				// if it no TSPAN = something is wrong - go to next text node
                // need check style too
				if (strcmp(currentTspan->name(), "svg:tspan") == 0) {
					// we must adjust position data of tspan for new text transform
					double adjX;
					if (! sp_repr_get_double(currentTspan, "x", &adjX)) adjX = 0;
					double adjY;
					if (! sp_repr_get_double(currentTspan, "y", &adjY)) adjY = 0;
					double adjEndX;
					if (! sp_repr_get_double(currentTspan, "data-endX", &adjEndX)) adjEndX = 0;
					Geom::Point adjPoint = Geom::Point(adjX, adjY);
					Geom::Point endPoint = Geom::Point(adjEndX, adjY);
					adjPoint *= currentAffine;
					adjPoint *= mainAffine.inverse();
					endPoint = (endPoint * currentAffine) * mainAffine.inverse();

					// adjast data-x attribute
					Glib::ustring strDataX;
					std::vector<SVGLength> data_x = sp_svg_length_list_read(currentTspan->attribute("data-x"));
					strDataX.clear();
					for(int i = 0; i < data_x.size(); i++) {
						if (data_x[i]._set) {
							Geom::Point adjDataX = Geom::Point(data_x[i].value, adjY);
							adjDataX = (adjDataX * currentAffine) * mainAffine.inverse();
							Inkscape::CSSOStringStream os_x;
							os_x << adjDataX.x();
							strDataX.append(os_x.str());
							strDataX.append(" ");
						}
					}
					currentTspan->setAttribute("data-x", strDataX.c_str());

					// save adjasted data
					sp_repr_set_svg_double(currentTspan, "data-endX", endPoint.x());
					sp_repr_set_svg_double(currentTspan, "x", adjPoint.x());
					sp_repr_set_svg_double(currentTspan, "y", adjPoint.y());

					// move it to the main text node
					mainTextNode->addChild(currentTspan->duplicate(currentTspan->document()), mainTextNode->lastChild());
					Inkscape::XML::Node *remNode = currentTspan;
					currentTspan = currentTspan->next();
					currentTextNode->removeChild(remNode);
				} else {
					break;
				}
			}

			// if was merged all TSPAN nodes
			if (! currentTspan) {
				currentTextNode->parent()->removeChild(currentTextNode);
				listNodes.erase(listNodes.begin() + i);
				i--;
			}
		} else {
			// if affine is not compatible we must start from other "main text node" (current node)
			// so start new merge transaction
			mainTextNode = currentTextNode;
			mainSpText = (SPItem*)this->getSpDocument()->getObjectByRepr(mainTextNode);
			mainAffine = mainSpText->transform;
		}
	}
}

static bool tspan_compare_position(Inkscape::XML::Node *first, Inkscape::XML::Node *second)
{
	double firstX;
	double firstY;
    double secondX;
	double secondY;
	static double textSize = 1;
	static Inkscape::XML::Node *parentNode = 0;
	if (first->parent() != parentNode) {
		parentNode = first->parent();
		SPCSSAttr *style = sp_repr_css_attr(parentNode, "style");
		gchar const *fntStrSize = sp_repr_css_property(style, "font-size", "1");
		textSize = g_ascii_strtod(fntStrSize, NULL);
		delete style;
	}

	if (! sp_repr_get_double(first,  "x", &firstX) ) firstX  = 0;
	if (! sp_repr_get_double(second, "x", &secondX)) secondX = 0;
	if (! sp_repr_get_double(first,  "y", &firstY) ) firstY  = 0;
	if (! sp_repr_get_double(second, "y", &secondY)) secondY = 0;

	//compare
	// round Y to 20% of font size and compare
	if (textSize == 0) textSize = 0.00001;
	if (fabs(firstY - secondY)/textSize < 0.2) {
		if (firstX == secondX) return false;
		else
			if (firstX < secondX) return true;
			else return false;
	} else {
		if (firstY < secondY) return true;
		else return false;
	}
}

int utf8_strlen(const char *str)
{
    int c,i,ix,q;
    for (q=0, i=0, ix=strlen(str); i < ix; i++, q++)
    {
        c = (unsigned char) str[i];
        if      (c>=0   && c<=127) i+=0;
        else if ((c & 0xE0) == 0xC0) i+=1;
        else if ((c & 0xF0) == 0xE0) i+=2;
        else if ((c & 0xF8) == 0xF0) i+=3;
        //else if (($c & 0xFC) == 0xF8) i+=4; // 111110bb //byte 5, unnecessary in 4 byte UTF-8
        //else if (($c & 0xFE) == 0xFC) i+=5; // 1111110b //byte 6, unnecessary in 4 byte UTF-8
        else return 0;//invalid utf8
    }
    return q;
}

void mergeTwoTspan(Inkscape::XML::Node *first, Inkscape::XML::Node *second)
{
	static Inkscape::XML::Node *lastPassedFirstTspan = 0;
	// todo: calculate averidge parmetrs DX and if current more -> this is space char.
	static int firstTspanCharMerged = 0;
	static double avrDxMerged = 0;

	double firstEndX;
	double secondX;
	double spaceSize; // posible variant -read it from font
	double wordSpace; // additional distantion betweene words
	static gdouble fntSize;

	gchar const *firstContent = first->firstChild()->content();
	gchar const *secondContent = second->firstChild()->content();

	if (lastPassedFirstTspan != first) {
		// calculate space size
		SPCSSAttr *style = sp_repr_css_attr(first->parent(), "style");
		gchar const *fntStrSize = sp_repr_css_property(style, "font-size", "0.0001");
		fntSize = g_ascii_strtod(fntStrSize, NULL);
		sp_repr_get_double(first, "sodipodi:spaceWidth", &spaceSize);
		if ( spaceSize <= 0 ) {
			spaceSize = fntSize / 3;
		}

		if (! sp_repr_get_double(first, "sodipodi:wordSpace", &wordSpace)) wordSpace = 0;
		delete style;
	}

	//if (! sp_repr_get_double(first, "sodipodi:space_size", &spaceSize)) spaceSize = 0;
	if (! sp_repr_get_double(first, "data-endX", &firstEndX)) firstEndX = 0;
	if (! sp_repr_get_double(second, "x", &secondX)) secondX = 0;

	gchar *firstDx = g_strdup(first->attribute("dx"));
	gchar *secondDx = g_strdup(second->attribute("dx"));
	double different = secondX - firstEndX; // gap for second content

	// avoid too low numbers (with exponential form)
	if (sp_creator_sh && strstr(sp_creator_sh, "Adobe Photoshop"))
	{
		if (fabs(different) < 1e-04) different = 0;
	}

	// if we have some space between space - we can put space to it
	if (different < spaceSize) {
		if ((spaceSize - different)/spaceSize < 0.75) {
			spaceSize = different;
		}
	}
	// Will put space between tspan
	gchar addSpace[2] = {0, 0};
	if ((different >= spaceSize) || (different > fntSize/4)) {
		different = different - spaceSize;
		addSpace[0] = ' ';
	}

	// check: need dx attribute or it is empty
	if (fabs(different) > 0 || (secondDx && strlen(secondDx) > 0) || (firstDx && strlen(firstDx) > 0)) {
		// represent different to string
		Inkscape::CSSOStringStream os_diff;
		os_diff << different;
		//fill dx if empty
		if ((! firstDx) || strlen(firstDx) == 0) {
			if (firstDx) free(firstDx);
			firstDx = (gchar*)malloc(strlen(firstContent) * 2 + 2);
			firstDx[0] = 0;
			for(int i = 0; i < (utf8_strlen(firstContent) * 2); i = i + 2) {
				firstDx[i] = '0';
				firstDx[i+1] = ' ';
				firstDx[i+2] = 0;
			}
		}
		if ((! secondDx) || strlen(secondDx) == 0) {
			if (secondDx) free(secondDx);
			secondDx = (gchar*)malloc(strlen(secondContent) * 2 + 2);
			secondDx[0] = 0;
			for(int i = 0; i < (utf8_strlen(secondContent) * 2); i = i + 2) {
				secondDx[i] = '0';
				secondDx[i+1] = ' ';
				secondDx[i+2] = 0;
			}
		}

		gchar *mergedDx;
		// We put additional space between tspan
		if (addSpace[0]) {
			mergedDx = g_strdup_printf("%s %s %s ", firstDx, os_diff.str().c_str(), secondDx);
		} else {
			mergedDx = g_strdup_printf("%s %s%s", firstDx, os_diff.str().c_str(), (secondDx + 1));
		}
		first->setAttribute("dx", mergedDx);
		free(mergedDx);
	}

	gchar const *firstDataX = first->attribute("data-x");
	if (! firstDataX || strlen(firstDataX) == 0) {
		firstDataX = first->attribute("x");
	}
	gchar const *secondDataX = second->attribute("data-x");
	if (! secondDataX || strlen(secondDataX) == 0) {
		secondDataX = second->attribute("x");
	}
	gchar *mergeDataX;
	if (addSpace[0]) {
		Inkscape::CSSOStringStream os;
		os << firstEndX;
		mergeDataX = g_strdup_printf("%s  %s %s", firstDataX, os.str().c_str(), secondDataX);
	} else {
		mergeDataX = g_strdup_printf("%s  %s", firstDataX, secondDataX);
	}
	first->setAttribute("data-x", mergeDataX);
	free(mergeDataX);


	gchar *mergedContent =
			g_strdup_printf("%s%s%s",
				first->firstChild()->content(),
				addSpace,
				second->firstChild()->content());
	first->firstChild()->setContent(mergedContent);
	first->setAttribute("data-endX", second->attribute("data-endX"));
	free(mergedContent);
	free(firstDx);
	free(secondDx);
}

void mergeTspanList(NodeList &tspanArray)
{
	// sort form left to right, from top to bottom
	std::sort(tspanArray.begin(), tspanArray.end(), tspan_compare_position);
	double textSize;
	if (tspanArray.size() > 0) {
	    Inkscape::XML::Node *textNode = tspanArray[0]->parent();
		SPCSSAttr *style = sp_repr_css_attr(textNode, "style");
		gchar const *fntStrSize = sp_repr_css_property(style, "font-size", "0.0001");
		textSize = g_ascii_strtod(fntStrSize, NULL);
		delete style;
	}

	for(int i = 0; i < tspanArray.size() - 1; i++) {
	    double firstY;
	    double secondY;
	    double firstEndX;
	    double secondX;
	    double spaceSize;
		Inkscape::XML::Node *tspan1 = tspanArray[i];
		Inkscape::XML::Node *tspan2 = tspanArray[i + 1];
		sp_repr_get_double(tspan1, "y", &firstY);
		sp_repr_get_double(tspan2, "y", &secondY);

		if (! sp_repr_get_double(tspan1, "data-endX", &firstEndX)) firstEndX = 0;
		if (! sp_repr_get_double(tspan2, "x", &secondX)) secondX = 0;
		if (! sp_repr_get_double(tspan1, "sodipodi:spaceWidth", &spaceSize)) spaceSize = 0;
		const char* align1 = tspan1->attribute("data-align");
		const char* align2 = tspan2->attribute("data-align");
		if ( spaceSize <= 0 ) {
			spaceSize = textSize / 3;
		}

		if (textSize == 0) textSize = 0.00001;
		// round Y to 20% of font size and compare
		// if gap more then 3.5 of text size - mind other column
		if (fabs(firstY - secondY)/textSize < 0.2 &&
			// litle negative gap
				(fabs(firstEndX - secondX)/textSize < 0.2 || (firstEndX <= secondX)) &&
				(secondX - firstEndX < spaceSize * 6) &&
				((align1 == nullptr && align2 == nullptr) || ((align1 != nullptr && align2 != nullptr) && (strcmp(align1, align2) == 0)))
				/* &&
				spaceSize > 0*/) {
			mergeTwoTspan(tspan1, tspan2);
			tspan2->parent()->removeChild(tspan2);
			//tspanArray.erase(tspanArray.begin() + i+1);
			tspanArray.erase(std::remove(tspanArray.begin() + i + 1, tspanArray.begin() + i + 1, nullptr));
			i--;
		}
	}
}

void SvgBuilder::setDocumentSize(double width, double height) {
    sp_repr_set_svg_double(_root, "width", width);
    sp_repr_set_svg_double(_root, "height", height);
    this->_width = width;
    this->_height = height;
}

/**
 * \brief Sets groupmode of the current container to 'layer' and sets its label if given
 */
void SvgBuilder::setAsLayer(char *layer_name) {
    _container->setAttribute("inkscape:groupmode", "layer");
    if (layer_name) {
        _container->setAttribute("inkscape:label", layer_name);
    }
}

void SvgBuilder::setLayoutName(char *layout_name) {
	if (layout_name) {
		_container->setAttribute("data-layoutname", layout_name);
	}
}

static void _getNodesByTag(Inkscape::XML::Node* node, const char* tag, NodeList* list)
{
	Inkscape::XML::Node* cursorNode = node;
	while(cursorNode)
	{
		const char* nodeName = cursorNode->name();
		if (strcasecmp(nodeName, tag) == 0)
			list->push_back(cursorNode);
		if (cursorNode->childCount() > 0)
			_getNodesByTag(cursorNode->firstChild(), tag, list);
		cursorNode = cursorNode->next();
	}
}

NodeList* SvgBuilder::getNodeListByTag(const char* tag, NodeList* list, Inkscape::XML::Node* startNode)
{


	Inkscape::XML::Node* rootNode;
	if (startNode == nullptr)
		rootNode = getRoot();
	else
		rootNode = startNode->firstChild();

	_getNodesByTag(rootNode, tag, list);

	return list;
}

static Inkscape::XML::Node*  _getMainNode(Inkscape::XML::Node* rootNode, int maxDeep = 0)
{
	Inkscape::XML::Node* tmpNode = rootNode->firstChild();
	Inkscape::XML::Node* mainNode = nullptr;
	while(tmpNode)
	{
		if ( strcmp(tmpNode->name(),"svg:defs") == 0 )
		{
			mainNode = tmpNode->next();
			while(mainNode)
			{
				if (strcmp(mainNode->name(), "svg:g") == 0)
					break;
				mainNode = mainNode->next();
			}
			break;
		}
		if (tmpNode->childCount() > 0 && (maxDeep > 1 || maxDeep == 0))
			_getMainNode(tmpNode, (maxDeep == 0 ? maxDeep : maxDeep - 1));

		tmpNode = tmpNode->next();
	}
	return mainNode;
}

Inkscape::XML::Node* SvgBuilder::getMainNode()
{
	Inkscape::XML::Node* rootNode = getRoot();
	return _getMainNode(rootNode, 2);
}

struct NodeState {
	Inkscape::XML::Node* node;
	SPItem* spNode;
	bool isConnected;
	bool isRejected;
	Geom::Rect sqBBox;
	unsigned int z;

	void initGeometry(SPDocument *spDoc);

	NodeState(Inkscape::XML::Node* _node) :
		spNode(nullptr),
		isConnected(false),
		isRejected(true),
		z(0)
	{
		node = _node;
	};
};

void NodeState::initGeometry(SPDocument *spDoc)
{
	spNode = (SPItem*)spDoc->getObjectByRepr(node);

	Geom::OptRect visualBound(spNode->visualBounds());
	if (visualBound.is_initialized())
	{
		sqBBox = visualBound.get();
		Geom::Affine nodeAffine = spNode->getRelativeTransform(spDoc->getRoot());
		sqBBox = sqBBox * nodeAffine;
	}
}

static void appendGraphNodes(Inkscape::XML::Node *startNode, std::vector<NodeState> &nodesStatesList, std::vector<std::string> &tags)
{
	static unsigned int zCounter = 0;
	if (inList(tags, startNode->name()))
	{
		zCounter++;
		NodeState nodeState(startNode);
		nodeState.z = zCounter;
		nodesStatesList.push_back(nodeState);
		//return;
	}

	Inkscape::XML::Node *tmpNode = startNode->firstChild();
	while(tmpNode)
	{
		appendGraphNodes(tmpNode, nodesStatesList, tags);
		tmpNode = tmpNode->next();
	}
}
/*
struct RegionsStat {
	int passIntersect = 0;
	int passAt = 0;
	int nodesCount = 0;
};
*/
bool inList(std::vector<std::string> &tags, const char* tag)
{
	for(auto &item :  tags)
	{
		if (strcasecmp(item.c_str(), tag) == 0)
		 return true;
	}
	return false;
}

bool checkForSolid(Inkscape::XML::Node* firstNode, Inkscape::XML::Node* secondNode)
{
	NodeList firstParents, secondParents;
	Inkscape::XML::Node* tmpNode = firstNode->parent();
	while(tmpNode) {
		firstParents.push_back(tmpNode);
		tmpNode = tmpNode->parent();
	}

	tmpNode = secondNode->parent();
	while(tmpNode) {
		secondParents.push_back(tmpNode);
		tmpNode = tmpNode->parent();
	}

	int firstIdx = firstParents.size()-1;
	int secondIdx = secondParents.size()-1;
	Inkscape::XML::Node* commonNode = firstParents[firstIdx];

	for(;firstParents[firstIdx] == secondParents[secondIdx]; firstIdx--, secondIdx--);
	if (firstParents[firstIdx]->next() == secondParents[secondIdx]) return true;

	return false;
	/*
	tmpNode = firstParents[firstIdx]->next();
	while(tmpNode && tmpNode != secondParents[secondIdx])
	{
		if (has_visible_text(tmpNode)) return false;
		tmpNode = tmpNode->next();
	}
*/
}

std::vector<NodeList> SvgBuilder::getRegions(std::vector<std::string> &tags)
{
	std::vector<NodeList> result;

   	SPDocument *spDoc = getSpDocument();
	SPRoot* spRoot = spDoc->getRoot();
	te_update_layout_now_recursive(spRoot);

	Inkscape::XML::Node *mainNode = getMainNode();
	Inkscape::XML::Document *currentDocument = mainNode->document();
	SPObject* spMainNode = spDoc->getObjectByRepr(mainNode);

	std::vector<NodeState> nodesStatesList;
	appendGraphNodes(mainNode, nodesStatesList, tags);

	// cashe list parameters
	for(NodeState& nodeState : nodesStatesList)
	{
		nodeState.initGeometry(spDoc);
	}

	while(true) // it will ended when we make empty region
	{
		long int regionNodesCount = -1;
		std::vector<NodeState*> currentRegion;
		bool startNewRegion = false;

		while(regionNodesCount != currentRegion.size() && (!startNewRegion)) // if count of paths for region changed - try found other paths
		{
			long int regionChecked = regionNodesCount;
			regionNodesCount = currentRegion.size();
			if (regionChecked < 0) regionChecked = 0;

			for(NodeState& nodeState : nodesStatesList)
			{
				if (nodeState.isConnected) continue;

				if (currentRegion.size() == 0) // it will first path in the symbol
				{
					currentRegion.push_back(&nodeState);
					nodeState.isConnected = true;
					continue;
				}
// we can merge objects it do not exist layout beetwine
				if (! checkForSolid(currentRegion[currentRegion.size()-1]->node, nodeState.node)) {
					startNewRegion = true;
					break;
				}

				// todo: Should be avoid run to same nodes some times - when added new node loop will check all regionNodes agen
				for(size_t regionIdx = regionChecked; regionIdx < currentRegion.size(); regionIdx++)
				{
					NodeState* regionNode = currentRegion[regionIdx];
					const char* rId = regionNode->node->attribute("id");
					const char* nId = nodeState.node->attribute("id");
					//printf("region %s : node %s\n", rId, nId);


					if (regionNode->sqBBox.intersects(nodeState.sqBBox) ||
							regionNode->sqBBox.contains(nodeState.sqBBox))
					{
						nodeState.isConnected = true;
						currentRegion.push_back(&nodeState);
						break;
					}
				} // end for
			} // for by node states
		} //end while (region was changed)
		//start new region

		std::sort(currentRegion.begin(), currentRegion.end(),
		          [] (NodeState* const a, NodeState* const b) { return a->z < b->z; });

		if (currentRegion.size() > 0)
		{
			NodeList region;
			for(NodeState* regionNode : currentRegion)
			{
				region.push_back(regionNode->node);
			}
			result.push_back(region);
		}
		else break;
	};

	return result;
}


/**
 * \brief Sets the current container's opacity
 */
void SvgBuilder::setGroupOpacity(double opacity) {
    sp_repr_set_svg_double(_container, "opacity", CLAMP(opacity, 0.0, 1.0));
}

void SvgBuilder::saveState() {
    SvgGraphicsState new_state;
    new_state.group_depth = 0;
    new_state.softmask = _state_stack.back().softmask;
    _state_stack.push_back(new_state);
    pushGroup();
}

void SvgBuilder::restoreState() {
    while( _state_stack.back().group_depth > 0 ) {
        popGroup();
    }
    _state_stack.pop_back();
}

Inkscape::XML::Node *SvgBuilder::pushNode(const char *name) {
    Inkscape::XML::Node *node = _xml_doc->createElement(name);
    _node_stack.push_back(node);
    _container = node;
    return node;
}

Inkscape::XML::Node *SvgBuilder::popNode() {
    Inkscape::XML::Node *node = NULL;
    if ( _node_stack.size() > 1 ) {
        node = _node_stack.back();
        _node_stack.pop_back();
        _container = _node_stack.back();    // Re-set container
    } else {
        TRACE(("popNode() called when stack is empty\n"));
        node = _root;
    }
    return node;
}

Inkscape::XML::Node *SvgBuilder::pushGroup() {
    Inkscape::XML::Node *saved_container = _container;
    Inkscape::XML::Node *node = pushNode("svg:g");
    saved_container->appendChild(node);
    Inkscape::GC::release(node);
    _state_stack.back().group_depth++;
    // Set as a layer if this is a top-level group
    if ( _container->parent() == _root && _is_top_level ) {
        static int layer_count = 1;
        if ( layer_count > 1 ) {
            gchar *layer_name = g_strdup_printf("%s%d", _docname, layer_count);
            setAsLayer(layer_name);
            g_free(layer_name);
        } else {
            setAsLayer(_docname);
        }
    }
    if (_container->parent()->attribute("inkscape:groupmode") != NULL) {
        _ttm[0] = _ttm[3] = 1.0;    // clear ttm if parent is a layer
        _ttm[1] = _ttm[2] = _ttm[4] = _ttm[5] = 0.0;
        _ttm_is_set = false;
    }
    return _container;
}

Inkscape::XML::Node *SvgBuilder::popGroup() {
    if (_container != _root) {  // Pop if the current container isn't root
        popNode();
        _state_stack.back().group_depth--;
    }

    return _container;
}

Inkscape::XML::Node *SvgBuilder::getContainer() {
    return _container;
}

Inkscape::XML::Node *SvgBuilder::createElement(char const *name) {
	return _xml_doc->createElement(name);
}

static gchar *svgConvertRGBToText(double r, double g, double b) {
    using Inkscape::Filters::clamp;
    static gchar tmp[1023] = {0};
    snprintf(tmp, 1023,
             "#%02x%02x%02x",
             clamp(SP_COLOR_F_TO_U(r)),
             clamp(SP_COLOR_F_TO_U(g)),
             clamp(SP_COLOR_F_TO_U(b)));
    return (gchar *)&tmp;
}

static gchar *svgConvertGfxRGB(GfxRGB *color) {
    double r = (double)color->r / 65535.0;
    double g = (double)color->g / 65535.0;
    double b = (double)color->b / 65535.0;
    return svgConvertRGBToText(r, g, b);
}

static void svgSetTransform(Inkscape::XML::Node *node, double c0, double c1,
                            double c2, double c3, double c4, double c5) {
    Geom::Affine matrix(c0, c1, c2, c3, c4, c5);
    gchar *transform_text = sp_svg_transform_write(matrix);
    node->setAttribute("transform", transform_text);
    g_free(transform_text);
}

/**
 * \brief Generates a SVG path string from poppler's data structure
 */
static gchar *svgInterpretPath(GfxPath *path) {
    Inkscape::SVG::PathString pathString;
    for (int i = 0 ; i < path->getNumSubpaths() ; ++i ) {
        GfxSubpath *subpath = path->getSubpath(i);
        if (subpath->getNumPoints() > 0) {
            pathString.moveTo(subpath->getX(0), subpath->getY(0));
            int j = 1;
            while (j < subpath->getNumPoints()) {
                if (subpath->getCurve(j)) {
                    pathString.curveTo(subpath->getX(j), subpath->getY(j),
                                       subpath->getX(j+1), subpath->getY(j+1),
                                       subpath->getX(j+2), subpath->getY(j+2));

                    j += 3;
                } else {
                    pathString.lineTo(subpath->getX(j), subpath->getY(j));
                    ++j;
                }
            }
            if (subpath->isClosed()) {
                pathString.closePath();
            }
        }
    }

    return g_strdup(pathString.c_str());
}

/**
 * \brief Sets stroke style from poppler's GfxState data structure
 * Uses the given SPCSSAttr for storing the style properties
 */
void SvgBuilder::_setStrokeStyle(SPCSSAttr *css, GfxState *state) {
    // Stroke color/pattern
    if ( state->getStrokeColorSpace()->getMode() == csPattern ) {
        gchar *urltext = _createPattern(state->getStrokePattern(), state, true);
        sp_repr_css_set_property(css, "stroke", urltext);
        if (urltext) {
            g_free(urltext);
        }
    } else {
        GfxRGB stroke_color;
        state->getStrokeRGB(&stroke_color);
        sp_repr_css_set_property(css, "stroke", svgConvertGfxRGB(&stroke_color));
    }

    // Opacity
    Inkscape::CSSOStringStream os_opacity;
    os_opacity << state->getStrokeOpacity();
    sp_repr_css_set_property(css, "stroke-opacity", os_opacity.str().c_str());

    // Line width
    Inkscape::CSSOStringStream os_width;
    double lw = state->getLineWidth();
    if (lw > 0.0) {
        os_width << lw;
    } else {
        // emit a stroke which is 1px in toplevel user units
        double pxw = Inkscape::Util::Quantity::convert(1.0, "pt", "px");
        os_width << 1.0 / state->transformWidth(pxw);
    }
    sp_repr_css_set_property(css, "stroke-width", os_width.str().c_str());

    // Line cap
    switch (state->getLineCap()) {
        case 0:
            sp_repr_css_set_property(css, "stroke-linecap", "butt");
            break;
        case 1:
            sp_repr_css_set_property(css, "stroke-linecap", "round");
            break;
        case 2:
            sp_repr_css_set_property(css, "stroke-linecap", "square");
            break;
    }

    // Line join
    switch (state->getLineJoin()) {
        case 0:
            sp_repr_css_set_property(css, "stroke-linejoin", "miter");
            break;
        case 1:
            sp_repr_css_set_property(css, "stroke-linejoin", "round");
            break;
        case 2:
            sp_repr_css_set_property(css, "stroke-linejoin", "bevel");
            break;
    }

    // Miterlimit
    Inkscape::CSSOStringStream os_ml;
    os_ml << state->getMiterLimit();
    sp_repr_css_set_property(css, "stroke-miterlimit", os_ml.str().c_str());

    // Line dash
    double *dash_pattern;
    int dash_length;
    double dash_start;
    state->getLineDash(&dash_pattern, &dash_length, &dash_start);
    if ( dash_length > 0 ) {
        Inkscape::CSSOStringStream os_array;
        for ( int i = 0 ; i < dash_length ; i++ ) {
            os_array << dash_pattern[i];
            if (i < (dash_length - 1)) {
                os_array << ",";
            }
        }
        sp_repr_css_set_property(css, "stroke-dasharray", os_array.str().c_str());

        Inkscape::CSSOStringStream os_offset;
        os_offset << dash_start;
        sp_repr_css_set_property(css, "stroke-dashoffset", os_offset.str().c_str());
    } else {
        sp_repr_css_set_property(css, "stroke-dasharray", "none");
        sp_repr_css_set_property(css, "stroke-dashoffset", NULL);
    }
}

/**
 * \brief Sets fill style from poppler's GfxState data structure
 * Uses the given SPCSSAttr for storing the style properties.
 */
void SvgBuilder::_setFillStyle(SPCSSAttr *css, GfxState *state, bool even_odd) {

    // Fill color/pattern
    if ( state->getFillColorSpace()->getMode() == csPattern ) {
        gchar *urltext = _createPattern(state->getFillPattern(), state);
        sp_repr_css_set_property(css, "fill", urltext);
        if (urltext) {
            g_free(urltext);
        }
    } else {
        GfxRGB fill_color;
        state->getFillRGB(&fill_color);
        sp_repr_css_set_property(css, "fill", svgConvertGfxRGB(&fill_color));
    }

    // Opacity
    Inkscape::CSSOStringStream os_opacity;
    os_opacity << state->getFillOpacity();
    sp_repr_css_set_property(css, "fill-opacity", os_opacity.str().c_str());
    
    // Fill rule
    sp_repr_css_set_property(css, "fill-rule", even_odd ? "evenodd" : "nonzero");
}

/**
 * \brief Sets style properties from poppler's GfxState data structure
 * \return SPCSSAttr with all the relevant properties set
 */
SPCSSAttr *SvgBuilder::_setStyle(GfxState *state, bool fill, bool stroke, bool even_odd) {
    SPCSSAttr *css = sp_repr_css_attr_new();
    if (fill) {
        _setFillStyle(css, state, even_odd);
    } else {
        sp_repr_css_set_property(css, "fill", "none");
    }
    
    if (stroke) {
        _setStrokeStyle(css, state);
    } else {
        sp_repr_css_set_property(css, "stroke", "none");
    }

    return css;
}

/**
 * \brief Emits the current path in poppler's GfxState data structure
 * Can be used to do filling and stroking at once.
 *
 * \param fill whether the path should be filled
 * \param stroke whether the path should be stroked
 * \param even_odd whether the even-odd rule should be used when filling the path
 */
void SvgBuilder::addPath(GfxState *state, bool fill, bool stroke, bool even_odd) {
    Inkscape::XML::Node *path = _xml_doc->createElement("svg:path");
    gchar *pathtext = svgInterpretPath(state->getPath());
    path->setAttribute("d", pathtext);
    g_free(pathtext);

    // Set style
    SPCSSAttr *css = _setStyle(state, fill, stroke, even_odd);
    if (fill) {
		double opacity;
		if (sp_repr_get_double(css, "fill-opacity", &opacity) && opacity > 0 && opacity < 1) {
			const char *value;
			 if ( (value = css->attribute("fill")) ) {
				 guint32 color = sp_svg_read_color(value, color);
				 if (color == 0) {
					 css->setAttribute("fill", "#FFFFFF");
				 }
			 }
		}
    }

    sp_repr_css_change(path, css, "style");
    sp_repr_css_attr_unref(css);

    _container->appendChild(path);
    _countOfPath++;
    Inkscape::GC::release(path);
}

/**
 * \brief Emits the current path in poppler's GfxState data structure
 * The path is set to be filled with the given shading.
 */
void SvgBuilder::addShadedFill(GfxShading *shading, double *matrix, GfxPath *path,
                               bool even_odd) {

    Inkscape::XML::Node *path_node = _xml_doc->createElement("svg:path");
    gchar *pathtext = svgInterpretPath(path);
    path_node->setAttribute("d", pathtext);
    g_free(pathtext);

    // Set style
    SPCSSAttr *css = sp_repr_css_attr_new();
    gchar *id = _createGradient(shading, matrix, true);
    if (id) {
        gchar *urltext = g_strdup_printf ("url(#%s)", id);
        sp_repr_css_set_property(css, "fill", urltext);
        g_free(urltext);
        g_free(id);
    } else {
        sp_repr_css_attr_unref(css);
        Inkscape::GC::release(path_node);
        return;
    }
    if (even_odd) {
        sp_repr_css_set_property(css, "fill-rule", "evenodd");
    }
    sp_repr_css_set_property(css, "stroke", "none");
    sp_repr_css_change(path_node, css, "style");
    sp_repr_css_attr_unref(css);

    _container->appendChild(path_node);
    Inkscape::GC::release(path_node);

    // Remove the clipping path emitted before the 'sh' operator
    int up_walk = 0;
    Inkscape::XML::Node *node = _container->parent();
    while( node && node->childCount() == 1 && up_walk < 3 ) {
        gchar const *clip_path_url = node->attribute("clip-path");
        if (clip_path_url) {
            // Obtain clipping path's id from the URL
            gchar clip_path_id[32];
            strncpy(clip_path_id, clip_path_url + 5, strlen(clip_path_url) - 6);
	    clip_path_id[sizeof (clip_path_id) - 1] = '\0';
            SPObject *clip_obj = _doc->getObjectById(clip_path_id);
            if (clip_obj) {
                clip_obj->deleteObject();
                node->setAttribute("clip-path", NULL);
                TRACE(("removed clipping path: %s\n", clip_path_id));
            }
            break;
        }
        node = node->parent();
        up_walk++;
    }
}

/**
 * \brief Clips to the current path set in GfxState
 * \param state poppler's data structure
 * \param even_odd whether the even-odd rule should be applied
 */
void SvgBuilder::clip(GfxState *state, bool even_odd) {
    pushGroup();
    setClipPath(state, even_odd);
}

void SvgBuilder::setClipPath(GfxState *state, bool even_odd) {
    // Create the clipPath repr
    Inkscape::XML::Node *clip_path = _xml_doc->createElement("svg:clipPath");
    clip_path->setAttribute("clipPathUnits", "userSpaceOnUse");
    // Create the path
    Inkscape::XML::Node *path = _xml_doc->createElement("svg:path");
    gchar *pathtext = svgInterpretPath(state->getPath());
    path->setAttribute("d", pathtext);
    g_free(pathtext);
    if (even_odd) {
        path->setAttribute("clip-rule", "evenodd");
    }
    clip_path->appendChild(path);
    Inkscape::GC::release(path);
    // Append clipPath to defs and get id
    _doc->getDefs()->getRepr()->appendChild(clip_path);
    gchar *urltext = g_strdup_printf ("url(#%s)", clip_path->attribute("id"));
    Inkscape::GC::release(clip_path);
    _container->setAttribute("clip-path", urltext);
    g_free(urltext);
}

/**
 * \brief Fills the given array with the current container's transform, if set
 * \param transform array of doubles to be filled
 * \return true on success; false on invalid transformation
 */
bool SvgBuilder::getTransform(double *transform) {
    Geom::Affine svd;
    gchar const *tr = _container->attribute("transform");
    bool valid = sp_svg_transform_read(tr, &svd);
    if (valid) {
        for ( int i = 0 ; i < 6 ; i++ ) {
            transform[i] = svd[i];
        }
        return true;
    } else {
        return false;
    }
}

gchar *SvgBuilder::getDocName() {
	return _docname;
}

/**
 * \brief Sets the transformation matrix of the current container
 */
void SvgBuilder::setTransform(double c0, double c1, double c2, double c3,
                              double c4, double c5) {
    // do not remember the group which is a layer
    if ((_container->attribute("inkscape:groupmode") == NULL) && !_ttm_is_set) {
        _ttm[0] = c0;
        _ttm[1] = c1;
        _ttm[2] = c2;
        _ttm[3] = c3;
        _ttm[4] = c4;
        _ttm[5] = c5;
        _ttm_is_set = true;
    }

    // Avoid transforming a group with an already set clip-path
    if ( _container->attribute("clip-path") != NULL ) {
        pushGroup();
    }
    TRACE(("setTransform: %f %f %f %f %f %f\n", c0, c1, c2, c3, c4, c5));
    svgSetTransform(_container, c0, c1, c2, c3, c4, c5);
}

void SvgBuilder::setTransform(double const *transform) {
    setTransform(transform[0], transform[1], transform[2], transform[3],
                 transform[4], transform[5]);
}

/**
 * \brief Checks whether the given pattern type can be represented in SVG
 * Used by PdfParser to decide when to do fallback operations.
 */
bool SvgBuilder::isPatternTypeSupported(GfxPattern *pattern) {
    if ( pattern != NULL ) {
        if ( pattern->getType() == 2 ) {    // shading pattern
            GfxShading *shading = (static_cast<GfxShadingPattern *>(pattern))->getShading();
            int shadingType = shading->getType();
            if ( shadingType == 2 || // axial shading
                 shadingType == 3 ) {   // radial shading
                return true;
            }
            return false;
        } else if ( pattern->getType() == 1 ) {   // tiling pattern
            return true;
        }
    }

    return false;
}

/**
 * \brief Creates a pattern from poppler's data structure
 * Handles linear and radial gradients. Creates a new PdfParser and uses it to
 * build a tiling pattern.
 * \return an url pointing to the created pattern
 */
gchar *SvgBuilder::_createPattern(GfxPattern *pattern, GfxState *state, bool is_stroke) {
    gchar *id = NULL;
    if ( pattern != NULL ) {
        if ( pattern->getType() == 2 ) {  // Shading pattern
            GfxShadingPattern *shading_pattern = static_cast<GfxShadingPattern *>(pattern);
            double *ptm;
            double m[6] = {1, 0, 0, 1, 0, 0};
            double det;

            // construct a (pattern space) -> (current space) transform matrix

            ptm = shading_pattern->getMatrix();
            det = _ttm[0] * _ttm[3] - _ttm[1] * _ttm[2];
            if (det) {
                double ittm[6];	// invert ttm
                ittm[0] =  _ttm[3] / det;
                ittm[1] = -_ttm[1] / det;
                ittm[2] = -_ttm[2] / det;
                ittm[3] =  _ttm[0] / det;
                ittm[4] = (_ttm[2] * _ttm[5] - _ttm[3] * _ttm[4]) / det;
                ittm[5] = (_ttm[1] * _ttm[4] - _ttm[0] * _ttm[5]) / det;
                m[0] = ptm[0] * ittm[0] + ptm[1] * ittm[2];
                m[1] = ptm[0] * ittm[1] + ptm[1] * ittm[3];
                m[2] = ptm[2] * ittm[0] + ptm[3] * ittm[2];
                m[3] = ptm[2] * ittm[1] + ptm[3] * ittm[3];
                m[4] = ptm[4] * ittm[0] + ptm[5] * ittm[2] + ittm[4];
                m[5] = ptm[4] * ittm[1] + ptm[5] * ittm[3] + ittm[5];
            }
            id = _createGradient(shading_pattern->getShading(),
                                 m,
                                 !is_stroke);
        } else if ( pattern->getType() == 1 ) {   // Tiling pattern
            id = _createTilingPattern(static_cast<GfxTilingPattern*>(pattern), state, is_stroke);
        }
    } else {
        return NULL;
    }
    gchar *urltext = g_strdup_printf ("url(#%s)", id);
    g_free(id);
    return urltext;
}

/**
 * \brief Creates a tiling pattern from poppler's data structure
 * Creates a sub-page PdfParser and uses it to parse the pattern's content stream.
 * \return id of the created pattern
 */
gchar *SvgBuilder::_createTilingPattern(GfxTilingPattern *tiling_pattern,
                                        GfxState *state, bool is_stroke) {

    Inkscape::XML::Node *pattern_node = _xml_doc->createElement("svg:pattern");
    // Set pattern transform matrix
    double *p2u = tiling_pattern->getMatrix();
    double m[6] = {1, 0, 0, 1, 0, 0};
    double det;
    det = _ttm[0] * _ttm[3] - _ttm[1] * _ttm[2];    // see LP Bug 1168908
    if (det) {
        double ittm[6];	// invert ttm
        ittm[0] =  _ttm[3] / det;
        ittm[1] = -_ttm[1] / det;
        ittm[2] = -_ttm[2] / det;
        ittm[3] =  _ttm[0] / det;
        ittm[4] = (_ttm[2] * _ttm[5] - _ttm[3] * _ttm[4]) / det;
        ittm[5] = (_ttm[1] * _ttm[4] - _ttm[0] * _ttm[5]) / det;
        m[0] = p2u[0] * ittm[0] + p2u[1] * ittm[2];
        m[1] = p2u[0] * ittm[1] + p2u[1] * ittm[3];
        m[2] = p2u[2] * ittm[0] + p2u[3] * ittm[2];
        m[3] = p2u[2] * ittm[1] + p2u[3] * ittm[3];
        m[4] = p2u[4] * ittm[0] + p2u[5] * ittm[2] + ittm[4];
        m[5] = p2u[4] * ittm[1] + p2u[5] * ittm[3] + ittm[5];
    }
    Geom::Affine pat_matrix(m[0], m[1], m[2], m[3], m[4], m[5]);
    gchar *transform_text = sp_svg_transform_write(pat_matrix);
    pattern_node->setAttribute("patternTransform", transform_text);
    g_free(transform_text);
    pattern_node->setAttribute("patternUnits", "userSpaceOnUse");
    // Set pattern tiling
    // FIXME: don't ignore XStep and YStep
    double *bbox = tiling_pattern->getBBox();
    sp_repr_set_svg_double(pattern_node, "x", 0.0);
    sp_repr_set_svg_double(pattern_node, "y", 0.0);
    sp_repr_set_svg_double(pattern_node, "width", bbox[2] - bbox[0]);
    sp_repr_set_svg_double(pattern_node, "height", bbox[3] - bbox[1]);

    // Convert BBox for PdfParser
    PDFRectangle box;
    box.x1 = bbox[0];
    box.y1 = bbox[1];
    box.x2 = bbox[2];
    box.y2 = bbox[3];
    // Create new SvgBuilder and sub-page PdfParser
    SvgBuilder *pattern_builder = new SvgBuilder(this, pattern_node);
    PdfParser *pdf_parser = new PdfParser(_xref, pattern_builder, tiling_pattern->getResDict(),
                                          &box);
    // Get pattern color space
    GfxPatternColorSpace *pat_cs = (GfxPatternColorSpace *)( is_stroke ? state->getStrokeColorSpace()
                                                            : state->getFillColorSpace() );
    // Set fill/stroke colors if this is an uncolored tiling pattern
    GfxColorSpace *cs = NULL;
    if ( tiling_pattern->getPaintType() == 2 && ( cs = pat_cs->getUnder() ) ) {
        GfxState *pattern_state = pdf_parser->getState();
        pattern_state->setFillColorSpace(cs->copy());
        pattern_state->setFillColor(state->getFillColor());
        pattern_state->setStrokeColorSpace(cs->copy());
        pattern_state->setStrokeColor(state->getFillColor());
    }

    // Generate the SVG pattern
    pdf_parser->parse(tiling_pattern->getContentStream());

    // Cleanup
    delete pdf_parser;
    delete pattern_builder;

    // Append the pattern to defs
    _doc->getDefs()->getRepr()->appendChild(pattern_node);
    gchar *id = g_strdup(pattern_node->attribute("id"));
    Inkscape::GC::release(pattern_node);

    return id;
}

/**
 * \brief Creates a linear or radial gradient from poppler's data structure
 * \param shading poppler's data structure for the shading
 * \param matrix gradient transformation, can be null
 * \param for_shading true if we're creating this for a shading operator; false otherwise
 * \return id of the created object
 */
gchar *SvgBuilder::_createGradient(GfxShading *shading, double *matrix, bool for_shading) {
    Inkscape::XML::Node *gradient;
    Function *func;
    int num_funcs;
    bool extend0, extend1;

    if ( shading->getType() == 2 ) {  // Axial shading
        gradient = _xml_doc->createElement("svg:linearGradient");
        GfxAxialShading *axial_shading = static_cast<GfxAxialShading*>(shading);
        double x1, y1, x2, y2;
        axial_shading->getCoords(&x1, &y1, &x2, &y2);
        sp_repr_set_svg_double(gradient, "x1", x1);
        sp_repr_set_svg_double(gradient, "y1", y1);
        sp_repr_set_svg_double(gradient, "x2", x2);
        sp_repr_set_svg_double(gradient, "y2", y2);
        extend0 = axial_shading->getExtend0();
        extend1 = axial_shading->getExtend1();
        num_funcs = axial_shading->getNFuncs();
        func = axial_shading->getFunc(0);
    } else if (shading->getType() == 3) {   // Radial shading
        gradient = _xml_doc->createElement("svg:radialGradient");
        GfxRadialShading *radial_shading = static_cast<GfxRadialShading*>(shading);
        double x1, y1, r1, x2, y2, r2;
        radial_shading->getCoords(&x1, &y1, &r1, &x2, &y2, &r2);
        // FIXME: the inner circle's radius is ignored here
        sp_repr_set_svg_double(gradient, "fx", x1);
        sp_repr_set_svg_double(gradient, "fy", y1);
        sp_repr_set_svg_double(gradient, "cx", x2);
        sp_repr_set_svg_double(gradient, "cy", y2);
        sp_repr_set_svg_double(gradient, "r", r2);
        extend0 = radial_shading->getExtend0();
        extend1 = radial_shading->getExtend1();
        num_funcs = radial_shading->getNFuncs();
        func = radial_shading->getFunc(0);
    } else {    // Unsupported shading type
        return NULL;
    }
    gradient->setAttribute("gradientUnits", "userSpaceOnUse");
    // If needed, flip the gradient transform around the y axis
    if (matrix) {
        Geom::Affine pat_matrix(matrix[0], matrix[1], matrix[2], matrix[3],
                              matrix[4], matrix[5]);
        if ( !for_shading && _is_top_level ) {
            Geom::Affine flip(1.0, 0.0, 0.0, -1.0, 0.0, Inkscape::Util::Quantity::convert(_height, "px", "pt"));
            pat_matrix *= flip;
        }
        gchar *transform_text = sp_svg_transform_write(pat_matrix);
        gradient->setAttribute("gradientTransform", transform_text);
        g_free(transform_text);
    }

    if ( extend0 && extend1 ) {
        gradient->setAttribute("spreadMethod", "pad");
    }

    if ( num_funcs > 1 || !_addGradientStops(gradient, shading, func) ) {
        Inkscape::GC::release(gradient);
        return NULL;
    }

    Inkscape::XML::Node *defs = _doc->getDefs()->getRepr();
    defs->appendChild(gradient);
    gchar *id = g_strdup(gradient->attribute("id"));
    Inkscape::GC::release(gradient);

    return id;
}

#define EPSILON 0.0001
/**
 * \brief Adds a stop with the given properties to the gradient's representation
 */
void SvgBuilder::_addStopToGradient(Inkscape::XML::Node *gradient, double offset,
                                    GfxRGB *color, double opacity) {
    Inkscape::XML::Node *stop = _xml_doc->createElement("svg:stop");
    SPCSSAttr *css = sp_repr_css_attr_new();
    Inkscape::CSSOStringStream os_opacity;
    gchar *color_text = NULL;
    if ( _transp_group_stack != NULL && _transp_group_stack->for_softmask ) {
        double gray = (double)color->r / 65535.0;
        gray = CLAMP(gray, 0.0, 1.0);
        os_opacity << gray;
        color_text = (char*) "#ffffff";
    } else {
        os_opacity << opacity;
        color_text = svgConvertGfxRGB(color);
    }
    sp_repr_css_set_property(css, "stop-opacity", os_opacity.str().c_str());
    sp_repr_css_set_property(css, "stop-color", color_text);

    sp_repr_css_change(stop, css, "style");
    sp_repr_css_attr_unref(css);
    sp_repr_set_css_double(stop, "offset", offset);

    gradient->appendChild(stop);
    Inkscape::GC::release(stop);
}

static bool svgGetShadingColorRGB(GfxShading *shading, double offset, GfxRGB *result) {
    GfxColorSpace *color_space = shading->getColorSpace();
    GfxColor temp;
    if ( shading->getType() == 2 ) {  // Axial shading
        (static_cast<GfxAxialShading*>(shading))->getColor(offset, &temp);
    } else if ( shading->getType() == 3 ) { // Radial shading
        (static_cast<GfxRadialShading*>(shading))->getColor(offset, &temp);
    } else {
        return false;
    }
    // Convert it to RGB
    color_space->getRGB(&temp, result);

    return true;
}

#define INT_EPSILON 8
bool SvgBuilder::_addGradientStops(Inkscape::XML::Node *gradient, GfxShading *shading,
                                   Function *func) {
    int type = func->getType();
    if ( type == 0 || type == 2 ) {  // Sampled or exponential function
        GfxRGB stop1, stop2;
        if ( !svgGetShadingColorRGB(shading, 0.0, &stop1) ||
             !svgGetShadingColorRGB(shading, 1.0, &stop2) ) {
            return false;
        } else {
            _addStopToGradient(gradient, 0.0, &stop1, 1.0);
            _addStopToGradient(gradient, 1.0, &stop2, 1.0);
        }
    } else if ( type == 3 ) { // Stitching
        StitchingFunction *stitchingFunc = static_cast<StitchingFunction*>(func);
        double *bounds = stitchingFunc->getBounds();
        double *encode = stitchingFunc->getEncode();
        int num_funcs = stitchingFunc->getNumFuncs();

        // Add stops from all the stitched functions
        GfxRGB prev_color, color;
        svgGetShadingColorRGB(shading, bounds[0], &prev_color);
        _addStopToGradient(gradient, bounds[0], &prev_color, 1.0);
        for ( int i = 0 ; i < num_funcs ; i++ ) {
            svgGetShadingColorRGB(shading, bounds[i + 1], &color);
            // Add stops
            if (stitchingFunc->getFunc(i)->getType() == 2) {    // process exponential fxn
                double expE = (static_cast<ExponentialFunction*>(stitchingFunc->getFunc(i)))->getE();
                if (expE > 1.0) {
                    expE = (bounds[i + 1] - bounds[i])/expE;    // approximate exponential as a single straight line at x=1
                    if (encode[2*i] == 0) {    // normal sequence
                        _addStopToGradient(gradient, bounds[i + 1] - expE, &prev_color, 1.0);
                    } else {                   // reflected sequence
                        _addStopToGradient(gradient, bounds[i] + expE, &color, 1.0);
                    }
                }
            }
            _addStopToGradient(gradient, bounds[i + 1], &color, 1.0);
            prev_color = color;
        }
    } else { // Unsupported function type
        return false;
    }

    return true;
}

/**
 * \brief Sets _invalidated_style to true to indicate that styles have to be updated
 * Used for text output when glyphs are buffered till a font change
 */
void SvgBuilder::updateStyle(GfxState *state) {
    if (_in_text_object) {
        _invalidated_style = true;
        _current_state = state;
        _flushText();
    }
}

/*
    MatchingChars
    Count for how many characters s1 matches sp taking into account 
    that a space in sp may be removed or replaced by some other tokens
    specified in the code. (Bug LP #179589)
*/
static size_t MatchingChars(std::string s1, std::string sp)
{
    size_t is = 0;
    size_t ip = 0;

    while(is < s1.length() && ip < sp.length()) {
        if (s1[is] == sp[ip]) {
            is++; ip++;
        } else if (sp[ip] == ' ') {
            ip++;
            if (s1[is] == '_') { // Valid matches to spaces in sp.
                is++;
            }
        } else {
            break;
        }
    }
    return ip;
}

/*
    SvgBuilder::_BestMatchingFont
    Scan the available fonts to find the font name that best matches PDFname.
    (Bug LP #179589)
*/
std::string SvgBuilder::_BestMatchingFont(std::string PDFname)
{
    double bestMatch = 0;
    std::string bestFontname = "Arial";
    
    for (guint i = 0; i < _availableFontNames.size(); i++) {
        std::string fontname = _availableFontNames[i];
        
        // At least the first word of the font name should match.
        size_t minMatch = fontname.find(" ");
        if (minMatch == std::string::npos) {
           minMatch = fontname.length();
        }
        
        size_t Match = MatchingChars(PDFname, fontname);
        if (Match >= minMatch) {
            double relMatch = (float)Match / (fontname.length() + PDFname.length());
            if (relMatch > bestMatch) {
                bestMatch = relMatch;
                bestFontname = fontname;
            }
        }
    }

    if (bestMatch == 0)
        return PDFname;
    else
        return bestFontname;
}

/**
 * This array holds info about translating font weight names to more or less CSS equivalents
 */
static char *font_weight_translator[][2] = {
    {(char*) "bold",        (char*) "bold"},
    {(char*) "light",       (char*) "300"},
    {(char*) "black",       (char*) "900"},
    {(char*) "heavy",       (char*) "900"},
    {(char*) "ultrabold",   (char*) "800"},
    {(char*) "extrabold",   (char*) "800"},
    {(char*) "demibold",    (char*) "600"},
    {(char*) "semibold",    (char*) "600"},
    {(char*) "medium",      (char*) "500"},
    {(char*) "book",        (char*) "normal"},
    {(char*) "regular",     (char*) "normal"},
    {(char*) "roman",       (char*) "normal"},
    {(char*) "normal",      (char*) "normal"},
    {(char*) "ultralight",  (char*) "200"},
    {(char*) "extralight",  (char*) "200"},
    {(char*) "thin",        (char*) "100"}
};

/**
 * \brief Updates _font_style according to the font set in parameter state
 */
void SvgBuilder::updateFont(GfxState *state) {

    TRACE(("updateFont()\n"));
    _need_font_update = false;
    updateTextMatrix(state);    // Ensure that we have a text matrix built

    if (_font_style) {
        //sp_repr_css_attr_unref(_font_style);
    }
    _font_style = sp_repr_css_attr_new();
    GfxFont *font = state->getFont();
    // Store original name
    if (font->getName()) {
        _font_specification = font->getName()->getCString();
        if (font->getType() == fontCIDType2 && font->getToUnicode() && sp_font_default_font_sh) {
        		_font_specification = sp_font_default_font_sh;
        }
    } else {
        _font_specification = (char*) "Lato";
    }

    // Prune the font name to get the correct font family name
    // In a PDF font names can look like this: IONIPB+MetaPlusBold-Italic
    char *font_family = NULL;
    char *font_style = NULL;
    char *font_style_lowercase = NULL;
    char *plus_sign = strstr(_font_specification, "+");
    if (plus_sign) {
        font_family = g_strdup(plus_sign + 1);
        _font_specification = plus_sign + 1;
    } else {
        font_family = g_strdup(_font_specification);
    }
    char *style_delim = NULL;
    if ( ( style_delim = g_strrstr(font_family, "-") ) ||
         ( style_delim = g_strrstr(font_family, ",") ) ) {
        font_style = style_delim + 1;
        font_style_lowercase = g_ascii_strdown(font_style, -1);
        style_delim[0] = 0;
    }

    // Font family
    if (state->getFont()->getName()) { // if font family is explicitly given use it.
    	char *fName = prepareFamilyName(font->getName()->getCString(), false);
		GooString *fontName2= new GooString(fName);
		free(fName);

        //sp_repr_css_set_property(_font_style, "font-family", font->getFamily()->getCString());
		if (font->getType() == fontCIDType2 && font->getToUnicode() && sp_font_default_font_sh) {
			sp_repr_css_set_property(_font_style, "font-family", sp_font_default_font_sh);
		} else {
			sp_repr_css_set_property(_font_style, "font-family", fontName2->getCString());
		}
		delete fontName2;
    } else { 
        int attr_value = 1;
        sp_repr_get_int(_preferences, "localFonts", &attr_value);
        if (attr_value != 0) {
            // Find the font that best matches the stripped down (orig)name (Bug LP #179589).
            sp_repr_css_set_property(_font_style, "font-family", _BestMatchingFont(font_family).c_str());
        } else {
            sp_repr_css_set_property(_font_style, "font-family", font_family);
        }
    }

    // Font style
    if (font->isItalic()) {
        sp_repr_css_set_property(_font_style, "font-style", "italic");
    } else if (font_style) {
        if ( strstr(font_style_lowercase, "italic") ||
             strstr(font_style_lowercase, "slanted") ) {
            sp_repr_css_set_property(_font_style, "font-style", "italic");
        } else if (strstr(font_style_lowercase, "oblique")) {
            sp_repr_css_set_property(_font_style, "font-style", "oblique");
        }
    }

    // Font variant -- default 'normal' value
    sp_repr_css_set_property(_font_style, "font-variant", "normal");

    // Font weight
    GfxFont::Weight font_weight = font->getWeight();
    char *css_font_weight = NULL;
    if ( font_weight != GfxFont::WeightNotDefined ) {
        if ( font_weight == GfxFont::W400 ) {
            css_font_weight = (char*) "normal";
        } else if ( font_weight == GfxFont::W700 ) {
            css_font_weight = (char*) "bold";
        } else {
            gchar weight_num[4] = "100";
            weight_num[0] = (gchar)( '1' + (font_weight - GfxFont::W100) );
            sp_repr_css_set_property(_font_style, "font-weight", (gchar *)&weight_num);
        }
    } else if (font_style) {
        // Apply the font weight translations
        int num_translations = sizeof(font_weight_translator) / ( 2 * sizeof(char *) );
        for ( int i = 0 ; i < num_translations ; i++ ) {
            if (strstr(font_style_lowercase, font_weight_translator[i][0])) {
                css_font_weight = font_weight_translator[i][1];
            }
        }
    } else {
        css_font_weight = (char*) "normal";
    }
    if (css_font_weight) {
        sp_repr_css_set_property(_font_style, "font-weight", css_font_weight);
    }
    g_free(font_family);
    if (font_style_lowercase) {
        g_free(font_style_lowercase);
    }

    // Font stretch
    GfxFont::Stretch font_stretch = font->getStretch();
    gchar *stretch_value = NULL;
    switch (font_stretch) {
        case GfxFont::UltraCondensed:
            stretch_value = (char*) "ultra-condensed";
            break;
        case GfxFont::ExtraCondensed:
            stretch_value = (char*) "extra-condensed";
            break;
        case GfxFont::Condensed:
            stretch_value = (char*) "condensed";
            break;
        case GfxFont::SemiCondensed:
            stretch_value = (char*) "semi-condensed";
            break;
        case GfxFont::Normal:
            stretch_value = (char*) "normal";
            break;
        case GfxFont::SemiExpanded:
            stretch_value = (char*) "semi-expanded";
            break;
        case GfxFont::Expanded:
            stretch_value = (char*) "expanded";
            break;
        case GfxFont::ExtraExpanded:
            stretch_value = (char*) "extra-expanded";
            break;
        case GfxFont::UltraExpanded:
            stretch_value = (char*) "ultra-expanded";
            break;
        default:
            break;
    }
    if ( stretch_value != NULL ) {
        sp_repr_css_set_property(_font_style, "font-stretch", stretch_value);
    }

    // Font size
    Inkscape::CSSOStringStream os_font_size;
    double css_font_size = _font_scaling * state->getFontSize();
    if ( font->getType() == fontType3 ) {
        double *font_matrix = font->getFontMatrix();
        if ( font_matrix[0] != 0.0 ) {
            css_font_size *= font_matrix[3] / font_matrix[0];
            if (css_font_size < 0) {
            	// TODO: here need use fabs instead abs ( Sergey )
            	css_font_size = abs(css_font_size);
            }
        }
    }

    css_font_size = round(css_font_size * 100) / 100;
    os_font_size << css_font_size;
    sp_repr_css_set_property(_font_style, "font-size", os_font_size.str().c_str());

    // Writing mode
    if ( font->getWMode() == 0 ) {
        sp_repr_css_set_property(_font_style, "writing-mode", "lr");
    } else {
        sp_repr_css_set_property(_font_style, "writing-mode", "tb");
    }

    _current_font = font;
    _invalidated_style = true;
}

/**
 * \brief Shifts the current text position by the given amount (specified in text space)
 */
void SvgBuilder::updateTextShift(GfxState *state, double shift) {
    double shift_value = -shift * 0.001 * fabs(state->getFontSize());
    if (state->getFont()->getWMode()) {
        _text_position[1] += shift_value;
    } else {
        _text_position[0] += shift_value;
    }
}

/**
 * \brief Updates current text position
 */
void SvgBuilder::updateTextPosition(double tx, double ty) {
    Geom::Point new_position(tx, ty);
    _text_position = new_position;
}

/**
 * \brief Flushes the buffered characters
 */
void SvgBuilder::updateTextMatrix(GfxState *state) {
	upTimer(timTEXT_FLUSH);
    _flushText();
    // Update text matrix
    double *text_matrix = state->getTextMat();
    double w_scale = sqrt( text_matrix[0] * text_matrix[0] + text_matrix[2] * text_matrix[2] );
    double h_scale = sqrt( text_matrix[1] * text_matrix[1] + text_matrix[3] * text_matrix[3] );
    double max_scale;
    if ( w_scale > h_scale ) {
        max_scale = w_scale;
    } else {
        max_scale = h_scale;
    }
    // Calculate new text matrix
    Geom::Affine new_text_matrix(text_matrix[0] * state->getHorizScaling(),
                               text_matrix[1] * state->getHorizScaling(),
                               -text_matrix[2], -text_matrix[3],
                               0.0, 0.0);

    if ( fabs( max_scale - 1.0 ) > EPSILON ) {
        // Cancel out scaling by font size in text matrix
        for ( int i = 0 ; i < 4 ; i++ ) {
            new_text_matrix[i] /= max_scale;
        }
    }
    _text_matrix = new_text_matrix;
    _font_scaling = max_scale;
    incTimer(timTEXT_FLUSH);
}

static double calculateSvgDx(const SvgGlyph& glyph, const SvgGlyph& prevGlyph, double scale) {
	double calc_dx = glyph.text_position[0] - prevGlyph.text_position[0]; // X distance between left side of two symbols
	calc_dx = calc_dx - prevGlyph.dx; // receive extra space between symbols
	//calc_dx = calc_dx/glyph.code.size(); // possible it is macro symbol (string definite as one symbol in PDF :-/), so we divide extra space to all
	calc_dx *= scale; // scale to the font size

    if (! glyph.font->getWMode()) {  // if text should has constant extra space
    	calc_dx += glyph.charSpace * scale;
    }
    return calc_dx;
}

/**
 * \brief Writes the buffered characters to the SVG document
 */
void SvgBuilder::_flushText() {
    // Ignore empty strings
    if ( _glyphs.empty()) {
        _glyphs.clear();
        return;
    }
    double lastDeltaX = 0;
    double first_glyphX = 0;
    double tspanEndXPos = 0;
    int glipCount = 0;
    std::vector<SvgGlyph>::iterator i = _glyphs.begin();
    const SvgGlyph& first_glyph = (*i);
    int render_mode = first_glyph.render_mode;
    // Ignore invisible characters
    if ( render_mode == 3 ) {
        _glyphs.clear();
        return;
    }

    Inkscape::XML::Node *text_node = _xml_doc->createElement("svg:text");
    gchar c[32];
    // add coordinates for end text
    sp_svg_number_write_de(c, sizeof(c), glipEndX, 8, -8);
    text_node->setAttribute("data-endGlipX", g_strdup(c));
    sp_svg_number_write_de(c, sizeof(c), glipEndY, 8, -8);
    text_node->setAttribute("data-endGlipY", g_strdup(c));
    // Set text matrix
    Geom::Affine text_transform(_text_matrix);
    text_transform[4] = first_glyph.position[0];
    text_transform[5] = first_glyph.position[1];
    Geom::Affine clip_transform(text_transform);
    clip_transform[3] *= -1;
    gchar *path_transform = sp_svg_transform_write(clip_transform);
    gchar *transform = sp_svg_transform_write(text_transform);
    text_node->setAttribute("transform", transform);
    g_free(transform);

    bool new_tspan = true;
    bool dxIsDefault = true; //if dx attribut will have 0 in all position
    bool same_coords[2] = {true, true};
    Geom::Point last_delta_pos;
    unsigned int glyphs_in_a_row = 0;
    Inkscape::XML::Node *tspan_node = NULL;
    Glib::ustring x_coords;
    Glib::ustring dx_coords;
    Glib::ustring y_coords;
    Glib::ustring text_buffer;
    Glib::ustring glyphs_buffer;

    // Output all buffered glyphs
    while (1) {
        const SvgGlyph& glyph = (*i);
        std::vector<SvgGlyph>::iterator prev_iterator = i - 1;
        // Check if we need to make a new tspan
        if (glyph.style_changed) {
            new_tspan = true;
        } else if ( i != _glyphs.begin() ) {
            const SvgGlyph& prev_glyph = (*prev_iterator);
            float calc_dx = glyph.text_position[0] - prev_glyph.text_position[0] - prev_glyph.dx;
            if ( !( ( glyph.dy == 0.0 && prev_glyph.dy == 0.0 &&
                     glyph.text_position[1] == prev_glyph.text_position[1] ) ||
                    ( glyph.dx == 0.0 && prev_glyph.dx == 0.0 &&
                     glyph.text_position[0] == prev_glyph.text_position[0] ) ) ||
            		(calc_dx > 3 * _font_scaling) || (calc_dx < (-_font_scaling)) // start new TSPAN if we have gap (positive or negative)
            		/*||
            		// negative dx attribute can't be showing in mozilla
            		( calc_dx < (prev_glyph.dx/(-5)) && sp_use_dx_sh && text_buffer.length() > 0 && i != _glyphs.end())*/) {
            	new_tspan = true;
            }
        }

        // Create tspan node if needed
        if ( new_tspan || i == _glyphs.end() ) {
            if (tspan_node) {
                // Set the x and y coordinate arrays
                if ( same_coords[0] ) {
                        sp_repr_set_svg_double(tspan_node, "x", last_delta_pos[0]);
                } else {
                	if (sp_use_dx_sh) {
                		if (! dxIsDefault)
                			tspan_node->setAttribute("dx", dx_coords.c_str());
                		sp_repr_set_svg_double(tspan_node, "x", first_glyphX);
                		tspan_node->setAttribute("data-x", x_coords.c_str());
                	} else {
                		tspan_node->setAttribute("x", x_coords.c_str());
                	}
                    sp_svg_number_write_de(c, sizeof(c), lastDeltaX/glipCount, 8, -8);
                    tspan_node->setAttribute("data-dx", c);
                    glipCount = 0;
                }
                if ( same_coords[1] ) {
                    sp_repr_set_svg_double(tspan_node, "y", last_delta_pos[1]);
                } else {
                    tspan_node->setAttribute("y", y_coords.c_str());
                }
                TRACE(("tspan content: %s\n", text_buffer.c_str()));
                if ( glyphs_in_a_row > 1 ) {
                    tspan_node->setAttribute("sodipodi:role", "line");
                }
                // Add text content node to tspan
                Inkscape::XML::Node *text_content = _xml_doc->createTextNode(text_buffer.c_str());
                tspan_node->appendChild(text_content);
                Inkscape::GC::release(text_content);
                text_node->appendChild(tspan_node);
                // when text used as mask for image
                tspan_node->setAttribute("sodipodi:glyphs_list", glyphs_buffer.c_str());
                tspan_node->setAttribute("sodipodi:glyphs_transform", path_transform);
                Inkscape::CSSOStringStream os_endX;
                os_endX << tspanEndXPos;
                tspan_node->setAttribute("data-endX", os_endX.str().c_str());

                // Clear temporary buffers
                x_coords.clear();
                dx_coords.clear();
                y_coords.clear();
                text_buffer.clear();
                glyphs_buffer.clear();
                Inkscape::GC::release(tspan_node);
                glyphs_in_a_row = 0;
            }
            if ( i == _glyphs.end() ) {
                sp_repr_css_attr_unref((*prev_iterator).style);
                break;
            } else {
                tspan_node = _xml_doc->createElement("svg:tspan");
                Inkscape::CSSOStringStream os_spaceW;
                Inkscape::CSSOStringStream os_wordSpaceW;
                os_spaceW << glyph.spaceWidth * _font_scaling;
                os_wordSpaceW << glyph.wordSpace * _font_scaling;
                tspan_node->setAttribute("sodipodi:spaceWidth", os_spaceW.str().c_str());
                tspan_node->setAttribute("sodipodi:wordSpace", os_spaceW.str().c_str());
                dxIsDefault = true; // default dx will empty when all gaps is 0
                ///////
                // Create a font specification string and save the attribute in the style
                PangoFontDescription *descr = pango_font_description_from_string(glyph.font_specification);
                Glib::ustring properFontSpec = font_factory::Default()->ConstructFontSpecification(descr);
                pango_font_description_free(descr);
                sp_repr_css_set_property(glyph.style, "-inkscape-font-specification", properFontSpec.c_str());

                // Set style and unref SPCSSAttr if it won't be needed anymore
                // assume all <tspan> nodes in a <text> node share the same style
                sp_repr_css_change(text_node, glyph.style, "style");
                if ( glyph.style_changed && i != _glyphs.begin() ) {    // Free previous style
                    sp_repr_css_attr_unref((*prev_iterator).style);
                }
            }
            new_tspan = false;
        }
        if ( glyphs_in_a_row > 0 ) {
            x_coords.append(" ");
            dx_coords.append(" ");
            y_coords.append(" ");
            // Check if we have the same coordinates
            const SvgGlyph& prev_glyph = (*prev_iterator);
            for ( int p = 0 ; p < 2 ; p++ ) {
                if ( glyph.text_position[p] != prev_glyph.text_position[p] ) {
                    same_coords[p] = false;
                }
            }
        }


        // Append the coordinates to their respective strings
        Geom::Point delta_pos( glyph.text_position - first_glyph.text_position );
        delta_pos[1] += glyph.rise;
        delta_pos[1] *= -1.0;   // flip it
        delta_pos *= _font_scaling;
        Inkscape::CSSOStringStream os_x;
        Inkscape::CSSOStringStream os_dx;
        const SvgGlyph& prev_glyph = (*prev_iterator);
        os_x << delta_pos[0];
        if (glyph.text_position[0] != first_glyph.text_position[0] && dx_coords.length() > 0) {
        	float calc_dx = calculateSvgDx(glyph, prev_glyph, _font_scaling);

            if (fabs(calc_dx) >0.001) dxIsDefault = false; // if we always have dx~0 we do not create attribute
            os_dx << calc_dx;
        } else {
        	os_dx << 0; // we set "x" attribute for first element so dx always 0;
        }
        lastDeltaX = delta_pos[0];
        double originalDx = glyph.dx - prev_glyph.wordSpace;
        if (! glyph.font->getWMode()) {
        	originalDx -= glyph.charSpace;
        }
        tspanEndXPos = delta_pos[0] + originalDx * _font_scaling;

        if (glipCount == 0)
          first_glyphX = delta_pos[0];
        glipCount++;
        x_coords.append(os_x.str());
        dx_coords.append(os_dx.str());
        Inkscape::CSSOStringStream os_y;
        os_y << delta_pos[1];
        y_coords.append(os_y.str());
        last_delta_pos = delta_pos;

        // Append the character to the text buffer
	if ( !glyph.code.empty() ) {
            text_buffer.append(1, glyph.code[0]);
            // if it is macro glyph -treating
            if (glyph.code.size() > 1) {
            	for(int glyphNum = 1; glyphNum < glyph.code.size(); glyphNum++) {
            		if (glyph.code[glyphNum] <= 32 || // extra space is not allowed
            			glyph.code[glyphNum] == 0xAD || glyph.code[glyphNum] == 0x2010 || // Soft hyphen - had double in first position
            			glyph.code[glyphNum] == 0xA0 ) continue; // extra space is not allowed
            		text_buffer.append(1, glyph.code[glyphNum]);
            		x_coords.append(" ");
            		x_coords.append(os_x.str());

            		dx_coords.append(" 0 ");
            	}
            }

            void *tmpVoid = malloc(sizeof(SvgGlyph));
			memcpy(tmpVoid, &glyph, sizeof(SvgGlyph));
			g_ptr_array_add(glyphs_for_clips, tmpVoid);

			// when text used as mask of image
			Inkscape::CSSOStringStream n_glyph;
			n_glyph << glyphs_for_clips->len - 1;
			glyphs_buffer.append(n_glyph.str());
			glyphs_buffer.append(" ");
	}

        glyphs_in_a_row++;
        ++i;
    }
    _container->appendChild(text_node);
    Inkscape::GC::release(text_node);

    _glyphs.clear();
    g_free(path_transform);
}

void SvgBuilder::beginString(GfxState *state, GooString * /*s*/) {
    if (_need_font_update) {
        updateFont(state);
    }
    IFTRACE(double *m = state->getTextMat());
    TRACE(("tm: %f %f %f %f %f %f\n",m[0], m[1],m[2], m[3], m[4], m[5]));
    IFTRACE(m = state->getCTM());
    TRACE(("ctm: %f %f %f %f %f %f\n",m[0], m[1],m[2], m[3], m[4], m[5]));
}

/**
 * \brief Adds the specified character to the text buffer
 * Takes care of converting it to UTF-8 and generates a new style repr if style
 * has changed since the last call.
 */
void SvgBuilder::addChar(GfxState *state, double x, double y,
                         double dx, double dy,
                         double originX, double originY,
                         CharCode code, int /*nBytes*/, Unicode *u, int uLen) {


    bool is_space = ( uLen == 1 && u[0] == 32 );
    // Skip beginning space
    if ( is_space && _glyphs.empty()) {
        Geom::Point delta(dx, dy);
         _text_position += delta;
         return;
    }
    // Allow only one space in a row
    if ( is_space && (_glyphs[_glyphs.size() - 1].code.size() == 1) &&
         (_glyphs[_glyphs.size() - 1].code[0] == 32) ) {
        Geom::Point delta(dx, dy);
        _text_position += delta;
        return;
    }

    SvgGlyph new_glyph;
    new_glyph.is_space = is_space;
    new_glyph.position = Geom::Point( x - originX, y - originY );
    new_glyph.text_position = _text_position;
    new_glyph.dx = dx;
    new_glyph.dy = dy;
    new_glyph.gidCode = code;
    new_glyph.font = state->getFont();
    new_glyph.fontSize = state->getFontSize();
    new_glyph.charSpace = state->getCharSpace(); // used for dx calculate in _flushText
    new_glyph.spaceWidth = spaceWidth;
    new_glyph.wordSpace = state->getWordSpace();
    Geom::Point delta(dx, dy);
    _text_position += delta;

    // Convert the character to UTF-8 since that's our SVG document's encoding
    {
        gunichar2 uu[8] = {0};

        for (int i = 0; i < uLen; i++) {
            uu[i] = u[i];
        }

        gchar *tmp = g_utf16_to_utf8(uu, uLen, NULL, NULL, NULL);
        if ( tmp && *tmp ) {
            new_glyph.code = tmp;
        } else {
            new_glyph.code.clear();
        }
        g_free(tmp);
    }

    // Copy current style if it has changed since the previous glyph
    // todo: it can containts some glypg - some chars
    if (_invalidated_style || _glyphs.empty()) {
        new_glyph.style_changed = true;
        int render_mode = state->getRender();
        // Set style
        bool has_fill = !( render_mode & 1 );
        bool has_stroke = ( render_mode & 3 ) == 1 || ( render_mode & 3 ) == 2;
        new_glyph.style = _setStyle(state, has_fill, has_stroke);
        new_glyph.render_mode = render_mode;
        sp_repr_css_merge(new_glyph.style, _font_style); // Merge with font style
        _invalidated_style = false;
    } else {
        new_glyph.style_changed = false;
        // Point to previous glyph's style information
        const SvgGlyph& prev_glyph = _glyphs.back();
        new_glyph.style = prev_glyph.style;
        new_glyph.render_mode = prev_glyph.render_mode;
    }
    new_glyph.font_specification = _font_specification;
    new_glyph.rise = state->getRise();

    _glyphs.push_back(new_glyph);
}

char* SvgBuilder::glyphToPath(GfxState *state, CharCode codeCopy, Unicode uCopy) {
    SvgGlyph new_glyph;
	char *tail;
	static GfxFont      *font = nullptr; // often we need this trik only for one font in file so make this static
	static FT_Byte      *buf = nullptr;
	static FT_Face       face = nullptr;
    static FT_Library    ft_lib = nullptr;
    FT_Error      error;
    int len;

    if (ft_lib == nullptr)
    	FT_Init_FreeType(&ft_lib);

    if (font != state->getFont()) {
	    	if (buf) {
	    		FT_Done_Face(face);
	    		free(buf);
	    		buf = nullptr;
	    	}
	    	font = state->getFont();
		    FT_Byte *buf = (FT_Byte *)font->readEmbFontFile(_xref, &len);
		    error = FT_New_Memory_Face(ft_lib, buf, len, 0, &face);
	}

    //new_glyph.is_space = is_space;
    //new_glyph.position = Geom::Point( x - originX, y - originY );
    //new_glyph.text_position = _text_position;
    //new_glyph.dx = dx;
    //new_glyph.dy = dy;
    new_glyph.gidCode = codeCopy;
    new_glyph.font = state->getFont();
    new_glyph.fontSize = state->getFontSize();
    new_glyph.charSpace = state->getCharSpace(); // used for dx calculate in _flushText
    new_glyph.spaceWidth = spaceWidth;
    new_glyph.wordSpace = state->getWordSpace();

	return getGlyph(&new_glyph, face);
}

void SvgBuilder::endString(GfxState * /*state*/) {
}

void SvgBuilder::beginTextObject(GfxState *state) {
    _in_text_object = true;
    _invalidated_style = true;  // Force copying of current state
    _current_state = state;
}

void SvgBuilder::endTextObject(GfxState * /*state*/) {
    _flushText();
    // TODO: clip if render_mode >= 4
    _in_text_object = false;
}

/**
 * Helper functions for supporting direct PNG output into a base64 encoded stream
 */
void png_write_base64stream(png_structp png_ptr, png_bytep data, png_size_t length)
{
    Inkscape::IO::Base64OutputStream *stream =
            (Inkscape::IO::Base64OutputStream*)png_get_io_ptr(png_ptr); // Get pointer to stream
    for ( unsigned i = 0 ; i < length ; i++ ) {
        stream->put((int)data[i]);
    }
}

void png_flush_base64stream(png_structp png_ptr)
{
    Inkscape::IO::Base64OutputStream *stream =
            (Inkscape::IO::Base64OutputStream*)png_get_io_ptr(png_ptr); // Get pointer to stream
    stream->flush();
}

void spoolOriginalToFile(Stream *str, gchar *fileName) {
	//calculate length of file
	BaseStream *bStr = str->getBaseStream();
	int strLen = bStr->getLength();

	//get encrypted stream
	FilterStream *strFilter = (FilterStream *)str;
	Stream* strImg = strFilter->getNextStream();

	unsigned char *buffer;
	buffer = (unsigned char *)malloc(strLen);


    // read image stream to memory
    strImg->reset();
    strLen = strImg->doGetChars(strLen, (Guchar*)buffer);

    // JPEG data has to start with 0xFF 0xD8
    // but some pdf like the one on
    // https://bugs.freedesktop.org/show_bug.cgi?id=3299
    // does have some garbage before that this seeks for
    // the start marker...
    bool startFound = false;
    unsigned char c = 0, c2 = 0;
    int pos = 0;
    while (!startFound)
    {
      if (!c)
	  {
	    c = buffer[pos++];
	    if (c == -1)
	    {
	      error(errSyntaxError, -1, "Could not find start of jpeg data");
		  return;
	    }
	    if (c != 0xFF) c = 0;
	  }
      else
      {
        c2 = buffer[pos++];
        if (c2 != 0xD8)
        {
          c = 0;
          c2 = 0;
        }
        else
        	startFound = true;
      }
    }

	FILE *file = fopen(fileName, "w");
	fwrite(&buffer[pos - 2], 1, strLen-pos + 2, file);
	fclose(file);
	free(buffer);
}

unsigned char* SvgBuilder::_encodeImageAlphaMask(Stream *str, int width, int height,
                           GfxImageColorMap *color_map, bool interpolate) {
	// Convert pixels
	ImageStream *image_stream;
	if (color_map) {
		image_stream = new ImageStream(str, width, color_map->getNumPixelComps(),
									   color_map->getBits());
	} else {
		image_stream = new ImageStream(str, width, 1, 1);
	}
	image_stream->reset();

	// Convert grayscale values
	unsigned char *buffer = new unsigned char[width * height];

	for ( int y = 0 ; y < height ; y++ ) {
		unsigned char *row = image_stream->getLine();
		if (color_map) {
			color_map->getGrayLine(row, &buffer[y * width], width);
		} else {
			unsigned char *buf_ptr = &buffer[y * width];
			for ( int x = 0 ; x < width ; x++ ) {
				if ( row[x] ^ 0 ) {
					*buf_ptr++ = 0;
				} else {
					*buf_ptr++ = 0xFF;
				}
			}
		}
	}
	delete image_stream;
    str->close();

    return buffer;
}

void mergeWithAlpha(unsigned int* color_map, const unsigned char* alpha_map, const int color_len, const int map_len)
{
	for(int i = 0; i < color_len; i++) {
		unsigned char* color_byte = (unsigned char*) &color_map[i];
		color_byte[3] = ~alpha_map[(int)round(i*map_len/color_len)];
	}
}

Inkscape::XML::Node *SvgBuilder::_createMaskedImage(Stream *str, int width, int height,
                                  GfxImageColorMap *color_map, bool interpolate,
								  unsigned char* alphaChanel, int mask_width, int mask_height)  {
    // Decide whether we should embed this image
    int attr_value = 1;
    sp_repr_get_int(_preferences, "embedImages", &attr_value);
    bool embed_image = ( attr_value != 0 );
    // Decide whether use PNG render branch or save original JPEG stream
    bool isJpeg = FALSE;

    _countOfImages++;
    png_structp png_ptr;
    png_infop info_ptr;

	// Create PNG write struct
	png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if ( png_ptr == NULL ) {
		return NULL;
	}

	// Create PNG info struct
	info_ptr = png_create_info_struct(png_ptr);
	if ( info_ptr == NULL ) {
		png_destroy_write_struct(&png_ptr, NULL);
		return NULL;
	}
	// Set error handler
	if (setjmp(png_jmpbuf(png_ptr))) {
		png_destroy_write_struct(&png_ptr, &info_ptr);
		return NULL;
	}

    // Set read/write functions
    Inkscape::IO::StringOutputStream base64_string;
    Inkscape::IO::Base64OutputStream base64_stream(base64_string);
    FILE *fp = NULL;
    gchar *file_name = NULL; // href file name
    gchar *file_name_png = NULL;
    gchar *file_name_jpg = NULL;
    if (embed_image) {
        base64_stream.setColumnWidth(0);   // Disable line breaks
        png_set_write_fn(png_ptr, &base64_stream, png_write_base64stream, png_flush_base64stream);
    } else {
        int counter = getImageIngex();
        // build path of file for linking image
        file_name_png = g_strdup_printf("%s%s_img%d.png", sp_export_svg_path_sh, _docname, counter);
        file_name_jpg = g_strdup_printf("%s%s_img%d.jpg", sp_export_svg_path_sh, _docname, counter);

       	fp = fopen(file_name_png, "wb");
        // build link value for image
       	file_name = g_strdup_printf("%s_img%d.png", _docname, counter);

        if ( fp == NULL ) {
            png_destroy_write_struct(&png_ptr, &info_ptr);
            g_free(file_name);
            g_free(file_name_png);
            g_free(file_name_jpg);
            return NULL;
        }
        png_init_io(png_ptr, fp);
    }

	// Set header data ????

	png_set_invert_alpha(png_ptr);
	png_color_8 sig_bit;
	png_set_IHDR(png_ptr, info_ptr,
				 width,
				 height,
				 8, /* bit_depth */
				 PNG_COLOR_TYPE_RGB_ALPHA,
				 PNG_INTERLACE_NONE,
				 PNG_COMPRESSION_TYPE_BASE,
				 PNG_FILTER_TYPE_BASE);
	sig_bit.red = 8;
	sig_bit.green = 8;
	sig_bit.blue = 8;
	sig_bit.alpha = 8;

	png_set_sBIT(png_ptr, info_ptr, &sig_bit);
	png_set_bgr(png_ptr);
	// Write the file header
	png_write_info(png_ptr, info_ptr);

	// Convert pixels
	ImageStream *image_stream;
    if (color_map) {
		image_stream = new ImageStream(str, width,
									   color_map->getNumPixelComps(),
									   color_map->getBits());
		image_stream->reset();

		// Convert RGB values
		unsigned int *buffer = new unsigned int[width];

		for ( int i = 0 ; i < height ; i++ ) {
			unsigned char *row = image_stream->getLine();
			memset((void*)buffer, 0xff, sizeof(int) * width);
			color_map->getRGBLine(row, buffer, width);
			mergeWithAlpha(buffer, &alphaChanel[(int)(mask_width * round(1.0 * i * mask_height/height))], width, mask_width);
			png_write_row(png_ptr, (png_bytep)buffer);
		}

		delete [] buffer;

	} else {    // A colormap must be provided, so quit
		png_destroy_write_struct(&png_ptr, &info_ptr);
		if (!embed_image) {
			fclose(fp);
			g_free(file_name);
		}
		return NULL;
	}
	delete image_stream;
	// Close PNG
	png_write_end(png_ptr, info_ptr);
	png_destroy_write_struct(&png_ptr, &info_ptr);
	base64_stream.close();
    str->close();

    // Create repr
    Inkscape::XML::Node *image_node = _xml_doc->createElement("svg:image");
    sp_repr_set_svg_double(image_node, "width", 1);
    sp_repr_set_svg_double(image_node, "height", 1);
    if( !interpolate ) {
        SPCSSAttr *css = sp_repr_css_attr_new();
        // This should be changed after CSS4 Images widely supported.
        sp_repr_css_set_property(css, "image-rendering", "optimizeSpeed");
        sp_repr_css_change(image_node, css, "style");
        sp_repr_css_attr_unref(css);
    }

    // PS/PDF images are placed via a transformation matrix, no preserveAspectRatio used
    image_node->setAttribute("preserveAspectRatio", "none");

    // Set transformation

        svgSetTransform(image_node, 1.0, 0.0, 0.0, -1.0, 0.0, 1.0);

    // Create href
    if (embed_image) {
        // Append format specification to the URI
        Glib::ustring& png_data = base64_string.getString();
        png_data.insert(0, "data:image/png;base64,");
        image_node->setAttribute("xlink:href", png_data.c_str());
    } else {
    	fclose(fp);
        image_node->setAttribute("xlink:href", file_name);

        g_free(file_name);
        g_free(file_name_png);
        g_free(file_name_jpg);
    }

    image_node->setAttribute("sodipodi:img_width", std::to_string(width).c_str());
    image_node->setAttribute("sodipodi:img_height", std::to_string(height).c_str());
    return image_node;
}

/**
 * \brief Creates an <image> element containing the given ImageStream as a PNG
 *
 */
Inkscape::XML::Node *SvgBuilder::_createImage(Stream *str, int width, int height,
                                              GfxImageColorMap *color_map, bool interpolate,
                                              int *mask_colors, bool alpha_only,
                                              bool invert_alpha) {
    // Decide whether we should embed this image
    int attr_value = 1;
    sp_repr_get_int(_preferences, "embedImages", &attr_value);
    bool embed_image = ( attr_value != 0 );
    // Decide whether use PNG render branch or save original JPEG stream
    bool isJpeg = FALSE;
    Object o;
    str->getDict()->lookup("Filter", &o);
    if (o.getType() == objName) {
		char *gStr = o.getName();
		isJpeg = strcmp(gStr,"DCTDecode") == 0;
    }
    o.free();
	bool makeOriginalImage = ( sp_try_origin_jpeg_sp && //CLI option
			                   isJpeg &&
			                   (! embed_image) && ((color_map == 0) || (color_map->getNumPixelComps() == 3)) &&
			                   (mask_colors == 0) &&
			                   (! alpha_only) &&
							   (! invert_alpha));
    _countOfImages++;
    png_structp png_ptr;
    png_infop info_ptr;
    if (! makeOriginalImage) {
		// Create PNG write struct
		png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
		if ( png_ptr == NULL ) {
			return NULL;
		}

		// Create PNG info struct
		info_ptr = png_create_info_struct(png_ptr);
		if ( info_ptr == NULL ) {
			png_destroy_write_struct(&png_ptr, NULL);
			return NULL;
		}
		// Set error handler
		if (setjmp(png_jmpbuf(png_ptr))) {
			png_destroy_write_struct(&png_ptr, &info_ptr);
			return NULL;
		}
    }

    // Set read/write functions
    Inkscape::IO::StringOutputStream base64_string;
    Inkscape::IO::Base64OutputStream base64_stream(base64_string);
    FILE *fp = NULL;
    gchar *file_name = NULL; // href file name
    gchar *file_name_png = NULL;
    gchar *file_name_jpg = NULL;
    if (embed_image) {
        base64_stream.setColumnWidth(0);   // Disable line breaks
        png_set_write_fn(png_ptr, &base64_stream, png_write_base64stream, png_flush_base64stream);
    } else {
        int counter = getImageIngex();
        // build path of file for linking image
        file_name_png = g_strdup_printf("%s%s_img%d.png", sp_export_svg_path_sh, _docname, counter);
        file_name_jpg = g_strdup_printf("%s%s_img%d.jpg", sp_export_svg_path_sh, _docname, counter);

        if (! makeOriginalImage)
        	fp = fopen(file_name_png, "wb");
        // build link value for image
        if (sp_create_jpeg_sp || makeOriginalImage) {
            file_name = g_strdup_printf("%s_img%d.jpg", _docname, counter); // for href attribute
            if (makeOriginalImage)
            	spoolOriginalToFile(str, file_name_jpg);
        }
        else
        	file_name = g_strdup_printf("%s_img%d.png", _docname, counter); // for href attribute

        if ( fp == NULL && (! makeOriginalImage)) {
            png_destroy_write_struct(&png_ptr, &info_ptr);
            g_free(file_name);
            g_free(file_name_png);
            g_free(file_name_jpg);
            return NULL;
        }
        if (! makeOriginalImage) png_init_io(png_ptr, fp);
    }

    if (! makeOriginalImage) {
		// Set header data
		if ( !invert_alpha && !alpha_only ) {
			png_set_invert_alpha(png_ptr);
		}
		png_color_8 sig_bit;
		if (alpha_only) {
			png_set_IHDR(png_ptr, info_ptr,
						 width,
						 height,
						 8, /* bit_depth */
						 PNG_COLOR_TYPE_GRAY,
						 PNG_INTERLACE_NONE,
						 PNG_COMPRESSION_TYPE_BASE,
						 PNG_FILTER_TYPE_BASE);
			sig_bit.red = 0;
			sig_bit.green = 0;
			sig_bit.blue = 0;
			sig_bit.gray = 8;
			sig_bit.alpha = 0;
		} else {
			png_set_IHDR(png_ptr, info_ptr,
						 width,
						 height,
						 8, /* bit_depth */
						 PNG_COLOR_TYPE_RGB_ALPHA,
						 PNG_INTERLACE_NONE,
						 PNG_COMPRESSION_TYPE_BASE,
						 PNG_FILTER_TYPE_BASE);
			sig_bit.red = 8;
			sig_bit.green = 8;
			sig_bit.blue = 8;
			sig_bit.alpha = 8;
		}
		png_set_sBIT(png_ptr, info_ptr, &sig_bit);
		png_set_bgr(png_ptr);
		// Write the file header
		png_write_info(png_ptr, info_ptr);

		// Convert pixels
		ImageStream *image_stream;
		if (alpha_only) {
			if (color_map) {
				image_stream = new ImageStream(str, width, color_map->getNumPixelComps(),
											   color_map->getBits());
			} else {
				image_stream = new ImageStream(str, width, 1, 1);
			}
			image_stream->reset();

			// Convert grayscale values
			unsigned char *buffer = new unsigned char[width];
			int invert_bit = invert_alpha ? 1 : 0;
			for ( int y = 0 ; y < height ; y++ ) {
				unsigned char *row = image_stream->getLine();
				if (color_map) {
					color_map->getGrayLine(row, buffer, width);
				} else {
					unsigned char *buf_ptr = buffer;
					for ( int x = 0 ; x < width ; x++ ) {
						if ( row[x] ^ invert_bit ) {
							*buf_ptr++ = 0;
						} else {
							*buf_ptr++ = 255;
						}
					}
				}
				png_write_row(png_ptr, (png_bytep)buffer);
			}
			delete [] buffer;
		} else if (color_map) {
			image_stream = new ImageStream(str, width,
										   color_map->getNumPixelComps(),
										   color_map->getBits());
			image_stream->reset();

			// Convert RGB values
			unsigned int *buffer = new unsigned int[width];
			if (mask_colors) {
				for ( int y = 0 ; y < height ; y++ ) {
					unsigned char *row = image_stream->getLine();
					color_map->getRGBLine(row, buffer, width);

					unsigned int *dest = buffer;
					for ( int x = 0 ; x < width ; x++ ) {
						// Check each color component against the mask
						for ( int i = 0; i < color_map->getNumPixelComps() ; i++) {
							if ( row[i] < mask_colors[2*i] * 255 ||
								 row[i] > mask_colors[2*i + 1] * 255 ) {
								*dest = *dest | 0xff000000;
								break;
							}
						}
						// Advance to the next pixel
						row += color_map->getNumPixelComps();
						dest++;
					}
					// Write it to the PNG
					png_write_row(png_ptr, (png_bytep)buffer);
				}
			} else {
				for ( int i = 0 ; i < height ; i++ ) {
					unsigned char *row = image_stream->getLine();
					memset((void*)buffer, 0xff, sizeof(int) * width);
					color_map->getRGBLine(row, buffer, width);
					png_write_row(png_ptr, (png_bytep)buffer);
				}
			}
			delete [] buffer;

		} else {    // A colormap must be provided, so quit
			png_destroy_write_struct(&png_ptr, &info_ptr);
			if (!embed_image) {
				fclose(fp);
				g_free(file_name);
			}
			return NULL;
		}
		delete image_stream;
		// Close PNG
		png_write_end(png_ptr, info_ptr);
		png_destroy_write_struct(&png_ptr, &info_ptr);
		base64_stream.close();
    }
    str->close();

    // Create repr
    Inkscape::XML::Node *image_node = _xml_doc->createElement("svg:image");
    sp_repr_set_svg_double(image_node, "width", 1);
    sp_repr_set_svg_double(image_node, "height", 1);
    if( !interpolate ) {
        SPCSSAttr *css = sp_repr_css_attr_new();
        // This should be changed after CSS4 Images widely supported.
        sp_repr_css_set_property(css, "image-rendering", "optimizeSpeed");
        sp_repr_css_change(image_node, css, "style");
        sp_repr_css_attr_unref(css);
    }

    // PS/PDF images are placed via a transformation matrix, no preserveAspectRatio used
    image_node->setAttribute("preserveAspectRatio", "none");

    // Set transformation

        svgSetTransform(image_node, 1.0, 0.0, 0.0, -1.0, 0.0, 1.0);

    // Create href
    if (embed_image) {
        // Append format specification to the URI
        Glib::ustring& png_data = base64_string.getString();
        png_data.insert(0, "data:image/png;base64,");
        image_node->setAttribute("xlink:href", png_data.c_str());
    } else {
    	if (! makeOriginalImage) fclose(fp);
        image_node->setAttribute("xlink:href", file_name);
        if (sp_create_jpeg_sp && (! makeOriginalImage)) {
			gchar *cmd = g_strdup_printf("convert %s -background white -flatten %s",
										 file_name_png, file_name_jpg);
			system(cmd);
			remove(file_name_png);
			g_free(cmd);
        }
        g_free(file_name);
        g_free(file_name_png);
        g_free(file_name_jpg);
    }

    image_node->setAttribute("sodipodi:img_width", std::to_string(width).c_str());
    image_node->setAttribute("sodipodi:img_height", std::to_string(height).c_str());
    return image_node;
}

/**
 * \brief Creates a <mask> with the specified width and height and adds to <defs>
 *  If we're not the top-level SvgBuilder, creates a <defs> too and adds the mask to it.
 * \return the created XML node
 */
Inkscape::XML::Node *SvgBuilder::_createMask(double width, double height) {
    Inkscape::XML::Node *mask_node = _xml_doc->createElement("svg:mask");
    mask_node->setAttribute("maskUnits", "userSpaceOnUse");
    sp_repr_set_svg_double(mask_node, "x", 0.0);
    sp_repr_set_svg_double(mask_node, "y", 0.0);
    sp_repr_set_svg_double(mask_node, "width", width);
    sp_repr_set_svg_double(mask_node, "height", height);
    // Append mask to defs
    if (_is_top_level) {
        _doc->getDefs()->getRepr()->appendChild(mask_node);
        Inkscape::GC::release(mask_node);
        return _doc->getDefs()->getRepr()->lastChild();
    } else {    // Work around for renderer bug when mask isn't defined in pattern
        static int mask_count = 0;
        Inkscape::XML::Node *defs = _root->firstChild();
        if ( !( defs && !strcmp(defs->name(), "svg:defs") ) ) {
            // Create <defs> node
            defs = _xml_doc->createElement("svg:defs");
            _root->addChild(defs, NULL);
            Inkscape::GC::release(defs);
            defs = _root->firstChild();
        }
        gchar *mask_id = g_strdup_printf("_mask%d", mask_count++);
        mask_node->setAttribute("id", mask_id);
        g_free(mask_id);
        defs->appendChild(mask_node);
        Inkscape::GC::release(mask_node);
        return defs->lastChild();
    }
}

#define GG_STATE_START 0
#define GG_STATE_CONT 1
#define FT_face_getTag(F, P) (FT_CURVE_TAG(F->glyph->outline.tags[P]))
#define GLYPH_SCALE 0.00001
#define gLibUstrAppendCoord(USTR, D) { \
    Inkscape::CSSOStringStream coord; \
	coord << D * GLYPH_SCALE; USTR.append(coord.str()); USTR.append(" "); \
}

char *SvgBuilder::getGlyph(SvgGlyph *svgGlyph, FT_Face face) {
	int firstPoint;
	int lastPoint;
	Glib::ustring path;
	FT_Error      error;
	FT_GlyphSlot  slot;

	error = FT_Set_Char_Size( face, 0, (uint)(svgGlyph->fontSize * 100000 * _font_scaling),
	                            0, 72);                /* set character size */
	slot = face->glyph;
	/* load glyph image into the slot (erase previous one) */
	uint state = 0;
	uint countour = 0;
	if (FT_Load_Glyph(face, (FT_UInt)svgGlyph->gidCode, FT_LOAD_NO_BITMAP) == 0) {
	  /*int ptest = 0;
	  printf("-=%i=-", svgGlyph->gidCode);
	  while(ptest < face->glyph->outline.n_points) {
		FT_Pos x = face->glyph->outline.points[ptest].x;
		FT_Pos y = face->glyph->outline.points[ptest].y;
	    printf("%i\t%i\t%i\n", x, y, FT_face_getTag(face, ptest));
	    ptest++;
	  }*/
	  int p = 0;
	  int pStub = -1; // If p do not changed during one loop mind it can not build curve so we must go out

	  while(p < face->glyph->outline.n_points) {
		if (p == pStub) return g_strdup("");
		pStub = p;
		FT_Pos x = face->glyph->outline.points[p].x;
		FT_Pos y = face->glyph->outline.points[p].y;
		char tag = FT_CURVE_TAG(face->glyph->outline.tags[p]);

	    if (state == GG_STATE_START) {
	    	// TODO: if first point is not FT_CURVE_TAG_ON need calculate virtual start point or
	    	//       take last point in contur
	    	path.append("M");
	    	firstPoint = p;

	    	if (face->glyph->outline.n_contours > countour)
	    	  lastPoint = face->glyph->outline.contours[countour];
	    	else
	    	  lastPoint = face->glyph->outline.n_points - 1;


	    	if (tag != FT_CURVE_TAG_CONIC) {
				gLibUstrAppendCoord(path, x);
				gLibUstrAppendCoord(path, y);
				p++;
	    	} else {
	    		pStub = -1;
	    		char tag2 = FT_CURVE_TAG(face->glyph->outline.tags[lastPoint]);
	    		if (tag2 != FT_CURVE_TAG_CONIC) {
					gLibUstrAppendCoord(path, face->glyph->outline.points[lastPoint].x);
					gLibUstrAppendCoord(path, face->glyph->outline.points[lastPoint].y);
	    		} else {
	    			gLibUstrAppendCoord(path, (face->glyph->outline.points[lastPoint].x + x)/2);
	    			gLibUstrAppendCoord(path, (face->glyph->outline.points[lastPoint].y + y)/2);
	    		}
	    	}
	    	state++;
	    	continue;
	    }

	    //draw curve
	    if (state == GG_STATE_CONT) {
	    	uint prevTag = FT_face_getTag(face, p-1);
	    	int nextP = (p == lastPoint ? firstPoint : p + 1);
	    	uint nextTag = FT_face_getTag(face, nextP);

            // https://www.freetype.org/freetype2/docs/glyphs/glyphs-6.html
	    	// Build current curve and skip used points
	    	switch (tag) {
	    	    case FT_CURVE_TAG_ON :
	    	    	path.append("L");
	    	    	gLibUstrAppendCoord(path, x);
	    	        gLibUstrAppendCoord(path, y);
	    	        p++;
	    	    	break;

	    	    case FT_CURVE_TAG_CONIC :
	    	    	// simple curve
	    	   		if ((prevTag == FT_CURVE_TAG_ON || prevTag == FT_CURVE_TAG_CONIC)  &&
	    	   				nextTag == FT_CURVE_TAG_ON) {
	    			    path.append("Q");
	    			    gLibUstrAppendCoord(path, x);
	    			    gLibUstrAppendCoord(path, y);
	    			    FT_Vector nextVector = face->glyph->outline.points[nextP];
	    			    gLibUstrAppendCoord(path, nextVector.x);
	    			    gLibUstrAppendCoord(path, nextVector.y);
	    			    path.append("\n");
	    			    p += 2;
	    		    }
	    	   		// first step from two step curve
	    	    	if ((prevTag == FT_CURVE_TAG_ON || prevTag == FT_CURVE_TAG_CONIC)  &&
	    	    			nextTag == FT_CURVE_TAG_CONIC) {
	    			    path.append("Q");
	    			    gLibUstrAppendCoord(path, x);
	    			    gLibUstrAppendCoord(path, y);
	    			    FT_Vector nextVector = face->glyph->outline.points[nextP];
	    			    // Calculate virtual point
	    			    gLibUstrAppendCoord(path, (x + nextVector.x)/2);
	    			    gLibUstrAppendCoord(path, (y + nextVector.y)/2);
	    			    p += 2;
	    			    // second step from two step curve
	    			    int endVecNum = (p > lastPoint) ? firstPoint : p;
	    			    char endPointTag = FT_CURVE_TAG(face->glyph->outline.tags[p]);
	    			    FT_Vector nextVector2 = face->glyph->outline.points[endVecNum];
	    			    if (endPointTag == FT_CURVE_TAG_ON) {
							path.append("\nT");
							gLibUstrAppendCoord(path, nextVector2.x);
							gLibUstrAppendCoord(path, nextVector2.y);
							path.append("\n");
							p++;
	    			    }
	    			    // if next point is CONIC curve we must have middle virtual point
	    			    if (endPointTag == FT_CURVE_TAG_CONIC) {
	    			    	path.append("\nT");
	    			    	// Calculate virtual point
	    			        gLibUstrAppendCoord(path, (nextVector.x + nextVector2.x)/2);
	    			        gLibUstrAppendCoord(path, (nextVector.y + nextVector2.y)/2);
	    			    }
	    		    }

	    		    break; // end of FT_CURVE_TAG_CONIC
	    	    case FT_CURVE_TAG_CUBIC :
	    	    	path.append("C");
					gLibUstrAppendCoord(path, x);
					gLibUstrAppendCoord(path, y);
					FT_Vector nextVector = face->glyph->outline.points[nextP];
					gLibUstrAppendCoord(path, nextVector.x);
					gLibUstrAppendCoord(path, nextVector.y);
					p += 2;
					if (p > lastPoint) nextVector = face->glyph->outline.points[firstPoint];
					else nextVector = face->glyph->outline.points[p];
					gLibUstrAppendCoord(path, nextVector.x);
					gLibUstrAppendCoord(path, nextVector.y);
					path.append("\n");
					p++;
	    	    	break;
	    	}

	    	if (p > lastPoint) {
	    		p = lastPoint + 1;
	    	    countour++;
	    	    path.append("Z \n");
	    	    state = GG_STATE_START;
	    	}
	    	continue;
	    }
	    p++;
	  }
	}
	return g_strdup(path.c_str());
}

FT_GlyphSlot SvgBuilder::getFTGlyph(GfxFont *font, double fontSize, uint gidCode, unsigned long int zoom) {

	static GfxFont      *_font = nullptr;
	static FT_Face       face;
    static FT_Library    ft_lib;
    static FT_Byte      *buf = nullptr;
    int len;
    FT_Error      error;

    // first entry - should init library
    if (_font == nullptr)
    	FT_Init_FreeType(&ft_lib);

    // load new font
    if (_font != font)
    {
    	if (buf != nullptr)
    		free(buf);
		_font = font;
		FT_Byte *buf = (FT_Byte *)_font->readEmbFontFile(_xref, &len);
		error = FT_New_Memory_Face(ft_lib, buf, len, 0, &face);
    }

    // set zoom of glyph
	error = FT_Set_Char_Size( face, 0, (uint)(fontSize * zoom),
	                            0, 72);                /* set character size */

	// load current glyph
	if (FT_Load_Glyph(face, (FT_UInt)gidCode, FT_LOAD_NO_BITMAP) == 0) {
	  return face->glyph;
	} else
		return nullptr;

}

void lookUpTspans(Inkscape::XML::Node *container, GPtrArray *result) {
	Inkscape::XML::Node *ch = container->firstChild();
	while(ch) {
		if (strcmp(ch->name(), "svg:tspan") == 0) {
			g_ptr_array_add(result, ch);
			ch = ch->next();
			continue;
		}
		if (ch->childCount() > 0)
			lookUpTspans(ch, result);
		ch = ch->next();
	}
}

void SvgBuilder::adjustEndX()
{
#define TO_ROOT 1
#define TO_CONT 2
	Inkscape::XML::Node *container;
	if (sp_scale_endx_sp & TO_CONT)
		container = this->getContainer();
	else if (sp_scale_endx_sp & TO_ROOT)
		container =getRoot();
	else return;
	SPItem *spCont = (SPItem*)_doc->getObjectByRepr(container);
	GPtrArray *listSpans = g_ptr_array_new();
	lookUpTspans(container, listSpans);

    Inkscape::XML::Node *tmpNode;
    for(int i = 0; i < listSpans->len; i++) {
    	// calculate geometry of tspan
    	tmpNode = (Inkscape::XML::Node *)g_ptr_array_index(listSpans, i);
    	SPItem *spNode = (SPItem*)_doc->getObjectByRepr(tmpNode);
    	//Geom::OptRect visualBound(spNode->visualBounds());
    	Geom::Affine affine= spNode->getRelativeTransform(spCont);
    	double endX = 0;
    	sp_repr_get_double(tmpNode, "data-endX", &endX);
    	Geom::Point position = Geom::Point(endX, 0) * affine;
    	sp_repr_set_svg_double(tmpNode, "data-endX", position.x());
    }


	g_ptr_array_free(listSpans, false);
}

SvgBuilder::todoRemoveClip SvgBuilder::checkClipAroundText(Inkscape::XML::Node *gNode)
{
	/*
	 Node should keep structuere:
	 <g clip-path="#url(XXX)">
	 	 <g>
	 	 <text><tspan></tspan></text>
	 	 </g>
	 </g>

	 or

	 <g clip-path="#url(XXX)">
	 	 <text><tspan></tspan></text>
	 </g>

	 or

	 <g clip-path="#url(XXX)">
	 </g>

	 */
	if (strcmp(gNode->name(), "svg:g") != 0) return CLIP_NOTFOUND;

	//if (gNode->childCount() == 0) return CLIP_NOTFOUND;
	Inkscape::XML::Node *firstChild = gNode->firstChild();
	Inkscape::XML::Node *firstFirstChild = nullptr;
	if (firstChild != nullptr)
		firstFirstChild = firstChild->firstChild();
	if (firstChild)
	{
		if (strcmp(firstChild->name(), "svg:text") != 0)
		{
			if (strcmp(firstChild->name(), "svg:g") != 0) return CLIP_NOTFOUND;
			if (firstFirstChild == nullptr) return CLIP_NOTFOUND;
			if (strcmp(firstFirstChild->name(), "svg:text") != 0) return CLIP_NOTFOUND;
		}
	}

	const char* clipUrl = gNode->attribute("clip-path");
	if (clipUrl == nullptr) return CLIP_NOTFOUND;

	gchar clip_path_id[32];
	strncpy(clip_path_id, &clipUrl[5], strlen(clipUrl) - 6);
	clip_path_id[strlen(clipUrl) - 6] = 0;
	SPDocument* spDoc = this->getSpDocument();
	SPClipPath* spClipPath = (SPClipPath*)spDoc->getObjectById(clip_path_id);
	SPItem* spGNode = (SPItem*)spDoc->getObjectByRepr(gNode);
	Geom::Affine affine = spGNode->getRelativeTransform(spDoc->getRoot());

	Geom::OptRect bbox = spClipPath->geometricBounds(affine);
	if (firstChild == nullptr || bbox.contains(spGNode->geometricBounds(affine)))
	{
		gNode->setAttribute("clip-path", nullptr);
		Inkscape::XML::Node* clipNode = spClipPath->getRepr();
		clipNode->parent()->removeChild(clipNode);
		return REMOVE_CLIP;
	}

	return CLIP_NOTFOUND;
}

double SvgBuilder::fetchAverageColor(Inkscape::XML::Node *container, Inkscape::XML::Node *image_node)
{
	// if we have not any text in the container - skip this function
	GPtrArray *listSpans = g_ptr_array_new();
	lookUpTspans(container, listSpans);
	const guint size = listSpans->len;
	if (size == 0)
	{
		g_ptr_array_free(listSpans, false);
		return 0;
	}

	// for normal rendering - we should set current folder = SVG's folder
	char current_path[PATH_MAX];
	// remember current folder
	getwd(current_path);
	chdir(sp_export_svg_path_sh);

	_doc->ensureUpToDate();
	// calculate geometry params for image
	SPItem *imgItem = (SPItem*)_doc->getObjectByRepr(image_node);
	Geom::OptRect imgVisualBound(imgItem->visualBounds());
	Geom::Affine imgAffine= imgItem->getRelativeTransform(_doc->getRoot());
	Geom::Rect imgSqBBox = imgVisualBound.get() * imgAffine;
	Geom::IntRect imgIntBBox(
			round(imgSqBBox[Geom::X][0]),
			round(imgSqBBox[Geom::Y][0]),
			round(imgSqBBox[Geom::X][1]),
			round(imgSqBBox[Geom::Y][1]));


	// calculate geometry params for text
	SPItem *textItem = (SPItem*)_doc->getObjectByRepr(container);
	Geom::Affine textAffine= textItem->getRelativeTransform(_doc->getRoot());
	Geom::OptRect textVisualBound(textItem->visualBounds());
	Geom::Rect textSqBBox = textVisualBound.get() * textAffine;
	Geom::IntRect textIntBBox(
			round(textSqBBox[Geom::X][0]),
			round(textSqBBox[Geom::Y][0]),
			round(textSqBBox[Geom::X][1]),
			round(textSqBBox[Geom::Y][1]));

    /* Create new drawing */
    Inkscape::Drawing drawing;
    //drawing.setExact(true); // export with maximum blur rendering quality
    unsigned const dkey = SPItem::display_key_new(1);

    // Create ArenaItems and set transform
    drawing.setRoot(imgItem->invoke_show(drawing, dkey, SP_ITEM_SHOW_DISPLAY));
    drawing.root()->setTransform(imgAffine);
    /* Update to renderable state */
    drawing.update(imgIntBBox);

    // allocate memory for pixels array
    unsigned long int width = abs(textIntBBox[Geom::X][1] - textIntBBox[Geom::X][0]);
    unsigned long int num_rows = abs(textIntBBox[Geom::Y][1] - textIntBBox[Geom::Y][0]);;
    int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, width);
    unsigned char *px = g_new(guchar, num_rows * stride);

    // fill surface - background color
    cairo_surface_t *s = cairo_image_surface_create_for_data(
        (unsigned char *)px, CAIRO_FORMAT_ARGB32, width, num_rows, stride);
    Inkscape::DrawingContext dc(s, textSqBBox.min());
    dc.setSource(0xFFFFFF00);
    dc.setOperator(CAIRO_OPERATOR_SOURCE);
    dc.paint();
    dc.setOperator(CAIRO_OPERATOR_OVER);

    /* Render */
    drawing.render(dc, textIntBBox);
    chdir(current_path);
    cairo_status_t state = cairo_surface_write_to_png(s, "test_file.png");
    cairo_surface_destroy(s);

    // calculate color for each tspan
    Inkscape::XML::Node *tmpNode;
    uint64_t textSquare = 0;
	float texta = 0;
	float textr = 0;
	float textg = 0;
	float textb = 0;
    for(int i = 0; i < listSpans->len; i++) {
    	// calculate geometry of tspan
    	tmpNode = (Inkscape::XML::Node *)g_ptr_array_index(listSpans, i);
    	SPItem *tmpSpItem = (SPItem*)_doc->getObjectByRepr(tmpNode);
    	Geom::Affine tspanAffine= tmpSpItem->getRelativeTransform(_doc->getRoot());
    	Geom::OptRect tspanVisualBound(tmpSpItem->visualBounds());
    	Geom::Rect tspanSqBBox = tspanVisualBound.get() * tspanAffine *
    			Geom::Translate(-textIntBBox[Geom::X][0], -textIntBBox[Geom::Y][0]);
    	uint64_t r = 0;
    	uint64_t g = 0;
    	uint64_t b = 0;
    	uint64_t a = 0;
    	// set up geometry of tspan
    	int x1 = round(tspanSqBBox[Geom::X][0]);
    	int y1 = round(tspanSqBBox[Geom::Y][0]);
    	int x2 = round(tspanSqBBox[Geom::X][1]);
    	int y2 = round(tspanSqBBox[Geom::Y][1]);
    	if (x1 == x2) x2++;
    	if (y1 == y2) y2++;
    	uint64_t square = abs((x2-x1) * (y2 -y1));
    	if (square == 0) continue;
    	// averige color
    	for(int rowIdx = y1; rowIdx < y2; rowIdx++ )
    	{
    		for(int colIdx = x1 * 4; colIdx < x2 * 4; colIdx += 4)
    		{
    			uint32_t pointIdx = rowIdx * stride + colIdx;
    			r += px[pointIdx];
				g += px[pointIdx+1];
				b += px[pointIdx+2];
				a += px[pointIdx+3];
    		}
    	}

    	//set up fill attribute
    	float ia = a/square;
    	float ir = r/square;
    	float ig = g/square;
    	float ib = b/square;

    	uint64_t commonSquare = textSquare + square;
    	texta = texta * textSquare/commonSquare + ia * square/commonSquare;
    	textr = textr * textSquare/commonSquare + ir * square/commonSquare;
    	textg = textg * textSquare/commonSquare + ig * square/commonSquare;
    	textb = textb * textSquare/commonSquare + ib * square/commonSquare;
    	textSquare = commonSquare;
    }
	char fill[16];
	char opacity[16];
	sprintf(fill, "#%02x%02x%02x", (uint)round(textr), (uint)round(textg), (uint)round(textb));
    for(int i = 0; i < listSpans->len; i++) {
    	// calculate geometry of tspan
    	tmpNode = (Inkscape::XML::Node *)g_ptr_array_index(listSpans, i);

    	//sprintf(opacity, "%f", ia/255);
    	SPCSSAttr *style = sp_repr_css_attr(tmpNode->parent(), "style");
    	sp_repr_css_set_property(style, "fill", fill);
    	//sp_repr_css_set_property(style, "fill-opacity", opacity);
    	Glib::ustring strCss;
    	sp_repr_css_write_string(style, strCss);
    	tmpNode->parent()->setAttribute("style", strCss.c_str());
    }


    // cleanup
    g_free(px);
    // release item
    imgItem->invoke_hide(dkey);
    g_ptr_array_free(listSpans, false);

	return size;
}

const char *SvgBuilder::generateClipsFormLetters(Inkscape::XML::Node *container) {

	Inkscape::XML::Node *ch = container->firstChild();
	Inkscape::XML::Node *clipNode = 0;
	GPtrArray *listSpans = g_ptr_array_new();
	lookUpTspans(container, listSpans);
	int len;
	char *tail;
	GfxFont      *font = 0;
	FT_Face       face;
    FT_Library    ft_lib;
    FT_Byte      *buf = 0;
    FT_Error      error;
	FT_Init_FreeType(&ft_lib);

	Inkscape::XML::Node *tmpNode;
	for(int i = 0; i < listSpans->len; i++) {
		tmpNode = (Inkscape::XML::Node *)g_ptr_array_index(listSpans, i);
		const char *n_list = tmpNode->attribute("sodipodi:glyphs_list");
		const char *listEnd = n_list + strlen(n_list);
		tail = 0;

		while(n_list <  listEnd) {
			char *clipPath;
			if (! clipNode) {
				clipNode = _xml_doc->createElement("svg:clipPath");
			    clipNode->setAttribute("clipPathUnits", "userSpaceOnUse");
			}
 		    int idx = strtof(n_list, &tail); n_list = tail + 1;
 		    SvgGlyph *glyph = (SvgGlyph *)g_ptr_array_index(glyphs_for_clips, idx);
            // If current glyph have new font - init face
 		    if (font != glyph->font) {
 		    	if (buf) {
 		    		FT_Done_Face(face);
 		    		free(buf);
 		    		buf = 0;
 		    	}
 		    	font = glyph->font;
 			    FT_Byte *buf = (FT_Byte *)font->readEmbFontFile(_xref, &len);
 			    error = FT_New_Memory_Face(ft_lib, buf, len, 0, &face);
 		    }
 		    clipPath = getGlyph(glyph, face);
 		    Inkscape::XML::Node *genPath = _xml_doc->createElement("svg:path");
 		    genPath->setAttribute("style", "clip-rule:evenodd");
 		    genPath->setAttribute("transform", tmpNode->attribute("sodipodi:glyphs_transform"));
 		    genPath->setAttribute("d", clipPath);
 		    clipNode->appendChild(genPath);
 		    Inkscape::GC::release(genPath);
 		    free(clipPath);
		}
	}

	if (buf) {
		FT_Done_Face(face);
		free(buf);
		buf = 0;
	}
	g_ptr_array_free(listSpans, false);
	FT_Done_FreeType(ft_lib);

    if (clipNode) {
    	_doc->getDefs()->getRepr()->appendChild(clipNode);
    	Inkscape::GC::release(clipNode);
    	return clipNode->attribute("id");
    }
    else return 0;
}

static void removeImg(const char* fileName)
{
	char current_path[PATH_MAX];
	getwd(current_path);
	chdir(sp_export_svg_path_sh);
	remove(fileName);
	chdir(current_path);
}

void SvgBuilder::addImage(GfxState * /*state*/, Stream *str, int width, int height,
                          GfxImageColorMap *color_map, bool interpolate, int *mask_colors) {

     Inkscape::XML::Node *image_node = _createImage(str, width, height, color_map, interpolate, mask_colors);

     //const char *clipId = generateClipsFormLetters(_container);
     /*if (clipId && image_node) {
    	 Inkscape::XML::Node *gNode = _xml_doc->createElement("svg:g");
    	 gchar *urltext = g_strdup_printf ("url(#%s)", clipId);
    	 gNode->setAttribute("clip-path", urltext);
         free(urltext);
         gNode->appendChild(image_node);
         Inkscape::GC::release(image_node);
         _container->appendChild(gNode);
         Inkscape::GC::release(gNode);
         double fillColor = fetchAverageColor(_container, image_node);
     } else {*/
		 if (image_node) {
			 _container->appendChild(image_node);
			 if (sp_creator_sh && strstr(sp_creator_sh, "Adobe Photoshop"))
			 {
				 double tspanCount = fetchAverageColor(_container, image_node);
				 if (tspanCount > 0)
				 {
					const char* tmpName = image_node->attribute("xlink:href");
					removeImg(tmpName);
					_container->removeChild(image_node);
					 //image_node->setAttribute("visibility", "hidden");
				 }
			 }
			Inkscape::GC::release(image_node);
		 }
     //}
}

void SvgBuilder::addImageMask(GfxState *state, Stream *str, int width, int height,
                              bool invert, bool interpolate) {

    // Create a rectangle
    Inkscape::XML::Node *rect = _xml_doc->createElement("svg:rect");
    sp_repr_set_svg_double(rect, "x", 0.0);
    sp_repr_set_svg_double(rect, "y", 0.0);
    sp_repr_set_svg_double(rect, "width", 1.0);
    sp_repr_set_svg_double(rect, "height", 1.0);
    svgSetTransform(rect, 1.0, 0.0, 0.0, -1.0, 0.0, 1.0);
    // Get current fill style and set it on the rectangle
    SPCSSAttr *css = sp_repr_css_attr_new();
    _setFillStyle(css, state, false);
    sp_repr_css_change(rect, css, "style");
    sp_repr_css_attr_unref(css);

    // Scaling 1x1 surfaces might not work so skip setting a mask with this size
    if ( width > 1 || height > 1 ) {
        Inkscape::XML::Node *mask_image_node =
            _createImage(str, width, height, NULL, interpolate, NULL, true, invert);
        if (mask_image_node) {
            // Create the mask
            Inkscape::XML::Node *mask_node = _createMask(1.0, 1.0);
            // Remove unnecessary transformation from the mask image
            mask_image_node->setAttribute("transform", NULL);
            mask_node->appendChild(mask_image_node);
            Inkscape::GC::release(mask_image_node);
            gchar *mask_url = g_strdup_printf("url(#%s)", mask_node->attribute("id"));
            rect->setAttribute("mask", mask_url);
            g_free(mask_url);
        }
    }

    // Add the rectangle to the container
    _container->appendChild(rect);
    if (sp_rect_how_path_sh)
    	_countOfPath++;
    Inkscape::GC::release(rect);
}

void SvgBuilder::addMaskedImage(GfxState * /*state*/, Stream *str, int width, int height,
                                GfxImageColorMap *color_map, bool interpolate,
                                Stream *mask_str, int mask_width, int mask_height,
                                bool invert_mask, bool mask_interpolate) {

    Inkscape::XML::Node *mask_image_node = _createImage(mask_str, mask_width, mask_height,
                                          NULL, mask_interpolate, NULL, true, invert_mask);
    Inkscape::XML::Node *image_node = _createImage(str, width, height, color_map, interpolate, NULL);
    if ( mask_image_node && image_node ) {
        // Create mask for the image
        Inkscape::XML::Node *mask_node = _createMask(1.0, 1.0);
        // Remove unnecessary transformation from the mask image
        mask_image_node->setAttribute("transform", NULL);
        mask_node->appendChild(mask_image_node);
        // Scale the mask to the size of the image
        Geom::Affine mask_transform((double)width, 0.0, 0.0, (double)height, 0.0, 0.0);
        gchar *transform_text = sp_svg_transform_write(mask_transform);
        mask_node->setAttribute("maskTransform", transform_text);
        g_free(transform_text);
        // Set mask and add image
        gchar *mask_url = g_strdup_printf("url(#%s)", mask_node->attribute("id"));
        image_node->setAttribute("mask", mask_url);
        g_free(mask_url);
        _container->appendChild(image_node);
    }
    if (mask_image_node) {
        Inkscape::GC::release(mask_image_node);
    }
    if (image_node) {
        Inkscape::GC::release(image_node);
    }
}
    
void SvgBuilder::addSoftMaskedImage(GfxState * /*state*/, Stream *str, int width, int height,
                                    GfxImageColorMap *color_map, bool interpolate,
                                    Stream *mask_str, int mask_width, int mask_height,
                                    GfxImageColorMap *mask_color_map, bool mask_interpolate) {
	if (sp_preserve_dpi_sp) {
		unsigned char* alphaChanel = _encodeImageAlphaMask(mask_str, mask_width, mask_height, mask_color_map, mask_interpolate);
	    Inkscape::XML::Node *image_node = _createMaskedImage(str, width, height, color_map, interpolate, alphaChanel, mask_width, mask_height);
	    delete [] alphaChanel;
	    if (image_node) {
	    	_container->appendChild(image_node);
	    	Inkscape::GC::release(image_node);
	    }
	} else {
		Inkscape::XML::Node *mask_image_node = _createImage(mask_str, mask_width, mask_height,
															mask_color_map, mask_interpolate, NULL, true);

		Inkscape::XML::Node *image_node = _createImage(str, width, height, color_map, interpolate, NULL);

		if ( mask_image_node && image_node ) {
			// Create mask for the image
			Inkscape::XML::Node *mask_node = _createMask(1.0, 1.0);
			// Remove unnecessary transformation from the mask image
			mask_image_node->setAttribute("transform", NULL);
			mask_node->appendChild(mask_image_node);
			// Set mask and add image
			gchar *mask_url = g_strdup_printf("url(#%s)", mask_node->attribute("id"));
			image_node->setAttribute("mask", mask_url);
			g_free(mask_url);
			_container->appendChild(image_node);
		}
		if (mask_image_node) {
			Inkscape::GC::release(mask_image_node);
		}
		if (image_node) {
			Inkscape::GC::release(image_node);
		}
	}
}

/**
 * \brief Starts building a new transparency group
 */
void SvgBuilder::pushTransparencyGroup(GfxState * /*state*/, double *bbox,
                                       GfxColorSpace * /*blending_color_space*/,
                                       bool isolated, bool knockout,
                                       bool for_softmask) {

    // Push node stack
    pushNode("svg:g");

    // Setup new transparency group
    SvgTransparencyGroup *transpGroup = new SvgTransparencyGroup;
    for (size_t i = 0; i < 4; i++) {
        transpGroup->bbox[i] = bbox[i];        
    }
    transpGroup->isolated = isolated;
    transpGroup->knockout = knockout;
    transpGroup->for_softmask = for_softmask;
    transpGroup->container = _container;

    // Push onto the stack
    transpGroup->next = _transp_group_stack;
    _transp_group_stack = transpGroup;
}

void SvgBuilder::popTransparencyGroup(GfxState * /*state*/) {
    // Restore node stack
    popNode();
}

/**
 * \brief Places the current transparency group into the current container
 */
void SvgBuilder::paintTransparencyGroup(GfxState * /*state*/, double * /*bbox*/) {
    SvgTransparencyGroup *transpGroup = _transp_group_stack;
    _container->appendChild(transpGroup->container);
    Inkscape::GC::release(transpGroup->container);
    // Pop the stack
    _transp_group_stack = transpGroup->next;
    delete transpGroup;
}

/**
 * \brief Creates a mask using the current transparency group as its content
 */
void SvgBuilder::setSoftMask(GfxState * /*state*/, double * /*bbox*/, bool /*alpha*/,
                             Function * /*transfer_func*/, GfxColor * /*backdrop_color*/) {

    // Create mask
    Inkscape::XML::Node *mask_node = _createMask(1.0, 1.0);
    // Add the softmask content to it
    SvgTransparencyGroup *transpGroup = _transp_group_stack;
    mask_node->appendChild(transpGroup->container);
    Inkscape::GC::release(transpGroup->container);
    // Apply the mask
    _state_stack.back().softmask = mask_node;
    pushGroup();
    gchar *mask_url = g_strdup_printf("url(#%s)", mask_node->attribute("id"));
    _container->setAttribute("mask", mask_url);
    g_free(mask_url);
    // Pop the stack
    _transp_group_stack = transpGroup->next;
    delete transpGroup;
}

void SvgBuilder::clearSoftMask(GfxState * /*state*/) {
    if (_state_stack.back().softmask) {
        _state_stack.back().softmask = NULL;
        popGroup();
    }
}

} } } /* namespace Inkscape, Extension, Internal */

#endif /* HAVE_POPPLER */

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4 :
