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

#define PROFILER_ENABLE 0

void print_node(Inkscape::XML::Node *node, uint level);


void print_node_attribs(Inkscape::XML::Node *node, uint level);

bool isImage_node(Inkscape::XML::Node *node);

Inkscape::XML::Node *find_image_node(Inkscape::XML::Node *node, uint level);

Inkscape::XML::Node *merge_images(Inkscape::XML::Node *node1, Inkscape::XML::Node *node2);
char *readLineFromFile(FILE *fl);

namespace Inkscape {
namespace Extension {
namespace Internal {

#define CROP_LINE_SIZE 20
enum mark_line_style {
	CROP_MARK_STYLE,
	BLEED_LINE_STYLE
};

void createPrintingMarks(SvgBuilder *builder);
void mergeImagePathToOneLayer(SvgBuilder *builder);
void mergeMaskGradientToLayer(SvgBuilder *builder);
void mergeMaskToImage(SvgBuilder *builder);
void enumerationTagsStart(SvgBuilder *builder);
void enumerationTags(Inkscape::XML::Node *inNode);
uint mergeImagePathToLayerSave(SvgBuilder *builder, bool simulate=false);
void mergeTspan (SvgBuilder *builder);
void mergeNearestTextToOnetag(SvgBuilder *builder);
void compressGtag(SvgBuilder *builder);
void moveTextNode(SvgBuilder *builder, Inkscape::XML::Node *mainNode, Inkscape::XML::Node *currNode, Geom::Affine aff);
void moveTextNode(SvgBuilder *builder, Inkscape::XML::Node *mainNode, Inkscape::XML::Node *currNode=0);
int64_t svg_get_number_of_objects(Inkscape::XML::Node *node);

#if PROFILER_ENABLE == 1
#define TIMER_NUMBER 50

extern double profiler_timer[];
extern double profiler_timer_up[];

// print current tickcount value with message
#define logTime(message)  printf("%.9f : %s\n", Inkscape::Extension::Internal::GetTickCount(), message);
// save value of current tickcount
#define upTimer(num) Inkscape::Extension::Internal::profiler_timer_up[num] = Inkscape::Extension::Internal::GetTickCount()
// add different for saved value and carrent tickcount to timer counter
#define incTimer(num) profiler_timer[num] += Inkscape::Extension::Internal::GetTickCount() - \
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
	Inkscape::XML::Node *copyAsChild(Inkscape::XML::Node *destNode, Inkscape::XML::Node *childNode, char *rebasePath);
	Inkscape::XML::Node *saveImage(gchar *name, SvgBuilder *builder, bool visualBound = true);
	Geom::Rect save(gchar const *filename, bool visualBound = true);
	void saveThumbW(int w, gchar const *filename);
	void getMainClipSize(float *w, float *h);
	void getMainSize(float *w, float *h);
	void removeOldImagesEx(Inkscape::XML::Node *startNode);
	void removeOldImages(void);
	void removeRelateDefNodes(Inkscape::XML::Node *node);
	void removeGFromNode(Inkscape::XML::Node *node); // remove graph objects from node
	void addTagName(char *tagName);
	Inkscape::XML::Node *findNode(Inkscape::XML::Node *node, int level, int *count=0);
	Inkscape::XML::Node *findAttrNode(Inkscape::XML::Node *node);
	bool haveTagFormList(Inkscape::XML::Node *node, int *count=0, int level = 0);
	bool haveTagAttrFormList(Inkscape::XML::Node *node);
	void clearMerge(void);
	Inkscape::XML::Node *findFirstNode(int *count=0);
	Inkscape::XML::Node *findFirstAttrNode(void);
	void findImageMaskNode(Inkscape::XML::Node *node, std::vector<Inkscape::XML::Node *> *listNodes);
	Geom::Affine getAffine(Inkscape::XML::Node *node);
	Inkscape::XML::Node *findNextNode(Inkscape::XML::Node *node, int level);
	Inkscape::XML::Node *findNextAttrNode(Inkscape::XML::Node *node);
	Inkscape::XML::Node *generateNode(char* imgPath, SvgBuilder *builder, Geom::Rect *rt);
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
