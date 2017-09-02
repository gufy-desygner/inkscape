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

void print_node(Inkscape::XML::Node *node, uint level);

void print_node_attribs(Inkscape::XML::Node *node, uint level);

bool isImage_node(Inkscape::XML::Node *node);

Inkscape::XML::Node *find_image_node(Inkscape::XML::Node *node, uint level);

Inkscape::XML::Node *merge_images(Inkscape::XML::Node *node1, Inkscape::XML::Node *node2);



namespace Inkscape {
namespace Extension {
namespace Internal {

void mergeImagePathToOneLayer(SvgBuilder *builder);
void mergeMaskGradientToLayer(SvgBuilder *builder);
uint mergePredictionCountImages(SvgBuilder *builder);
void mergeImagePathToLayerSave(SvgBuilder *builder);
void mergeNearestTextToOnetag(SvgBuilder *builder);

class MergeBuilder {
public:
	MergeBuilder(Inkscape::XML::Node *sourceTree, gchar *rebasePath);
	void addImageNode(Inkscape::XML::Node *imageNode, char* rebasePath);
	void mergeAll(char* rebasePath);
	Inkscape::XML::Node *copyAsChild(Inkscape::XML::Node *destNode, Inkscape::XML::Node *childNode, char *rebasePath);
	void save(gchar const *filename);
	void saveThumbW(int w, gchar const *filename);
	void getMainClipSize(float *w, float *h);
	void getMainSize(float *w, float *h);
	void removeOldImages(void);
	void removeRelateDefNodes(Inkscape::XML::Node *node);
	void addTagName(char *tagName);
	Inkscape::XML::Node *findNode(Inkscape::XML::Node *node, int level);
	Inkscape::XML::Node *findAttrNode(Inkscape::XML::Node *node);
	bool haveTagFormList(Inkscape::XML::Node *node);
	bool haveTagAttrFormList(Inkscape::XML::Node *node);
	void clearMerge(void);
	Inkscape::XML::Node *findFirstNode(void);
	Inkscape::XML::Node *findFirstAttrNode(void);
	Inkscape::XML::Node *findNextNode(Inkscape::XML::Node *node, int level);
	Inkscape::XML::Node *findNextAttrNode(Inkscape::XML::Node *node);
	Inkscape::XML::Node *generateNode(char* imgPath, SvgBuilder *builder);
	void addAttrName(char *attrName); // attr name for sersch
	bool haveContent(Inkscape::XML::Node *node);
	Inkscape::XML::Node *getDefNodeById(char *nodeId);
	const char *findAttribute(Inkscape::XML::Node *node, char *attribName);
	char linkedID[100]; // last value of attribute of node from haveTagAttrFormList()
	float mainMatrix[6];
	~MergeBuilder(void);
private:
    SPDocument *_doc;
    Inkscape::XML::Node *_root;
    Inkscape::XML::Node *_defs;
    Inkscape::XML::Node *_mainVisual;
    Inkscape::XML::Node *_sourceRoot;
    Inkscape::XML::Node *_sourceVisual;
    Inkscape::XML::Document *_xml_doc;
    char *_listMergeTags[10];
    int _sizeListMergeTag;
    char *_listMergeAttr[10];
    int _sizeListMergeAttr;
};

} // namespace Internal
} // namespace Extension
} // namespace Inkscape

#endif /* SRC_EXTENSION_INTERNAL_PDFINPUT_PNG_MERGE_H_ */
