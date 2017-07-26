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

class MergeBuilder {
public:
	MergeBuilder(Inkscape::XML::Node *sourceTree);
	void addImageNode(Inkscape::XML::Node *imageNode, char* rebasePath);
	Inkscape::XML::Node *copyAsChild(Inkscape::XML::Node *destNode, Inkscape::XML::Node *childNode, char *rebasePath);
	void save(gchar const *filename);
	void getMainClipSize(float *w, float *h);
	void removeOldImages(void);
	~MergeBuilder(void);
private:
    SPDocument *_doc;
    Inkscape::XML::Node *_root;
    Inkscape::XML::Node *_defs;
    Inkscape::XML::Node *_mainVisual;
    Inkscape::XML::Node *_sourceRoot;
    Inkscape::XML::Document *_xml_doc;
};

} // namespace Internal
} // namespace Extension
} // namespace Inkscape

#endif /* SRC_EXTENSION_INTERNAL_PDFINPUT_PNG_MERGE_H_ */
