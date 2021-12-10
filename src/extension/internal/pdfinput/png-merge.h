/*
 * png-merge.h
 *
 *  Created on: 20 июл. 2017 г.
 *      Author: common
 */

#ifndef SRC_EXTENSION_INTERNAL_PDFINPUT_PNG_MERGE_H_
#define SRC_EXTENSION_INTERNAL_PDFINPUT_PNG_MERGE_H_
#include "xml/node.h"
#include "xml/element-node.h"
#include "xml/attribute-record.h"
#include "util/list.h"
#include "svg-builder.h"
#include "sp-item.h"
#include "sp-path.h"
#include "display/curve.h"
#include "Object.h"

#define PROFILER_ENABLE 0

bool isNotTable(Inkscape::XML::Node *node);

void print_node(Inkscape::XML::Node *node, uint level);

bool objStreamToFile(Object* obj, const char* fileName);
void print_node_attribs(Inkscape::XML::Node *node, uint level);

bool isImage_node(Inkscape::XML::Node *node);

Inkscape::XML::Node *find_image_node(Inkscape::XML::Node *node, uint level);

Inkscape::XML::Node *merge_images(Inkscape::XML::Node *node1, Inkscape::XML::Node *node2);
char *readLineFromFile(FILE *fl);
double rectIntersect(const Geom::Rect& main, const Geom::Rect& kind);
bool rectHasCommonEdgePoint(Geom::Rect rect1, Geom::Rect rect2);
inline bool approxEqual(const float x, const float y, const float epsilon = 0.05f);
inline bool definitelyBigger(const float a, const float b, const float epsilon = 0.05f);

namespace Inkscape {
namespace Extension {
namespace Internal {

#define CROP_LINE_SIZE 20
enum mark_line_style {
	CROP_MARK_STYLE,
	BLEED_LINE_STYLE
};

void createPrintingMarks(SvgBuilder *builder);
void mergeImagePathToOneLayer(SvgBuilder *builder, ApproveNode* approve = nullptr);
void mergeMaskGradientToLayer(SvgBuilder *builder);
void mergeMaskToImage(SvgBuilder *builder);
void enumerationTagsStart(SvgBuilder *builder);
void enumerationTags(Inkscape::XML::Node *inNode);
uint mergeImagePathToLayerSave(SvgBuilder *builder, bool splitRegions = true, bool simulate=false, uint* regionsCount = nullptr);
void mergeTspan (SvgBuilder *builder);
void mergeNearestTextToOnetag(SvgBuilder *builder);
void compressGtag(SvgBuilder *builder);
void moveTextNode(SvgBuilder *builder, Inkscape::XML::Node *mainNode, Inkscape::XML::Node *currNode, Geom::Affine aff, ApproveNode* approve);
void moveTextNode(SvgBuilder *builder, Inkscape::XML::Node *mainNode, Inkscape::XML::Node *currNode=0, ApproveNode* approve = nullptr);
int64_t svg_get_number_of_objects(Inkscape::XML::Node *node, ApproveNode* approve);

#define timPDF_PARSER 0
#define timCALCULATE_OBJECTS 1
#define timTEXT_FLUSH 2
#define timCREATE_IMAGE 3

#if PROFILER_ENABLE == 1
#define TIMER_NUMBER 50

extern double profiler_timer[];
extern double profiler_timer_up[];

// print current tickcount value with message
#define logTime(message)  printf("%.9f : %s\n", Inkscape::Extension::Internal::GetTickCount(), message);
// save value of current tickcount
#define upTimer(num) Inkscape::Extension::Internal::profiler_timer_up[num] = Inkscape::Extension::Internal::GetTickCount()
// add different for saved value and carrent tickcount to timer counter
#define incTimer(num) Inkscape::Extension::Internal::profiler_timer[num] += Inkscape::Extension::Internal::GetTickCount() - \
		Inkscape::Extension::Internal::profiler_timer_up[num]; \
		Inkscape::Extension::Internal::profiler_timer_up[num] = Inkscape::Extension::Internal::GetTickCount()
// erase timercounter for this timer
#define initTimer(num) Inkscape::Extension::Internal::profiler_timer[num] = 0; \
		Inkscape::Extension::Internal::profiler_timer_up[num] = Inkscape::Extension::Internal::GetTickCount()
// print timer counter with message
#define prnTimer(num, message) printf("%.9f : %s\n", Inkscape::Extension::Internal::profiler_timer[num], message);
#else
#define logTime
#define upTimer
#define incTimer
#define initTimer
#define prnTimer
#endif



double GetTickCount(void);

class TabRect {
public:
	Inkscape::XML::Node* node;
	double x1, x2, y1, y2;
	TabRect(double x1, double y1, double x2, double y2, Inkscape::XML::Node* node);
	TabRect(Geom::Point point1, Geom::Point point2, Inkscape::XML::Node* _node);
};

class TabLine {
	private:


