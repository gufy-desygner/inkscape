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
#include "TableRegion.h"
#include "TableRecognizeCommon.h"

#define PROFILER_ENABLE 0

#define MAX_ROTATION_ANGLE_SUPPORTED_TEXT_TABLE	2

void print_node(Inkscape::XML::Node *node, uint level);

bool objStreamToFile(Object* obj, const char* fileName);
void print_node_attribs(Inkscape::XML::Node *node, uint level);

bool isImage_node(Inkscape::XML::Node *node);

Inkscape::XML::Node *find_image_node(Inkscape::XML::Node *node, uint level);

Inkscape::XML::Node *merge_images(Inkscape::XML::Node *node1, Inkscape::XML::Node *node2);
char *readLineFromFile(FILE *fl);
bool rectHasCommonEdgePoint(Geom::Rect& rect1, Geom::Rect& rect2);
bool rectHasCommonEdgePoint( const double firstX1, const double firstY1, const double firstX2, const double firstY2,
		const double secondX1, const double secondY1, const double secondX2, const double secondY2 );
bool rectHasCommonEdgePoint( const int firstX1, const int firstY1, const int firstX2, const int firstY2,
		const int secondX1, const int secondY1, const int secondX2, const int secondY2, const int APPROX);
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
void mergePatternToLayer(SvgBuilder *builder);
void mergeMaskToImage(SvgBuilder *builder);
void enumerationTagsStart(SvgBuilder *builder);
void enumerationTags(Inkscape::XML::Node *inNode);
uint mergeImagePathToLayerSave(SvgBuilder *builder, bool splitRegions = true, bool simulate=false, uint* regionsCount = nullptr);
void mergeTspan (SvgBuilder *builder);
void mergeNearestTextToOnetag(SvgBuilder *builder);
void compressGtag(SvgBuilder *builder, int maxdep = 1000);
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

class MergeBuilder {
public:
	MergeBuilder(Inkscape::XML::Node *sourceTree, gchar *rebasePath);
	Inkscape::XML::Node *addImageNode(Inkscape::XML::Node *imageNode, char* rebasePath);
	Inkscape::XML::Node *fillTreeOfParents(Inkscape::XML::Node *fromNode);
	Inkscape::XML::Node *findNodeById(Inkscape::XML::Node *fromNode, const char* id);
	void mergeAll(char* rebasePath);
	Inkscape::XML::Node *copyAsChild(Inkscape::XML::Node *destNode, Inkscape::XML::Node *childNode, char *rebasePath, Inkscape::XML::Document *doc = nullptr);
	Inkscape::XML::Node *saveImage(gchar *name, SvgBuilder *builder, bool visualBound, double &resultDpi, Geom::Rect* rect = nullptr);
	void getMinMaxDpi(SPItem* node, double &min, double &max, Geom::Affine &innerAffine);
	Geom::Rect save(gchar const *filename, bool visualBound, double &resultDpi, Geom::Rect* rect = nullptr);
	void saveThumbW(int w, gchar const *filename);
	void getMainClipSize(float *w, float *h);
	void removeOldImagesEx(Inkscape::XML::Node *startNode);
	void removeOldImages(void);
	void removeRelateDefNodes(Inkscape::XML::Node *node);
	Inkscape::XML::Node *AddClipPathToMyDefs(Inkscape::XML::Node *originalNode, SvgBuilder *builder, char* patternId, gchar *rebasePath);
	void removeGFromNode(Inkscape::XML::Node *node); // remove graph objects from node
	void addTagName(char *tagName);
	Inkscape::XML::Node *findNode(Inkscape::XML::Node *node, int level, int *count=0);
	Inkscape::XML::Node *findAttrNode(Inkscape::XML::Node *node);
	Inkscape::XML::Node *findAttrNodeWithPattern(Inkscape::XML::Node *node);
	bool haveTagFormList(Inkscape::XML::Node *node, int *count=0, int level = 0, bool excludeTable=true);
	bool haveTagAttrFormList(Inkscape::XML::Node *node);
	bool haveTagAttrPattern(Inkscape::XML::Node *node);
	void clearMerge(void);
	Inkscape::XML::Node *findFirstNode(int *count=0);
	Inkscape::XML::Node *findFirstAttrNode(void);
	Inkscape::XML::Node *findFirstAttrNodeWithPattern(void);
	void findImageMaskNode(Inkscape::XML::Node *node, std::vector<Inkscape::XML::Node *> *listNodes);
	Geom::Affine getAffine(Inkscape::XML::Node *node);
	Inkscape::XML::Node *findNextNode(Inkscape::XML::Node *node, int level);
	Inkscape::XML::Node *findNextAttrNode(Inkscape::XML::Node *node);
	Inkscape::XML::Node *findNextAttrNodeWithPattern(Inkscape::XML::Node *node);
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
