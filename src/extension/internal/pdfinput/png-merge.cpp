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
#include "helper/png-write.h"

namespace Inkscape {
namespace Extension {
namespace Internal {

MergeBuilder::MergeBuilder(Inkscape::XML::Node *sourceTree, gchar *rebasePath)
{
	_sizeListMergeTag = 0;
	_sizeListMergeAttr = 0;
	_doc = SPDocument::createNewDoc(NULL, TRUE, TRUE);
	Inkscape::DocumentUndo::setUndoSensitive(_doc, false);
	_root = _doc->rroot;
	_xml_doc = _doc->getReprDoc();
	_sourceRoot = sourceTree;

	// copy all attributes
	Inkscape::Util::List<const Inkscape::XML::AttributeRecord > attrList = sourceTree->attributeList();
	while( attrList ) {
	    _root->setAttribute(g_quark_to_string(attrList->key), attrList->value);
	    attrList++;
	}


	Inkscape::XML::Node *tmpNode = sourceTree->firstChild();
	// Copy all no visual nodes from original doc
	while(tmpNode) {
		if ( strcmp(tmpNode->name(),"svg:g") != 0 ) {
			copyAsChild(_root, tmpNode, rebasePath);
			if ( strcmp(tmpNode->name(),"svg:defs") == 0 ) {
				_defs = tmpNode;
			}
		}
		else {
			break;
		};
		tmpNode = tmpNode->next();
	}
    _sourceVisual = tmpNode;
	// Create main visual node
	_mainVisual = _xml_doc->createElement("svg:g");
	_root->appendChild(_mainVisual);
	if (tmpNode) {
		Inkscape::Util::List<const Inkscape::XML::AttributeRecord > attrList = tmpNode->attributeList();
		while( attrList ) {
		  _mainVisual->setAttribute(g_quark_to_string(attrList->key), attrList->value);
		  attrList++;
		}
	}
}

void MergeBuilder::addTagName(char *tagName) {
	_listMergeTags[_sizeListMergeTag] = (char*) malloc(strlen(tagName) + 1);
	strcpy(_listMergeTags[_sizeListMergeTag], tagName);
	_sizeListMergeTag++;
}

void MergeBuilder::addAttrName(char *attrName) {
	_listMergeAttr[_sizeListMergeAttr] = (char*) malloc(strlen(attrName) + 1);
	strcpy(_listMergeAttr[_sizeListMergeAttr], attrName);
	_sizeListMergeAttr++;
}

Inkscape::XML::Node *MergeBuilder::findFirstNode(void) {
	return findNode(_sourceVisual->firstChild(), 2);
}

Inkscape::XML::Node *MergeBuilder::findFirstAttrNode(void) {
	return findAttrNode(_sourceVisual->firstChild());
}

Inkscape::XML::Node *MergeBuilder::findNextNode(Inkscape::XML::Node *node, int level) {
	return findNode(node, level);
}

Inkscape::XML::Node *MergeBuilder::findNextAttrNode(Inkscape::XML::Node *node) {
	return findAttrNode(node);
}

void MergeBuilder::clearMerge(void) {
    while(_mainVisual->firstChild()){
    	_mainVisual->removeChild(_mainVisual->firstChild());
    }
}

Inkscape::XML::Node *MergeBuilder::findNode(Inkscape::XML::Node *node, int level) {
	Inkscape::XML::Node *tmpNode;
	Inkscape::XML::Node *resNode;
	if (level > 2) return (Inkscape::XML::Node *) NULL;
	if (level < 2) {
		if (node->childCount()){
		  tmpNode = node->firstChild();
		  while( tmpNode ) {
			  resNode = findNode(tmpNode, level+1);
			  if (resNode) return resNode;
			  tmpNode = tmpNode->next();
		  }
		}
	}
	if (level == 2) {
		tmpNode = node;
		while(tmpNode) {
			if (haveTagFormList(tmpNode)) {
				return tmpNode;
			}
			tmpNode = tmpNode->next();
		}
	}

	return resNode;
}

Inkscape::XML::Node *MergeBuilder::findAttrNode(Inkscape::XML::Node *node) {
	Inkscape::XML::Node *tmpNode;
	Inkscape::XML::Node *resNode = NULL;
	tmpNode = node;
	while(tmpNode) {
		if (haveTagAttrFormList(tmpNode)) {
			return tmpNode;
		}
		tmpNode = tmpNode->next();
	}
	return resNode;
}


bool MergeBuilder::haveTagFormList(Inkscape::XML::Node *node) {
	  Inkscape::XML::Node *tmpNode = node;
	  if (node == 0) return false;
	  bool res = FALSE;
	  uint coun = 0;
	  // print_node(node, 3);
	  // Calculate count of right svg:g before image
	  while(  (coun < 5) &&
			  (tmpNode != NULL) &&
			  //(tmpNode->childCount() == 1) &&
			  (strcmp(tmpNode->name(), "svg:g") == 0)) {
		  coun++;
		  tmpNode = tmpNode->firstChild();
		  if (! tmpNode ) break;
	  }

	  if (tmpNode == 0) return false;

	  for(int i = 0; i < _sizeListMergeTag; i++) {
		  if ((coun >= 0) && (strcmp(tmpNode->name(), _listMergeTags[i]) == 0) && (tmpNode->childCount() == 0)) {
			  res = TRUE;
		  }
	  }

	  return res;
}

bool MergeBuilder::haveTagAttrFormList(Inkscape::XML::Node *node) {
	Inkscape::XML::Node *tmpNode = node;
	  if (tmpNode == 0) return false;
	  bool tag = FALSE;
	  bool attr = FALSE;
	  uint coun = 0;
	  // print_node(node, 3);
	  // Calculate count of right svg:g before other tag
	  while(  (coun < 15) &&
			  (tmpNode != NULL) &&
			  //(tmpNode->childCount() == 1) &&
			  (strcmp(tmpNode->name(), "svg:g") == 0)) {
		  coun++;
		  // Check in attribs list
		  Inkscape::Util::List<const Inkscape::XML::AttributeRecord > attrList = tmpNode->attributeList();
		  while( attrList ) {
			  const char *attrName = g_quark_to_string(attrList->key);
			  for(int i = 0; i < _sizeListMergeAttr; i++) {
				  if (strcmp(attrName, _listMergeAttr[i]) == 0) {
					  attr = TRUE;
				  }
			  }
			  attrList++;
		  }

		  if ((tmpNode->childCount() == 0) && tmpNode->next()) {
			  tmpNode = tmpNode->next();
			  coun--;
		  } else {
		      tmpNode = tmpNode->firstChild();
		  }
		  // if empty <g> tag - exit
		  if (! tmpNode ) break;
	  }

	  if (tmpNode == 0) return false;

	  // if in list of tag
	  for(int i = 0; i < _sizeListMergeTag; i++) {
		  if ((coun >= 0) && (strcmp(tmpNode->name(), _listMergeTags[i]) == 0) && (tmpNode->childCount() == 0)) {
			  tag = TRUE;
		  }
	  }

	  return tag && attr;

}

void MergeBuilder::addImageNode(Inkscape::XML::Node *imageNode, char* rebasePath) {
	copyAsChild(_mainVisual, imageNode, rebasePath);
}

void MergeBuilder::mergeAll(char* rebasePath) {
	Inkscape::XML::Node *node;
	node = _sourceVisual->firstChild();
	while(node) {
		copyAsChild(_mainVisual, node, rebasePath);
		node = node->next();
	}
}

Inkscape::XML::Node *MergeBuilder::copyAsChild(Inkscape::XML::Node *destNode, Inkscape::XML::Node *childNode, char *rebasePath) {
	Inkscape::XML::Node *tempNode = _xml_doc->createElement(childNode->name());
    //print_node(childNode, 1);
	// copy all attributes
	Inkscape::Util::List<const Inkscape::XML::AttributeRecord > attrList = childNode->attributeList();
	while( attrList ) {
		// High quality of images
		if ((strcmp(tempNode->name(), "svg:image") == 0) &&
			(strcmp(g_quark_to_string(attrList->key), "xlink:href") == 0 ) &&
			rebasePath) {
			tempNode->setAttribute(g_quark_to_string(attrList->key),
					        g_strdup_printf("%s%s", rebasePath, attrList->value));
		}
		else {
	      tempNode->setAttribute(g_quark_to_string(attrList->key), attrList->value);
		}
	    attrList++;
	}

	tempNode->setContent(childNode->content());

	destNode->appendChild(tempNode);
	// Add tree of appended node
	Inkscape::XML::Node *ch = childNode->firstChild();
	while(ch) {
		copyAsChild(tempNode, ch, rebasePath);
		ch = ch->next();
	}
	return tempNode;
}

void MergeBuilder::save(gchar const *filename) {
	std::vector<SPItem*> x;
	char *c;
	float width = strtof(_root->attribute("width"), &c);
	float height = strtof(_root->attribute("height"), &c);
	sp_export_png_file(_doc, filename,
					0, 0, width, height, // crop of document x,y,W,H
					width * 3, height * 3, // size of png
					150, 150, // dpi x & y
					0xFFFFFF00,
					NULL, // callback for progress bar
					NULL, // struct SPEBP
					true, // override file
					x);

}

void MergeBuilder::saveThumbW(int w, gchar const *filename){
	std::vector<SPItem*> x;
	char *c;
	float width = strtof(_root->attribute("width"), &c);
	float height = strtof(_root->attribute("height"), &c);
	float zoom = width/w;
	sp_export_png_file(_doc, filename,
					0, 0, width, height, // crop of document x,y,W,H
					width/zoom, height/zoom, // size of png
					150, 150, // dpi x & y
					0xFFFFFF00,
					NULL, // callback for progress bar
					NULL, // struct SPEBP
					true, // override file
					x);
}

char *prepareStringForFloat(char *str) {
	char *c2 = str;
	while(*c2 != 0) {
		if (*c2 == ',') *c2='|';
		if (*c2 == '.') *c2=localeconv()->decimal_point[0];
		c2++;
	}
	return str;
}

void MergeBuilder::getMainSize(float *w, float *h) {
	char *cm;
	char *ct;
	char *strMatrix = g_strdup_printf("%s",_sourceVisual->attribute("transform"));
	prepareStringForFloat(strMatrix);
	cm = &strMatrix[7];
	for(int i = 0; i < 6; i++) {
		mainMatrix[i] = strtof(cm, &ct);
		cm = ct + 1;
	}
	free(strMatrix);

	strMatrix = g_strdup_printf("%s", _sourceRoot->attribute("height"));
	prepareStringForFloat(strMatrix);
	*h = abs(strtof(strMatrix, &ct)/mainMatrix[0]);
	free(strMatrix);

	strMatrix = g_strdup_printf("%s", _sourceRoot->attribute("width"));
	prepareStringForFloat(strMatrix);
	*w = abs(strtof(strMatrix, &ct)/mainMatrix[3]);
	free(strMatrix);
}

void MergeBuilder::getMainClipSize(float *w, float *h) {
	Inkscape::XML::Node *mainPath = NULL;
	mainPath = _defs->firstChild();
	float startx, starty;
	float tmpf = 0;
	char *sizeStr;
	char *c1, *c2, *c3;
	*w = *h = 0;

	while ((mainPath != NULL) && (strcmp(mainPath->name(), "svg:clipPath") != 0 )) {
		mainPath = mainPath->next();
	}
	if (mainPath) { // we have clipPatch in defs
		mainPath = mainPath->firstChild();
		sizeStr = (char *) mainPath->attribute("d");
		c1 = (char *)alloca(strlen(sizeStr));
		strcpy(c1, sizeStr);
		c2 = c1;
		while(*c2 != 0) {
			if (*c2 == ',') *c2='|';
			if (*c2 == '.') *c2=localeconv()->decimal_point[0];
			c2++;
		}

		c2 = c1 + 1;
		while(c2 != 0 && c2[0] != 'Z') {
			tmpf = strtof(c2, &c3);
			if (c2 == c3) {
				c2 = c3 + 1;
				continue;
			}
			*w = *w > tmpf ? *w : tmpf;
			c2 = c3 + 1;
			tmpf = strtof(c2, &c3);
			*h = *h > tmpf ? *h : tmpf;
			c2 = c3 + 1;
		}
	}
	else  // we do not have clipPatch in defs
	{
		// TODO: if we do not have main path we must return width and height attribute from svg tag

	}

}

void MergeBuilder::removeOldImages(void) {
	Inkscape::XML::Node *tmpNode = _mainVisual->firstChild();
	Inkscape::XML::Node *toImageNode;
	const char *tmpName;
	while(tmpNode) {
		toImageNode = tmpNode;
		while(toImageNode && (strcmp(toImageNode->name(), "svg:image") != 0)) {
			toImageNode = toImageNode->firstChild();
		}
		if (toImageNode) { // if it is image node
			tmpName = toImageNode->attribute("xlink:href");
			remove(tmpName);
		}
	    tmpNode = tmpNode->next();
	}
}

MergeBuilder::~MergeBuilder(void){
  delete _doc;
  for(int i = 0; i < _sizeListMergeTag; i++) {
	  free(_listMergeTags[i]);
  }
  for(int i = 0; i < _sizeListMergeAttr; i++) {
  	  free(_listMergeAttr[i]);
  }
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
  while(  (coun < 5) &&
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
		tmpNode = node;
		while(tmpNode) {
			if (isImage_node(tmpNode)) {
				return tmpNode;
			}
			tmpNode = tmpNode->next();
		}
	}

	return resNode;
}


