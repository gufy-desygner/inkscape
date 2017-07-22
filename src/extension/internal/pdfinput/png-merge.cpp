/*
 * png-merge.cpp
 *
 *  Created on: 20 июл. 2017 г.
 *      Author: common
 */

#include "png-merge.h"
#include "xml/document.h"
#include "document.h"
#include "document-undo.h"
#include "../cairo-png-out.h"
#include "util/units.h"

namespace Inkscape {
namespace Extension {
namespace Internal {

MergeBuilder::MergeBuilder(Inkscape::XML::Node *sourceTree) {
	_doc = SPDocument::createNewDoc(NULL, TRUE, TRUE);
	//bool saved = DocumentUndo::getUndoSensitive(doc);
	Inkscape::DocumentUndo::setUndoSensitive(_doc, false);
	_root = _doc->rroot;
	_root->setAttribute("xml:space", "preserve");
	_xml_doc = _doc->getReprDoc();
	_sourceRoot = sourceTree;

	Inkscape::XML::Node *tmpNode = sourceTree->firstChild();
	// Copy all no visual nodes from original doc
	while(tmpNode) {
		//if ( strcmp(tmpNode->name(),"svg:g") != 0 ) {
			copyAsChild(_root, tmpNode);
		//}
		//else {
		//	break;
		//};
		tmpNode = tmpNode->next();
	}

	// Create main visual node
	_mainVisual = _xml_doc->createElement("svg:g");
/*	_root->appendChild(_mainVisual);
	if (tmpNode) {
		Inkscape::Util::List<const Inkscape::XML::AttributeRecord > attrList = tmpNode->attributeList();
		while( attrList ) {
		  _mainVisual->setAttribute(g_quark_to_string(attrList->key), attrList->value);
		  attrList++;
		}
	}*/
}

void MergeBuilder::addImageNode(Inkscape::XML::Node *imageNode) {

}

void MergeBuilder::copyAsChild(Inkscape::XML::Node *destNode, Inkscape::XML::Node *childNode) {
	Inkscape::XML::Node *tempNode = _xml_doc->createElement(childNode->name());

	// copy all attributes
	Inkscape::Util::List<const Inkscape::XML::AttributeRecord > attrList = childNode->attributeList();
	while( attrList ) {
	  tempNode->setAttribute(g_quark_to_string(attrList->key), attrList->value);
	  attrList++;
	}

	tempNode->setContent(childNode->content());

	destNode->appendChild(tempNode);
	// Add tree of appended node
	Inkscape::XML::Node *ch = childNode->firstChild();
	while(ch) {
		copyAsChild(tempNode, ch);
		ch = ch->next();
	}
}

void MergeBuilder::addText(char* str) {
	Inkscape::XML::Node *ch = _xml_doc->createElement("svg:text");
	ch->setContent(str);
	_mainVisual->appendChild(ch);
	Inkscape::Extension::Internal::CairoRendererOutput *png = new CairoRendererOutput();
	const Inkscape::Util::Quantity *quant = new Inkscape::Util::Quantity(2970, "px");
	_doc->setHeight(*quant, TRUE);
	quant = new Inkscape::Util::Quantity(2100, "px");
	_doc->setWidth(*quant, TRUE);
	_doc->setDocumentScale(0.4);
	//_root->

	png->save(NULL, _doc, "test.png");
}

} } } /* namespace Inkscape, Extension, Internal */

void print_prefix(uint level) {
	while((level--) > 0) printf("-");
}

void print_node(Inkscape::XML::Node *node, uint level) {
	print_prefix(level);
	printf("name %s\n", node->name());
	print_node_attribs(node, level);
	int c = node->childCount();
	print_prefix(level);
	printf("childCount %i\n", c);
	Inkscape::XML::Node *ch = node->firstChild();
	while(ch){
	  print_node(ch, level + 1);
	  ch = ch->next();
	}
}

void print_node_attribs(Inkscape::XML::Node *node, uint level) {
	Inkscape::Util::List<const Inkscape::XML::AttributeRecord > attrList = node->attributeList();
	while( attrList ) {
	  print_prefix(level);
	  printf("++attr %s : %s\n", g_quark_to_string(attrList->key), attrList->value);
	  attrList++;
	}
}
/*
 * image field must look how
--name svg:g
--++attr id : g32
--childCount 1
---name svg:g
---++attr id : g34
---++attr clip-path : url(#clipPath38)
---childCount 1
----name svg:g
----++attr id : g40
----++attr transform : matrix(62.950525,0,0,67.729844,264.97803,220.86824)
----childCount 1
-----name svg:image
-----++attr width : 1
-----++attr height : 1
-----++attr style : image-rendering:optimizeSpeed
-----++attr preserveAspectRatio : none
-----++attr transform : matrix(1,0,0,-1,0,1)
-----++attr xlink:href : und_img1.png
-----++attr id : image42
-----childCount 0
 */
bool isImage_node(Inkscape::XML::Node *node) {
  Inkscape::XML::Node *tmpNode = node;
  bool res = TRUE;
  uint coun = 0;

  // Calculate count of right svg:g before image
  while(  (coun < 4) &&
		  (tmpNode != NULL) &&
		  (tmpNode->childCount() == 1) &&
		  (strcmp(tmpNode->name(), "svg:g") == 0)) {
	  coun++;
	  tmpNode = tmpNode->firstChild();
	  if (! tmpNode ) break;
  }

  if ((coun > 0) && (strcmp(tmpNode->name(), "svg:image") == 0) && (tmpNode->childCount() == 0)) {
	  return TRUE;
  }

  return FALSE;
}

Inkscape::XML::Node *find_image_node(Inkscape::XML::Node *node, uint level) {
	Inkscape::XML::Node *tmpNode;
	Inkscape::XML::Node *resNode = NULL;
	if (level > 2) return (Inkscape::XML::Node *) NULL;
	if (level < 2) {
		if (node->childCount()){
		  tmpNode = node->firstChild();
		  while( tmpNode ) {
			  resNode = find_image_node(tmpNode, level+1);
			  if (resNode) return resNode;
			  tmpNode = tmpNode->next();
		  }
		}
	}
	if (level == 2) {
		if (isImage_node(node)) {
			return node;
		}
	}

	return resNode;
}

Inkscape::XML::Node *merge_images(Inkscape::XML::Node *node1, Inkscape::XML::Node *node2) {

	return node1;
}