		bool isVert;
		bool lookLikeTab;
		SPCurve* spCurve;

	public:
		Inkscape::XML::Node* node;
		Geom::Affine affineToMainNode;
		double x1, x2, y1, y2;
		TabLine(Inkscape::XML::Node* node, const Geom::Curve& curve, SPDocument* spDoc);
		bool intersectRect(Geom::Rect rect);
		Inkscape::XML::Node* searchByPoint(double xMediane, double yMediane, bool isVerticale);
		size_t curveSegmentsCount() { return spCurve->get_segment_count(); };


		bool isTableLine() { return  lookLikeTab; }
		bool isVertical() { return x1 == x2; }
};

struct TableCell {
	double x, y, width, height;
	 TabLine *topLine;
	 TabLine *bottomLine;
	 TabLine *leftLine;
	 TabLine *rightLine;
	 TabRect *rect;
	 int mergeIdx;
	 bool isMax;
	 int maxCol;
	 int maxRow;
	 TableCell* cellMax;
};

class TableDefenition
{
private:
	TableCell* _cells;


	int countCol, countRow;

	bool isHidCell(int c, int r);
	Inkscape::XML::Node* cellRender(SvgBuilder *builder, int c, int r, Geom::Affine aff);
	Inkscape::XML::Node* getLeftBorder(SvgBuilder *builder, int c, int r, Geom::Affine aff);
	Inkscape::XML::Node* getTopBorder(SvgBuilder *builder, int c, int r, Geom::Affine aff);
	Inkscape::XML::Node* getBottomBorder(SvgBuilder *builder, int c, int r, Geom::Affine aff);
	Inkscape::XML::Node* getRightBorder(SvgBuilder *builder, int c, int r, Geom::Affine aff);


public:
	double x, y, width, height;
	TableDefenition(unsigned int width, unsigned int height) :
		countCol(width),
		countRow(height),
		x(0),
		y(0),
		width(0),
		height(0)
	{
		_cells = new TableCell[countCol*countRow];

	}
	void setStroke(int xIdx, int yIdx, TabLine *topLine, TabLine *bottomLine, TabLine *leftLine, TabLine *rightLine);
	void setVertex(int xIdx, int yIdx, double xStart, double yStart, double xEnd, double yEnd);
	TableCell* getCell(int xIdx, int yIdx);
	void setMergeIdx(int col1, int row1, int mergeIdx);
	void setMergeIdx(int col1, int row1, int col2, int row2, int mergeIdx);
	void setRect(int col, int row, TabRect* rect);

	Inkscape::XML::Node* render(SvgBuilder *builder, Geom::Affine aff);
};

class TableRegion
{
private:

	double x1, x2, y1, y2;
	SvgBuilder *_builder;
	SPDocument* spDoc;
	bool _isTable;
	TableDefenition* tableDef;
public:
	std::vector<TabLine*> lines;
	std::vector<TabRect*> rects;
	TableRegion(SvgBuilder *builder) :
		_builder(builder),
		_isTable(true),
		tableDef(nullptr),
		x1(1e5),
		x2(0),
		y1(1e5),
		y2(0)
	{
		spDoc = _builder->getSpDocument();
	}
	~TableRegion();

	TabLine* searchByPoint(double xMediane, double yMediane, bool isVerticale);
	TabRect* matchRect(double xStart, double yStart, double xEnd, double yEnd);
	bool recIntersectLine(Geom::Rect rect);
	Geom::Rect getBBox();

	bool addLine(Inkscape::XML::Node* node);

