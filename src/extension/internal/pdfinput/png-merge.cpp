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
#include <regex.h>
#include "shared_opt.h"
#include "sp-item.h"
#include "sp-root.h"


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
	Inkscape::XML::Node *waitNode;
	while(tmpNode) {
		if ( strcmp(tmpNode->name(),"svg:g") != 0 ) {
			waitNode = copyAsChild(_root, tmpNode, rebasePath);
			if ( strcmp(tmpNode->name(),"svg:defs") == 0 ) {
				_defs = tmpNode;
				_myDefs = waitNode;
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
	Inkscape::XML::Node *resNode = NULL;
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

Inkscape::XML::Node *MergeBuilder::generateNode(char* imgPath, SvgBuilder *builder, Geom::Rect *rt) {
	// Insert node with merged image
		char *buf;
		float clipW , clipH;
		long double tempD;
		float coeffE;
		float coeffF;

		Inkscape::XML::Node *sumNode = builder->createElement("svg:g");
		if (rt) {
			char *c;
			float width = strtof(_root->attribute("width"), &c);
			float height = strtof(_root->attribute("height"), &c);
			clipW = ((*rt)[Geom::X].max() - (*rt)[Geom::X].min())/1.333333;
			clipH = ((*rt)[Geom::Y].max() - (*rt)[Geom::Y].min())/1.333333;
			coeffE = (*rt)[Geom::X].min()/1.333333;
			coeffF = (height - (*rt)[Geom::Y].max())/1.333333;
		} else {
		    getMainSize(&clipW, &clipH);
		    coeffE = mainMatrix[4]/mainMatrix[0] * (-1);
		    coeffF = mainMatrix[5]/mainMatrix[3] * (-1) - clipH;
		}


		buf = g_strdup_printf("matrix(%i.%i,0,0,%i.%i,%i.%i,%i.%i)",
					(int)clipW, (int)abs(modfl(clipW, &tempD)*1000),
					(int)clipH, (int)abs(modfl(clipH, &tempD)*1000),
					(int)coeffE, (int)abs(modfl(coeffE, &tempD)*1000),
					(int)coeffF, (int)abs(modfl(coeffF, &tempD)*1000));
		sumNode->setAttribute("transform", buf);
		Inkscape::XML::Node *tmpNode = builder->createElement("svg:image");
		sumNode->appendChild(tmpNode);
		tmpNode->setAttribute("xlink:href", imgPath);
		tmpNode->setAttribute("transform", "matrix(1,0,0,-1,0,1)");
		tmpNode->setAttribute("width", "1");
		tmpNode->setAttribute("height", "1");
		tmpNode->setAttribute("preserveAspectRatio", "none");
		free(buf);
		return sumNode;
}

bool MergeBuilder::haveContent(Inkscape::XML::Node *node) {
	Inkscape::XML::Node *tmpNode = node;
	if (tmpNode->content()) {
	   return true;
	}
	tmpNode = tmpNode->firstChild();
	while(tmpNode) {
		if (haveContent(tmpNode)) {
			return true;
		}
		tmpNode = tmpNode->next();
	}
	return false;
}

bool MergeBuilder::haveTagFormList(Inkscape::XML::Node *node) {
	  Inkscape::XML::Node *tmpNode = node;
	  if (node == 0) return false;
	  if (haveContent(node)) return false;
	  bool res = FALSE;
	  uint coun = 0;
	  // print_node(node, 3);
	  // Calculate count of right svg:g before image
	  while(  (coun < 10) &&
			  (tmpNode != NULL) &&
			  (tmpNode->childCount() > 0)/* &&
			  (strcmp(tmpNode->name(), "svg:g") == 0)*/) {
		  coun++;
		  if (strcmp(tmpNode->name(), "svg:g") == 0 &&
			  tmpNode->childCount() == 0  ) {
			  tmpNode = tmpNode->next();
		  } else {
		      tmpNode = tmpNode->firstChild();
		  }
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
	  Inkscape::XML::Node *tmpNode2 = NULL;
	  bool tag = FALSE;
	  bool attr = FALSE;
	  uint coun = 0;
	  // print_node(node, 3);
	  // Calculate count of right svg:g before other tag
	  while((coun < 15) &&  (tmpNode != NULL)) {
		  if (strcmp(tmpNode->name(), "svg:g") != 0 && (! tmpNode2)) {
			  tmpNode2 = tmpNode; // node for check tags list
		  }
		  coun++;
		  // Check in attribs list
		  Inkscape::Util::List<const Inkscape::XML::AttributeRecord > attrList = tmpNode->attributeList();
		  while( attrList ) {
			  const char *attrName = g_quark_to_string(attrList->key);
			  for(int i = 0; i < _sizeListMergeAttr; i++) {
				  if (strcmp(attrName, _listMergeAttr[i]) == 0 && (! attr)) {
					  bool additionCond = TRUE;
					  const char *styleValue = tmpNode->attribute(attrName);
					  if (strcmp(attrName, "style") == 0) {
						  additionCond = (strstr(styleValue, "Gradient") > 0) ||
								  (strstr(styleValue, "url(#pattern") > 0);
					  }
					  const char *begin = strstr(styleValue, "url(#");
					  const char *end;
					  if (begin) {
						  begin += 5;
						  end = strstr(begin, ")");
					  }
					  if (begin && end) {
						  memcpy(linkedID, begin, end - begin);
						  linkedID[end-begin] = 0;
						  attr = additionCond;
					  }
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

	  tmpNode = tmpNode2;

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
		if (strcmp(g_quark_to_string(attrList->key), "sodipodi:role") == 0) {
			attrList++;
			continue;
		}
		// High quality of images
		if ((strcmp(tempNode->name(), "svg:image") == 0) &&
			(strcmp(g_quark_to_string(attrList->key), "xlink:href") == 0 ) &&
			rebasePath) {
			tempNode->setAttribute(g_quark_to_string(attrList->key),
					        g_strdup_printf("%s%s", rebasePath, attrList->value));
		} else{
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

bool isTrans(char *patch) {
	char out[10];
	gchar *cmd = g_strdup_printf("convert %s -channel alpha -fx \"(a>0.9)\" "\
			                      "-alpha extract -fill black +opaque \"rgb(255,255,255)\" "
			            		  "-format \"%[fx:w*h*mean/(w*h)*100 > 99]\" info:",
								  patch);
	FILE *f = popen(cmd,"r");

	/* Read the output a line at a time - output it. */
	if (fgets(out, sizeof(out)-1, f) != NULL && out[0] == '1') {
		pclose(f);
		return true;
	} else {
		pclose(f);
		return false;
	}
}

Inkscape::XML::Node *MergeBuilder::saveImage(gchar *name, SvgBuilder *builder) {

	// Save merged image
	gchar* mergedImagePath = g_strdup_printf("%s%s.png", sp_export_svg_path_sh, name);
	gchar *fName;
	Geom::Rect rct = save(mergedImagePath);
	removeOldImages();
	//try convert to jpeg
	if (sp_create_jpeg_sp && isTrans(mergedImagePath)) {
		fName = g_strdup_printf("%s.jpg", name);
		gchar *jpgName =
			g_strdup_printf("%s%s",
				             sp_export_svg_path_sh,
							 fName);
		gchar *cmd =
			g_strdup_printf("convert %s -channel alpha -fx \"(a=1)\"  %s",
				             mergedImagePath,
							 jpgName);
		system(cmd);
		remove(mergedImagePath);
		g_free(jpgName);
		g_free(cmd);
	} else {
		fName = g_strdup_printf("%s.png", name);
	}
	free(mergedImagePath);
	Inkscape::XML::Node *node = generateNode(fName, builder, &rct);
	g_free(fName);
	return node;
}

Geom::Rect MergeBuilder::save(gchar const *filename) {
	std::vector<SPItem*> x;
	char *c;
	float width = strtof(_root->attribute("width"), &c);
	float height = strtof(_root->attribute("height"), &c);

	SPItem *item = (SPItem*)_doc->getRoot()->get_child_by_repr(_mainVisual);
	Geom::OptRect::reference_type sq = _doc->getRoot()->documentVisualBounds().get();
	double x1 = sq[Geom::X][0];
	double x2 = sq[Geom::X][1];
	double y1 = sq[Geom::Y][0];
	double y2 = sq[Geom::Y][1];
	//_doc->setViewBox(Geom::Rect(sq));

	sp_export_png_file(_doc, filename,
					round(x1), height - round(y1), round(x2), height - round(y2), // crop of document x,y,W,H
					(x2-x1) * 3, (y2-y1) * 3, // size of png
					600, 600, // dpi x & y
					0xFFFFFF00,
					NULL, // callback for progress bar
					NULL, // struct SPEBP
					true, // override file
					x);
    return sq;
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
					0x00000000,
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

void MergeBuilder::removeOldImagesEx(Inkscape::XML::Node *startNode) {
	if (! startNode) {
		return;
	}
	Inkscape::XML::Node *tmpNode = startNode->firstChild();
	Inkscape::XML::Node *toImageNode;
	const char *tmpName;
	while(tmpNode) {
		toImageNode = tmpNode;
		while(toImageNode && (strcmp(toImageNode->name(), "svg:image") != 0)) {
			if ((toImageNode->childCount() == 0) && toImageNode->next()) {
			  toImageNode = toImageNode->next();
			} else {
			  toImageNode = toImageNode->firstChild();
			}
		}
		if (toImageNode) { // if it is image node
			tmpName = toImageNode->attribute("xlink:href");
			remove(tmpName);
		}
	    tmpNode = tmpNode->next();
	}
}

void MergeBuilder::removeOldImages(void) {
	removeOldImagesEx(_mainVisual);
}

Inkscape::XML::Node *MergeBuilder::getDefNodeById(char *nodeId, Inkscape::XML::Node *mydef) {
	Inkscape::XML::Node *tmpNode;
	if (mydef) {
		tmpNode = mydef->firstChild();
	}
	else {
		tmpNode = _defs->firstChild();
	}
	Inkscape::XML::Node *resNode = NULL;
	while(tmpNode) {
	  if (strcmp(tmpNode->attribute("id"), nodeId) == 0) { //currect ID
		  resNode = tmpNode;
		  break;
	  }
	  tmpNode = tmpNode->next();
	}
	return resNode;
}

const char *MergeBuilder::findAttribute(Inkscape::XML::Node *node, char *attribName) {
	Inkscape::XML::Node *tmpNode = node;
	while(tmpNode) {
		if (tmpNode->attribute(attribName)) {
			return tmpNode->attribute(attribName);
		}
		tmpNode = tmpNode->firstChild();
	}

	return NULL;
}

void MergeBuilder::removeRelateDefNodes(Inkscape::XML::Node *node) {
  if (node) {
	char *tags[] = {(char*)"style", (char*)"clip-path", (char*)"mask"};
	if (node->childCount() > 0) {
		Inkscape::XML::Node *ch = node->firstChild();
		while(ch) {
			removeRelateDefNodes(ch);
			ch = ch->next();
		}
	}
	for(int i = 0; i < 3; i++) {
		Inkscape::XML::Node *tmpNode = node;
		const char* attrValue = tmpNode->attribute(tags[i]);
		if (attrValue) {
			const char *grIdStart = strstr(attrValue, "url(#");
			const char *grIdEnd;
			if (grIdStart) {
			  grIdStart += 5; // + len of "url(#"
			  grIdEnd = strstr(grIdStart, ")");
			}
			if (grIdStart && grIdEnd) {
				char nodeId[100];
				memcpy(nodeId, grIdStart, grIdEnd - grIdStart);
				nodeId[grIdEnd - grIdStart] = 0;
				Inkscape::XML::Node *remNode = getDefNodeById(nodeId);
				if (remNode) {
					//printf(">>>%s\n", nodeId);
					removeOldImagesEx(getDefNodeById(nodeId, _myDefs));
					removeRelateDefNodes(remNode);
					//printf("<<<%s\n", nodeId);
					remNode->parent()->removeChild(remNode);
					delete remNode;
				}
			}
		}
	}
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

uint mergePredictionCountImages(SvgBuilder *builder) {
  uint resultCount = 0;
  sp_merge_images_sh = (sp_merge_limit_sh > 0) &&
			 (sp_merge_limit_sh <= builder->getCountOfImages());
  sp_merge_path_sh = (sp_merge_limit_path_sh > 0) &&
			 (sp_merge_limit_path_sh <= builder->getCountOfPath());

  if (sp_merge_images_sh || sp_merge_path_sh) {
	Inkscape::XML::Node *root = builder->getRoot();
	Inkscape::XML::Node *remNode;
	Inkscape::XML::Node *toImageNode;
	const gchar *tmpName;

	Inkscape::Extension::Internal::MergeBuilder *mergeBuilder;

	uint countMergedNodes = 0;
	mergeBuilder = new Inkscape::Extension::Internal::MergeBuilder(root, sp_export_svg_path_sh);

	// Add needed tags
	if (sp_merge_images_sh) {
	  mergeBuilder->addTagName(g_strdup_printf("%s", "svg:image"));
	}
	if (sp_merge_path_sh) {
		  mergeBuilder->addTagName(g_strdup_printf("svg:path"));
	      if (sp_rect_how_path_sh) {
	    	  mergeBuilder->addTagName(g_strdup_printf("svg:rect"));
	      }
	}
	Inkscape::XML::Node *mergeNode = mergeBuilder->findFirstNode();
	Inkscape::XML::Node *visualNode;
	if (mergeNode) visualNode = mergeNode->parent();
	while(mergeNode) {
		countMergedNodes = 0;
		mergeBuilder->clearMerge();
		while (mergeNode->next() && mergeBuilder->haveTagFormList(mergeNode->next())) {
			countMergedNodes++;
			remNode = mergeNode;
			mergeNode = mergeNode->next();
		}

		// count prediction
		if (countMergedNodes) {
			resultCount++;
		}

		if (mergeNode)
		  mergeNode =  mergeNode->next();
		while( mergeNode && (! mergeBuilder->haveTagFormList(mergeNode))) {
			mergeNode = mergeNode->next();
		}
	}
	delete mergeBuilder;
  }
  return resultCount;
}

void mergerParseStyle(const char *style, char **names, char values[][100], int count) {
	const char *begin;
	const char *end;
	for(int i = 0; i < count; i++) {
		begin = strstr(style, names[i]);
		if (! begin) {
			values[i][0] = 0;
			continue;
		}
		begin += strlen(names[i]);
		end = strstr(begin, ";");
		if (! end) end = begin + strlen(begin);
		if (end - begin > 100) end = begin + 100;
		memcpy(values[i], begin, end - begin);
		values[i][end-begin] = 0;
	}
}

void mergerParseMatrixString(const char *matrix, double *values) {
	char mat[1024];
	char *pos;
	char *tail;
	strcpy(mat, matrix);
	//prepareStringForFloat(mat);
	pos = strstr(mat, "(") + 1;
	for(int i = 0; i < 6; i++) {
		values[i] = round(g_ascii_strtod(pos, &tail) * 100)/100;//strtod(pos, &tail);
		pos = tail + 1;
	}
}

/**
 * Merge two node if they have content then remove second node
 * return: if merge success return merged node (same: prevNode)
 *         if do not merge return NULL.
 */
Inkscape::XML::Node *generateMergedTextNode(
	Inkscape::XML::Node *prevTextNode,
	Inkscape::XML::Node *currTextNode)
{
	const char *currContent;
	const char *prevContent;
	Inkscape::XML::Node *_tspanPrevNode = prevTextNode->firstChild();
	Inkscape::XML::Node *_tspanCurrNode = currTextNode->firstChild();
	Inkscape::XML::Node *contentPrevNode = prevTextNode->firstChild();
    char *tmpPointer;


	// search text inside nodes
	if (_tspanCurrNode->childCount()) {
	    currContent = _tspanCurrNode->firstChild()->content();
	} else {
		currContent = _tspanCurrNode->content();
	}
	if (_tspanPrevNode->childCount()) {
	    prevContent = _tspanPrevNode->firstChild()->content();
	    contentPrevNode = _tspanPrevNode->firstChild();
	} else {
		prevContent = _tspanPrevNode->content();
		contentPrevNode = _tspanCurrNode;
	}


	char *styleAttribList[] = {
			(char*)"font-weight:",
			(char*)"font-size:",
			(char*)"font-family:"
	};

	char styleValuePrev[3][100];
	char styleValueCurr[3][100];

	// is not text - is not merge
	if (!(prevContent && currContent)) {
		return NULL;
	}

	const char *prevNodeStyle = prevTextNode->attribute("style");
	const char *currNodeStyle = currTextNode->attribute("style");

	const char *prevNodeMatrix = prevTextNode->attribute("transform");
    const char *currNodeMatrix = currTextNode->attribute("transform");
    double prevMatrix[6];
    double currMatrix[6];
    double prevEndGlipX;

    mergerParseStyle(prevNodeStyle, styleAttribList, styleValuePrev,  3);
    mergerParseStyle(currNodeStyle, styleAttribList, styleValueCurr,  3);

    // if font style different we can't merge it
    if ( !(
    		strcmp(styleValuePrev[0], styleValueCurr[0]) == 0 &&
    		strcmp(styleValuePrev[1], styleValueCurr[1]) == 0 &&
			strcmp(styleValuePrev[2], styleValueCurr[2]) == 0 )
       ) {
    	return NULL;
    }

    mergerParseMatrixString(prevNodeMatrix, prevMatrix);
    mergerParseMatrixString(currNodeMatrix, currMatrix);

    // different line position
    if (prevMatrix[5] != currMatrix[5]) {
    	return NULL;
    }

    prevEndGlipX = g_ascii_strtod(prevTextNode->attribute("data-endGlipX"), &tmpPointer);
    double avrWidthOfChar = (prevEndGlipX - prevMatrix[4])/strlen(prevContent);
    if (abs(currMatrix[4] - prevEndGlipX) < (avrWidthOfChar/20)) {
    	// do merge
    	contentPrevNode->setContent(g_strdup_printf("%s%s", prevContent, currContent));
    	currTextNode->parent()->removeChild(currTextNode);
    	delete currTextNode;

    	return prevTextNode;
    } else {
    	return NULL;
    }
}

void mergeFindNearestNodes(Inkscape::XML::Node *node) {
	Inkscape::XML::Node *tmpNode = node->firstChild();
	Inkscape::XML::Node *sumNode;
	Inkscape::XML::Node *prevTextNode = NULL;
	bool textLevel = false;
	while(tmpNode) {
		if (strcmp(tmpNode->name(), "svg:text") == 0) {
			if (prevTextNode) {
				sumNode = generateMergedTextNode(prevTextNode, tmpNode);
				if (sumNode) {
					prevTextNode = sumNode;
					tmpNode = sumNode;
				} else {
					prevTextNode = tmpNode;
				}
			} else {
				prevTextNode = tmpNode;
			}
			textLevel = true;
		} else {
			prevTextNode = 0;
			if (! textLevel) {
				mergeFindNearestNodes(tmpNode);
			}
		}
		tmpNode = tmpNode->next();
	}
}

void mergeNearestTextToOnetag(SvgBuilder *builder) {
	Inkscape::XML::Node *_root = builder->getRoot();
	Inkscape::XML::Node *_defNode = NULL;
	Inkscape::XML::Node *_visualNode = NULL;
	Inkscape::XML::Node *sumNode;
	Inkscape::XML::Node *prevTextNode = NULL;

	// parse top struct of SVG.
	Inkscape::XML::Node *tmpNode = _root->firstChild();
	while(tmpNode) {
		if (strcmp(tmpNode->name(), "svg:defs") == 0) {
			_defNode = tmpNode;
			break;
		}
		tmpNode = tmpNode->next();
	}

	tmpNode = tmpNode->next();
	while(tmpNode) {
		if (strcmp(tmpNode->name(), "svg:g") == 0) {
			_visualNode = tmpNode;
			break;
		}
		tmpNode = tmpNode->next();
	}


	// somthing is wrong
	if (!(_visualNode && _defNode)) return;

	// search two nearest text node
	mergeFindNearestNodes(_visualNode);
}


// do merge tags without text bitweene
void mergeImagePathToLayerSave(SvgBuilder *builder) {
  //================== merge paths and images ==================
  sp_merge_images_sh = (sp_merge_limit_sh > 0) &&
			 (sp_merge_limit_sh <= builder->getCountOfImages());
  sp_merge_path_sh = (sp_merge_limit_path_sh > 0) &&
			 (sp_merge_limit_path_sh <= builder->getCountOfPath());

  if (sp_merge_images_sh || sp_merge_path_sh) {
	Inkscape::XML::Node *root = builder->getRoot();
	//Inkscape::XML::Node *mergeNode = builder->getRoot();
	Inkscape::XML::Node *remNode;
	Inkscape::XML::Node *toImageNode;
	gchar *tmpName;

	Inkscape::Extension::Internal::MergeBuilder *mergeBuilder;

	uint countMergedNodes = 0;
	//find image nodes
	mergeBuilder = new Inkscape::Extension::Internal::MergeBuilder(root, sp_export_svg_path_sh);

	if (sp_merge_images_sh) {
	  mergeBuilder->addTagName(g_strdup_printf("%s", "svg:image"));
	}

	if (sp_merge_path_sh) {
      mergeBuilder->addTagName(g_strdup_printf("svg:path"));
      if (sp_rect_how_path_sh) {
    	  mergeBuilder->addTagName(g_strdup_printf("svg:rect"));
      }
	}
	Inkscape::XML::Node *mergeNode = mergeBuilder->findFirstNode();
	Inkscape::XML::Node *visualNode;
	if (mergeNode) visualNode = mergeNode->parent();
	//print_node(visualNode, 2);
	while(mergeNode) {
		countMergedNodes = 0;
		mergeBuilder->clearMerge();
		while (mergeNode->next() && mergeBuilder->haveTagFormList(mergeNode->next())) {
			countMergedNodes++;
			mergeBuilder->addImageNode(mergeNode, sp_export_svg_path_sh);
			remNode = mergeNode;
			mergeNode = mergeNode->next();
			//print_node(remNode,2);
			mergeBuilder->removeRelateDefNodes(remNode);
			remNode->parent()->removeChild(remNode);
			delete remNode;
		}

		//merge image
		if (countMergedNodes) {
			mergeBuilder->addImageNode(mergeNode, sp_export_svg_path_sh);
			remNode = mergeNode;

			tmpName = g_strdup_printf("%s_img%s", builder->getDocName(), mergeNode->attribute("id"));
			//char *fName = g_strdup_printf("%s_img%s.png", builder->getDocName(), tmpName);

			// Insert node with merged image
			Inkscape::XML::Node *sumNode = mergeBuilder->saveImage(tmpName, builder);
			visualNode->addChild(sumNode, mergeNode);
			mergeBuilder->removeRelateDefNodes(remNode);
			visualNode->removeChild(remNode);

			mergeNode = sumNode->next();
		}
		else { // if do not have two nearest images - can not merge
			mergeNode = mergeNode->next();
		}

		while( mergeNode && (! mergeBuilder->haveTagFormList(mergeNode))) {
			mergeNode = mergeNode->next();
		}
	}
	delete mergeBuilder;
  }
}

// merge all object and put it how background
void mergeImagePathToOneLayer(SvgBuilder *builder) {
	  Inkscape::XML::Node *root = builder->getRoot();
	  Inkscape::XML::Node *remNode;
	  Inkscape::XML::Node *toImageNode;
	  const gchar *tmpName;
	  uint countMergedNodes = 0;

	  Inkscape::Extension::Internal::MergeBuilder *mergeBuilder =
			  new Inkscape::Extension::Internal::MergeBuilder(root, sp_export_svg_path_sh);
	  mergeBuilder->addTagName(g_strdup_printf("%s", "svg:image"));
	  mergeBuilder->addTagName(g_strdup_printf("svg:path"));

	  Inkscape::XML::Node *mergeNode = mergeBuilder->findFirstNode();
	  Inkscape::XML::Node *visualNode;
	  if (mergeNode) visualNode = mergeNode->parent();
	  while(mergeNode) {
		countMergedNodes = 0;
		mergeBuilder->clearMerge();
		while (mergeNode) {
			if (! mergeBuilder->haveTagFormList(mergeNode)) {
				mergeNode = mergeNode->next();
				continue;
			}

			countMergedNodes++;
			mergeBuilder->addImageNode(mergeNode, sp_export_svg_path_sh);
			remNode = mergeNode;
			tmpName = mergeNode->attribute("id");
			mergeNode = mergeNode->next();

			mergeBuilder->removeRelateDefNodes(remNode);
			remNode->parent()->removeChild(remNode);
			delete remNode;
		}

		//merge image
		if (countMergedNodes) {
			if (mergeNode) {
			  mergeBuilder->addImageNode(mergeNode, sp_export_svg_path_sh);
			  remNode = mergeNode;
			}

			char *fName = g_strdup_printf("%s_img%s", builder->getDocName(), tmpName);
			visualNode->addChild(mergeBuilder->saveImage(fName, builder), NULL);
			free(fName);
		}
		else { // if do not have two nearest images - can not merge
			mergeNode = mergeNode->next();
		}
	  }
	  delete mergeBuilder;
}

void mergeMaskGradientToLayer(SvgBuilder *builder) {
	  //================== merge images with masks =================
	  if (sp_merge_mask_sh) {
		  // init MergeBuilder
		  Inkscape::Extension::Internal::MergeBuilder *mergeBuilder =
				  new Inkscape::Extension::Internal::MergeBuilder(builder->getRoot(), sp_export_svg_path_sh);
		  mergeBuilder->addTagName(g_strdup_printf("%s", "svg:image"));
		  mergeBuilder->addTagName(g_strdup_printf("%s", "svg:path"));
		  mergeBuilder->addAttrName(g_strdup_printf("%s", "mask"));
		  mergeBuilder->addAttrName(g_strdup_printf("%s", "style"));

		  // merge masked images
		  Inkscape::XML::Node *mergeNode = mergeBuilder->findFirstAttrNode();
		  Inkscape::XML::Node *remNode;
		  Inkscape::XML::Node *visualNode;
		  if (mergeNode) visualNode = mergeNode->parent();
		  const gchar *tmpName;
		  while(mergeNode) {
			mergeBuilder->clearMerge();
			mergeBuilder->addImageNode(mergeNode, sp_export_svg_path_sh);
			remNode = mergeNode;
			// generate name of new image
			tmpName = mergeNode->attribute("id");
			char *fName = g_strdup_printf("%s_img%s", builder->getDocName(), tmpName);

			// Save merged image

			Inkscape::XML::Node *sumNode = mergeBuilder->saveImage(fName, builder);
			visualNode->addChild(sumNode, mergeNode);
			visualNode->removeChild(remNode);
			mergeBuilder->removeRelateDefNodes(remNode);
	        delete remNode;
			mergeNode = sumNode->next();
			//free(buf);
			free(fName);
			mergeNode = mergeBuilder->findNextAttrNode(mergeNode);
		  }
		  delete mergeBuilder;
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
	print_prefix(level);
	printf("content: %s\n", node->content());
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