	bool isTable(){	return _isTable;	}
	bool buildKnote(SvgBuilder *builder);
	Inkscape::XML::Node* render(SvgBuilder *builder, Geom::Affine aff);
};
typedef std::vector<TableRegion*> TableList;

TableList* detectTables(SvgBuilder *builder, TableList* tables);



class MergeBuilder {
public:
	MergeBuilder(Inkscape::XML::Node *sourceTree, gchar *rebasePath);
	Inkscape::XML::Node *addImageNode(Inkscape::XML::Node *imageNode, char* rebasePath);
	Inkscape::XML::Node *fillTreeOfParents(Inkscape::XML::Node *fromNode);
	Inkscape::XML::Node *findNodeById(Inkscape::XML::Node *fromNode, const char* id);
	void mergeAll(char* rebasePath);
	Inkscape::XML::Node *copyAsChild(Inkscape::XML::Node *destNode, Inkscape::XML::Node *childNode, char *rebasePath);
	Inkscape::XML::Node *saveImage(gchar *name, SvgBuilder *builder, bool visualBound, double &resultDpi, Geom::Rect* rect = nullptr);
	void getMinMaxDpi(SPItem* node, double &min, double &max, Geom::Affine &innerAffine);
	Geom::Rect save(gchar const *filename, bool visualBound, double &resultDpi, Geom::Rect* rect = nullptr);
	void saveThumbW(int w, gchar const *filename);
	void getMainClipSize(float *w, float *h);
	void removeOldImagesEx(Inkscape::XML::Node *startNode);
	void removeOldImages(void);
	void removeRelateDefNodes(Inkscape::XML::Node *node);
	void removeGFromNode(Inkscape::XML::Node *node); // remove graph objects from node
	void addTagName(char *tagName);
	Inkscape::XML::Node *findNode(Inkscape::XML::Node *node, int level, int *count=0);
	Inkscape::XML::Node *findAttrNode(Inkscape::XML::Node *node);
	bool haveTagFormList(Inkscape::XML::Node *node, int *count=0, int level = 0, bool excludeTable=true);
	bool haveTagAttrFormList(Inkscape::XML::Node *node);
	void clearMerge(void);
	Inkscape::XML::Node *findFirstNode(int *count=0);
	Inkscape::XML::Node *findFirstAttrNode(void);
	void findImageMaskNode(Inkscape::XML::Node *node, std::vector<Inkscape::XML::Node *> *listNodes);
	Geom::Affine getAffine(Inkscape::XML::Node *node);
	Inkscape::XML::Node *findNextNode(Inkscape::XML::Node *node, int level);
	Inkscape::XML::Node *findNextAttrNode(Inkscape::XML::Node *node);
	static Inkscape::XML::Node *generateNode(const char* imgPath, SvgBuilder *builder, Geom::Rect *rt, Geom::Affine affine);
	void addAttrName(char *attrName); // attr name for sersch
	bool haveContent(Inkscape::XML::Node *node);
	Inkscape::XML::Node *getDefNodeById(char *nodeId, Inkscape::XML::Node *mydef = 0);
	Inkscape::XML::Node *getSourceSubVisual() { return _sourceSubVisual; };
	Inkscape::XML::Node *getSourceVisual() { return _sourceVisual; };
	const char *findAttribute(Inkscape::XML::Node *node, char *attribName);
	Inkscape::XML::Node *mergeTspan(Inkscape::XML::Node *textNode);
	Inkscape::XML::Node *compressGNode(Inkscape::XML::Node *gNode);
	char linkedID[100]; // last value of attribute of node from haveTagAttrFormList()
	float mainMatrix[6];
	~MergeBuilder(void);
	Inkscape::XML::Document* getXmlDoc() { return _xml_doc; };
	SPDocument *spDocument() { return _doc; };
	std::vector<Geom::Rect> getRegions();
private:
    SPDocument *_doc;
    Inkscape::XML::Node *_root;
    Inkscape::XML::Node *_defs;
    Inkscape::XML::Node *_myDefs;
    Inkscape::XML::Node *_mainVisual;
    Inkscape::XML::Node *_mainSubVisual;
    Inkscape::XML::Node *_sourceRoot;
    Inkscape::XML::Node *_sourceSubVisual;
    int subVisualDeep = 2;
    Inkscape::XML::Node *_sourceVisual;
    Inkscape::XML::Document *_xml_doc;
    char *_listMergeTags[10];
    int _sizeListMergeTag;
    char *_listMergeAttr[10];
    int _sizeListMergeAttr;
    Geom::Affine affine;
};

} // namespace Internal
} // namespace Extension
} // namespace Inkscape

#endif /* SRC_EXTENSION_INTERNAL_PDFINPUT_PNG_MERGE_H_ */
