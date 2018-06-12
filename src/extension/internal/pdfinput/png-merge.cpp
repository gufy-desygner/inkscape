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
#include "2geom/transforms.h"
#include "svg/svg.h"
#include "svg/css-ostringstream.h"
#include "xml/repr.h"
#include "xml/sp-css-attr.h"
#include <math.h>
#include "text-editing.h"
#include "svg/stringstream.h"
#include "sp-defs.h"
#include "sp-text.h"
#include "sp-flowtext.h"
#include "path-chemistry.h"
//#include <extension/system.h>
//#include <extension/db.h>

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
	_sourceSubVisual =  _sourceVisual;
	_mainSubVisual = _mainVisual;

	SPItem *spSubVisual = (SPItem*) _doc->getRoot()->get_child_by_repr(_mainVisual);
	affine = spSubVisual->transform.inverse();
	//SPItem *spSubVisual = (SPItem*)spRoot->get_child_by_repr(_sourceSubVisual);
	// todo : calculate affine transforms
	while((_sourceSubVisual->childCount() == 1) && ( strcmp(_sourceSubVisual->firstChild()->name(),"svg:g") == 0 )){
		subVisualDeep++;
		_sourceSubVisual = _sourceSubVisual->firstChild();
		Inkscape::XML::Node *subNode = _xml_doc->createElement("svg:g");
		Inkscape::Util::List<const Inkscape::XML::AttributeRecord > attrList = _sourceSubVisual->attributeList();
		while( attrList ) {
		  subNode->setAttribute(g_quark_to_string(attrList->key), attrList->value);
		  attrList++;
		}
		_mainSubVisual->appendChild(subNode);
		spSubVisual = (SPItem*) spSubVisual->get_child_by_repr(subNode);
		affine *= spSubVisual->transform.inverse();
		_mainSubVisual = subNode;
	}
    //ret = i2doc_affine()
    //    * Geom::Scale(1, -1)
    //    * Geom::Translate(0, document->getHeight().value("px"));
	//affine = spSubVisual->transform;
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

Inkscape::XML::Node *MergeBuilder::findFirstNode(int *count) {
	return findNode(_sourceSubVisual->firstChild(), subVisualDeep, count);
}

Inkscape::XML::Node *MergeBuilder::findFirstAttrNode(void) {
	return findAttrNode(_sourceSubVisual->firstChild());
}

Inkscape::XML::Node *MergeBuilder::findNextNode(Inkscape::XML::Node *node, int level) {
	return findNode(node, level);
}

Inkscape::XML::Node *MergeBuilder::findNextAttrNode(Inkscape::XML::Node *node) {
	return findAttrNode(node);
}

void MergeBuilder::clearMerge(void) {
    while(_mainSubVisual->firstChild()){
    	_mainSubVisual->removeChild(_mainSubVisual->firstChild());
    }
}

Inkscape::XML::Node *MergeBuilder::findNode(Inkscape::XML::Node *node, int level, int *count) {
	Inkscape::XML::Node *tmpNode;
	Inkscape::XML::Node *resNode = NULL;
	if (level > subVisualDeep) return (Inkscape::XML::Node *) NULL;
	if (level < subVisualDeep) {
		if (node->childCount()){
		  tmpNode = node->firstChild();
		  while( tmpNode ) {
			  resNode = findNode(tmpNode, level+1);
			  if (resNode) return resNode;
			  tmpNode = tmpNode->next();
		  }
		}
	}
	if (level == subVisualDeep) {
		tmpNode = node;
		while(tmpNode) {
			if (haveTagFormList(tmpNode, count)) {
				return tmpNode;
			}
			tmpNode = tmpNode->next();
		}
	}

	return resNode;
}

void MergeBuilder::findImageMaskNode(Inkscape::XML::Node * node, std::vector<Inkscape::XML::Node *> *listNodes) {
	Inkscape::XML::Node *tmpNode;
	Inkscape::XML::Node *resNode = NULL;

	tmpNode = node->firstChild();
	while(tmpNode) {
		// check condition
		const char* msk = tmpNode->attribute("mask");
		if (msk && strlen(msk) > 0) {
			// add node to list
			Inkscape::XML::Node *onlyG = tmpNode;
			while(! haveContent(onlyG->parent()) && onlyG->parent() != _sourceSubVisual) {
				onlyG = onlyG->parent();
			}

			if (listNodes->size() == 0 || listNodes->back() != onlyG)
				listNodes->push_back(onlyG);
		} else {
			// check children
			findImageMaskNode(tmpNode, listNodes);
		}
		tmpNode = tmpNode->next();
	}
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
	Geom::Affine aff;

	aff.setExpansionX(rt->width());
	aff.setExpansionY(rt->height());
	aff.setTranslation(Geom::Point(rt->left(), rt->top()));
	aff *= affine;
	char *buf = sp_svg_transform_write(aff);

	Inkscape::XML::Node *sumNode = builder->createElement("svg:g");
	sumNode->setAttribute("transform", buf);

	Inkscape::XML::Node *tmpNode = builder->createElement("svg:image");
	tmpNode->setAttribute("xlink:href", imgPath);
	tmpNode->setAttribute("width", "1");
	tmpNode->setAttribute("height", "1");
	tmpNode->setAttribute("preserveAspectRatio", "none");

	sumNode->appendChild(tmpNode);

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

bool MergeBuilder::haveTagFormList(Inkscape::XML::Node *node, int *count, int level) {
	  Inkscape::XML::Node *tmpNode = node;
	  int countOfnodes = 0;
	  if (node == 0) return false;
	  //if (haveContent(node)) return false;
	  // Calculate count of right svg:g before image
	  while(tmpNode) {
		  if (strcmp(tmpNode->name(), "svg:g") == 0 ||
			  tmpNode->childCount() > 0  ) {
			  haveTagFormList(tmpNode->firstChild(), &countOfnodes, level + 1);
		  } else {
			  for(int i = 0; i < _sizeListMergeTag; i++) {
				  if ((strcmp(tmpNode->name(), _listMergeTags[i]) == 0) && (tmpNode->childCount() == 0)) {
					  countOfnodes++;
				  }
			  }
		  }
		  if (level == 0) break;
	      tmpNode = tmpNode->next();
	  }

      if (count) {
    	  (*count) += countOfnodes;
      }

	  return (countOfnodes > 0);
}


// Try found attribute in any tag
// then try found ordered tag in current or child
bool MergeBuilder::haveTagAttrFormList(Inkscape::XML::Node *node) {
	Inkscape::XML::Node *tmpNode = node;
	  if (tmpNode == 0) return false;
	  Inkscape::XML::Node *tmpNode2 = NULL;
	  bool tag = FALSE;
	  bool attr = FALSE;
	  bool haveMask = FALSE;
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
					  // style tag is right only if it have some parametrs
					  if (strcmp(attrName, "style") == 0) {
						  additionCond = (strstr(styleValue, "Gradient") > 0) ||
								  (strstr(styleValue, "url(#pattern") > 0);
					  }
					  if (strcmp(attrName, "mask") == 0) {
					  	  return true;
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

Geom::Affine MergeBuilder::getAffine(Inkscape::XML::Node *fromNode) {
	Inkscape::XML::Node *tmpNode = fromNode;
	Geom::Affine rezult;
	//std::vector<Inkscape::XML::Node *> listNodes;
	// build chain of parent nodes
	while(tmpNode != _mainSubVisual) {
		//listNodes.push_back(tmpNode);
		SPItem *spNode = (SPItem*)_doc->getObjectByRepr(tmpNode);
		Geom::Affine aff;
		if (sp_svg_transform_read(tmpNode->attribute("transform"), &aff)) {
			rezult *= aff;
		}
		tmpNode = tmpNode->parent();
	}
	return rezult;
}

Inkscape::XML::Node *MergeBuilder::findNodeById(Inkscape::XML::Node *fromNode, const char* id) {
	Inkscape::XML::Node *tmpNode = fromNode;
	Inkscape::XML::Node *tmpResult;
	while(tmpNode) {
		if (strcmp(tmpNode->attribute("id"), id) == 0) {
			return tmpNode;
		}
		if (tmpNode->childCount()) {
			tmpResult = findNodeById(tmpNode->firstChild(), id);
			if (tmpResult) {
				return tmpResult;
			}
		}
		tmpNode = tmpNode->next();
	}
	return 0;
}

Inkscape::XML::Node *MergeBuilder::fillTreeOfParents(Inkscape::XML::Node *fromNode) {
	if (_sourceSubVisual == fromNode) return fromNode;
	Inkscape::XML::Node *tmpNode = fromNode->parent();
	Inkscape::XML::Node *chkParent = findNodeById(_mainSubVisual, tmpNode->attribute("id"));
	if (chkParent) return chkParent;
	std::vector<Inkscape::XML::Node *> listNodes;
	// build chain of parent nodes
	while(tmpNode != _sourceSubVisual) {
		listNodes.push_back(tmpNode);
		tmpNode = tmpNode->parent();
	}

	// create nodes in imageMerger
	Inkscape::XML::Node *appendNode = _mainSubVisual;
	for(int i = listNodes.size() - 1; i >=0 ; i--) {
		Inkscape::XML::Node *childNode = listNodes[i];
		Inkscape::XML::Node *tempNode = _xml_doc->createElement(childNode->name());
		// copy all attributes
		Inkscape::Util::List<const Inkscape::XML::AttributeRecord > attrList = childNode->attributeList();
		while( attrList ) {
			if (strcmp(g_quark_to_string(attrList->key), "sodipodi:role") == 0) {
				attrList++;
				continue;
			}
			tempNode->setAttribute(g_quark_to_string(attrList->key), attrList->value);
			attrList++;
		}
		tempNode->setContent(childNode->content());
		appendNode->appendChild(tempNode);
		appendNode = tempNode;
	}
	return appendNode;
}

Inkscape::XML::Node *MergeBuilder::addImageNode(Inkscape::XML::Node *imageNode, char* rebasePath) {
	if (_sourceSubVisual && imageNode->parent() != _sourceSubVisual && imageNode != _sourceSubVisual) {
		return copyAsChild(fillTreeOfParents(imageNode), imageNode, rebasePath);
	} else
		return copyAsChild(_mainSubVisual, imageNode, rebasePath);
}

void MergeBuilder::mergeAll(char* rebasePath) {
	Inkscape::XML::Node *node;
	node = _sourceSubVisual->firstChild();
	while(node) {
		copyAsChild(_mainSubVisual, node, rebasePath);
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

void draw_crop_line(SvgBuilder *builder, double x1, double y1,
					double x2, double y2, char* name, Inkscape::XML::Node *parent,
					mark_line_style  lineStyle = CROP_MARK_STYLE) {
	Inkscape::XML::Node* pathTag = builder->createElement("svg:path");
	char *style = 0;
	switch(lineStyle) {
		case CROP_MARK_STYLE : style = g_strdup("stroke:#000000;stroke-width:0.3;fill:none");
			break;
		case BLEED_LINE_STYLE : style = g_strdup("stroke:#000000;stroke-width:0.3;fill:none;stroke-miterlimit:4;stroke-dasharray:4, 2, 1, 2;stroke-dashoffset:0");
			break;
		default :
			style = g_strdup("stroke:#000000;stroke-width:0.3;fill:none");
	}
    pathTag->setAttribute("style", style);
    free(style);
    Inkscape::SVGOStringStream strX1;  strX1 << x1;
    Inkscape::SVGOStringStream strX2;  strX2 << x2;
    Inkscape::SVGOStringStream strY1;  strY1 << y1;
    Inkscape::SVGOStringStream strY2;  strY2 << y2;
    gchar  *dAttr = g_strdup_printf("M %s, %s L %s, %s", strX1.str().c_str(), strY1.str().c_str(),
    													strX2.str().c_str(), strY2.str().c_str());
    pathTag->setAttribute("d", dAttr);
    free(dAttr);
    parent->addChild(pathTag, parent->lastChild());
}

void createPrintingMarks(SvgBuilder *builder) {
	SPDocument *doc = builder->getSpDocument();
    te_update_layout_now_recursive(doc->getRoot());

    // Create a group for Crop Mark
	MergeBuilder *tools = new MergeBuilder(builder->getRoot(), 0);
	Inkscape::XML::Node* mainNode = tools->getSourceVisual();

    Geom::Rect sq;
    SPObject *obj = builder->getSpDocument()->getObjectById(mainNode->parent()->attribute("id"));
    Geom::OptRect visualBound(SP_ITEM(obj)->visualBounds());
    if (visualBound) {
    	sq = visualBound.get();
    }
    if (sp_bleed_marks_sh) {
		double x1 = sq[Geom::X][0] - sp_bleed_left_sh;
		double x2 = sq[Geom::X][1] + sp_bleed_right_sh;
		double y1 = sq[Geom::Y][0] - sp_bleed_top_sh ;
		double y2 = sq[Geom::Y][1] + sp_bleed_bottom_sh;
		Inkscape::XML::Node* g_crops = builder->createElement("svg:g");

		// Top left Mark
		draw_crop_line(builder, x1, y1, x1, y1 - CROP_LINE_SIZE, 0, g_crops, BLEED_LINE_STYLE);
		draw_crop_line(builder, x1, y1, x1 - CROP_LINE_SIZE, y1, 0, g_crops, BLEED_LINE_STYLE);

		// Top right Mark
		draw_crop_line(builder, x2, y1, x2, y1 - CROP_LINE_SIZE, 0, g_crops, BLEED_LINE_STYLE);
		draw_crop_line(builder, x2, y1, x2 + CROP_LINE_SIZE, y1, 0, g_crops, BLEED_LINE_STYLE);

		// Bottom left Mark
		draw_crop_line(builder, x1, y2, x1, y2 + CROP_LINE_SIZE, 0, g_crops, BLEED_LINE_STYLE);
		draw_crop_line(builder, x1, y2, x1 - CROP_LINE_SIZE, y2, 0, g_crops, BLEED_LINE_STYLE);

		// Bottom right Mark
		draw_crop_line(builder, x2, y2, x2, y2 + CROP_LINE_SIZE, 0, g_crops, BLEED_LINE_STYLE);
		draw_crop_line(builder, x2, y2, x2 + CROP_LINE_SIZE, y2, 0, g_crops, BLEED_LINE_STYLE);
		mainNode->parent()->addChild(g_crops, mainNode);
    }

    if (sp_crop_mark_sh) {
		Inkscape::XML::Node* g_crops = builder->createElement("svg:g");
		double x1 = sq[Geom::X][0];
		double x2 = sq[Geom::X][1];
		double y1 = sq[Geom::Y][0];
		double y2 = sq[Geom::Y][1];

		// Top left Mark
		draw_crop_line(builder, x1, y1 - sp_crop_mark_shift_sh,
								x1, y1 - CROP_LINE_SIZE - sp_crop_mark_shift_sh,
								0, g_crops, CROP_MARK_STYLE);
		draw_crop_line(builder, x1 - sp_crop_mark_shift_sh, y1,
								x1 - sp_crop_mark_shift_sh - CROP_LINE_SIZE, y1,
								0, g_crops, CROP_MARK_STYLE);

		// Top right Mark
		draw_crop_line(builder, x2, y1 - sp_crop_mark_shift_sh,
								x2, y1 - CROP_LINE_SIZE - sp_crop_mark_shift_sh,
								0, g_crops, CROP_MARK_STYLE);
		draw_crop_line(builder, x2 + sp_crop_mark_shift_sh, y1,
								x2 + sp_crop_mark_shift_sh + CROP_LINE_SIZE, y1,
								0, g_crops, CROP_MARK_STYLE);

		// Bottom left Mark
		draw_crop_line(builder, x1, y2 + sp_crop_mark_shift_sh,
								x1, y2 + CROP_LINE_SIZE + sp_crop_mark_shift_sh,
								0, g_crops, CROP_MARK_STYLE);
		draw_crop_line(builder, x1 - sp_crop_mark_shift_sh, y2,
								x1 - sp_crop_mark_shift_sh - CROP_LINE_SIZE, y2,
								0, g_crops, CROP_MARK_STYLE);

		// Bottom right Mark
		draw_crop_line(builder, x2, y2 + sp_crop_mark_shift_sh, x2,
								y2 + sp_crop_mark_shift_sh + CROP_LINE_SIZE,
								0, g_crops, CROP_MARK_STYLE);
		draw_crop_line(builder, x2 + sp_crop_mark_shift_sh, y2,
								x2 + sp_crop_mark_shift_sh + CROP_LINE_SIZE, y2,
								0, g_crops, CROP_MARK_STYLE);
		mainNode->parent()->addChild(g_crops, mainNode);
    }
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

Inkscape::XML::Node *MergeBuilder::saveImage(gchar *name, SvgBuilder *builder, bool visualBound) {

	// Save merged image
	gchar* mergedImagePath = g_strdup_printf("%s%s.png", sp_export_svg_path_sh, name);
	gchar *fName;
	Geom::Rect rct = save(mergedImagePath, visualBound);
	removeOldImages();
	//try convert to jpeg (if do not have transparent regions)
	if (sp_create_jpeg_sp && isTrans(mergedImagePath)) {
		fName = g_strdup_printf("%s.jpg", name);
		gchar *jpgName =
			g_strdup_printf("%s%s",
				             sp_export_svg_path_sh,
							 fName);
		gchar *cmd =
		    g_strdup_printf("convert %s -background white -flatten %s",
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

Geom::Rect MergeBuilder::save(gchar const *filename, bool adjustVisualBound) {
	std::vector<SPItem*> x;
	char *c;
	Geom::Rect sq = Geom::Rect();
	double x1, x2, y1, y2;
	float width = strtof(_root->attribute("width"), &c);
	float height = strtof(_root->attribute("height"), &c);

	SPItem *item = (SPItem*)_doc->getRoot()->get_child_by_repr(_mainVisual);

	Geom::OptRect visualBound = _doc->getRoot()->documentVisualBounds();
	if (visualBound && adjustVisualBound) {
		sq = visualBound.get();
	} else {
		sq = Geom::Rect(Geom::Point(0, 0),Geom::Point(width, height));
	}
	x1 = sq[Geom::X][0];
	x2 = sq[Geom::X][1];
	y1 = sq[Geom::Y][0];
	y2 = sq[Geom::Y][1];

	// todo: if width*height is zero - need remove this node. Now set virtual minimum size 1x1
	if (((round(x2)-round(x1)) * (round(y2)-round(y1))) < 1) {
		sq = Geom::Rect(Geom::Point(0, 0),Geom::Point(1, 1));
	    x1 = sq[Geom::X][0];
		x2 = sq[Geom::X][1];
		y1 = sq[Geom::Y][0];
		y2 = sq[Geom::Y][1];
	}

	double aproxW = (x2-x1);
	double aproxH = (y2-y1);
	if ( aproxW < 2048 && aproxH < 2048 ) {
		aproxW = (x2-x1) * 3;
		aproxH = (y2-y1) * 3;
	}
	sp_export_png_file(_doc, filename,
					round(x1), height - round(y1), round(x2), height - round(y2), // crop of document x,y,W,H
					aproxW, aproxH, // size of png
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

		if (strcmp(tmpNode->name(), "svg:image") == 0) { // if it is image node
			tmpName = tmpNode->attribute("xlink:href");
			remove(tmpName);
		} else {
			removeOldImagesEx(tmpNode);
		}
	    tmpNode = tmpNode->next();
	}
}

void MergeBuilder::removeOldImages(void) {
	removeOldImagesEx(_mainSubVisual);
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

// remove graph objects from node
// If node empty - remove node too
void MergeBuilder::removeGFromNode(Inkscape::XML::Node *node){
	if (!node) return;
	Inkscape::XML::Node *tmpNode = node->firstChild();
	while(tmpNode) {
		Inkscape::XML::Node *tmpNode2 = tmpNode->next();
		removeGFromNode(tmpNode);
		tmpNode = tmpNode2;
	}
	if ((! node->content() || strlen(node->content()) == 0) && node->childCount() == 0) {
		if (node->parent())
			node->parent()->removeChild(node);
	}
}

int tspan_compare_position(Inkscape::XML::Node **first, Inkscape::XML::Node **second) {
	double firstX;
	double firstY;
    double secondX;
	double secondY;
	static double textSize = 1;
	static Inkscape::XML::Node *parentNode = 0;
	if ((*first)->parent() != parentNode) {
		parentNode = (*first)->parent();
		SPCSSAttr *style = sp_repr_css_attr(parentNode, "style");
		gchar const *fntStrSize = sp_repr_css_property(style, "font-size", "1");
		textSize = g_ascii_strtod(fntStrSize, NULL);
		delete style;
	}

	if (! sp_repr_get_double(*first,  "x", &firstX) ) firstX  = 0;
	if (! sp_repr_get_double(*second, "x", &secondX)) secondX = 0;
	if (! sp_repr_get_double(*first,  "y", &firstY) ) firstY  = 0;
	if (! sp_repr_get_double(*second, "y", &secondY)) secondY = 0;

	//compare
	// round Y to 20% of font size and compare
	if (textSize == 0) textSize = 0.00001;
	if (fabs(firstY - secondY)/textSize < 0.2) {
		if (firstX == secondX) return 0;
		else
			if (firstX < secondX) return -1;
			else return 1;
	} else {
		if (firstY < secondY) return -1;
		else return 1;
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

void mergeTwoTspan(Inkscape::XML::Node *first, Inkscape::XML::Node *second) {
	gchar const *firstContent = first->firstChild()->content();
	gchar const *secondContent = second->firstChild()->content();

	double firstEndX;
	double secondX;
	double spaceSize;
	double wordSpace; // additional distantion betweene words

	// calculate space size
	SPCSSAttr *style = sp_repr_css_attr(first->parent(), "style");
	gchar const *fntStrSize = sp_repr_css_property(style, "font-size", "0.0001");
	sp_repr_get_double(first, "sodipodi:spaceWidth", &spaceSize);
	if ( spaceSize <= 0 ) {
		spaceSize = g_ascii_strtod(fntStrSize, NULL) / 3;
	} else {
		//spaceSize *= g_ascii_strtod(fntStrSize, NULL);
	}

	if (! sp_repr_get_double(first, "sodipodi:wordSpace", &wordSpace)) wordSpace = 0;
	delete style;


	//if (! sp_repr_get_double(first, "sodipodi:space_size", &spaceSize)) spaceSize = 0;
	if (! sp_repr_get_double(first, "sodipodi:end_x", &firstEndX)) firstEndX = 0;
	if (! sp_repr_get_double(second, "x", &secondX)) secondX = 0;

	gchar *firstDx = g_strdup(first->attribute("dx"));
	gchar *secondDx = g_strdup(second->attribute("dx"));
	double different = secondX - firstEndX; // gap for second content

	// if we have some space between space - we can put space to it
	if (different < spaceSize) {
		if ((spaceSize - different)/spaceSize < 0.7) {
			spaceSize = different;
		}
	}
	// Will put space between tspan
	gchar addSpace[2] = {0, 0};
	if (different >= spaceSize) {
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
	first->setAttribute("sodipodi:end_x", second->attribute("sodipodi:end_x"));
	free(mergedContent);
	free(firstDx);
	free(secondDx);
}

void mergeTspanList(GPtrArray *tspanArray) {
	// sort form left to right, from top to bottom
	g_ptr_array_sort(tspanArray, (GCompareFunc)tspan_compare_position);
	double textSize;
	if (tspanArray->len > 0) {
	    Inkscape::XML::Node *textNode = ((Inkscape::XML::Node *)g_ptr_array_index(tspanArray, 0))->parent();
		SPCSSAttr *style = sp_repr_css_attr(textNode, "style");
		gchar const *fntStrSize = sp_repr_css_property(style, "font-size", "0.0001");
		textSize = g_ascii_strtod(fntStrSize, NULL);
		delete style;
	}

	for(int i = 0; i < tspanArray->len - 1; i++) {
		double firstY;
	    double secondY;
	    double firstEndX;
	    double secondX;
	    double spaceSize;
		Inkscape::XML::Node *tspan1 = (Inkscape::XML::Node *)g_ptr_array_index(tspanArray, i);
		Inkscape::XML::Node *tspan2 = (Inkscape::XML::Node *)g_ptr_array_index(tspanArray, i + 1);
		sp_repr_get_double(tspan1, "y", &firstY);
		sp_repr_get_double(tspan2, "y", &secondY);

		if (! sp_repr_get_double(tspan1, "sodipodi:end_x", &firstEndX)) firstEndX = 0;
		if (! sp_repr_get_double(tspan2, "x", &secondX)) secondX = 0;
		if (! sp_repr_get_double(tspan1, "sodipodi:spaceWidth", &spaceSize)) spaceSize = 0;

		if (textSize == 0) textSize = 0.00001;
		// round Y to 20% of font size and compare
		// if gap more then 3.5 of text size - mind other column
		if (fabs(firstY - secondY)/textSize < 0.2 &&
			// litle negative gap
				(fabs(firstEndX - secondX)/textSize < 0.2 || (firstEndX <= secondX)) &&
				(secondX - firstEndX < textSize * 3.5)/* &&
				spaceSize > 0*/) {
			mergeTwoTspan(tspan1, tspan2);
			tspan2->parent()->removeChild(tspan2);
			g_ptr_array_remove_index(tspanArray, i+1);
			i--;
		}
	}

}

// do merge TSPAN for input text node and return resulted text node
Inkscape::XML::Node *MergeBuilder::mergeTspan(Inkscape::XML::Node *textNode) {
	GPtrArray *tspanArray = 0;
	Inkscape::XML::Node *tspanNode = textNode->firstChild();
	while(tspanNode) {
		if (strcmp(tspanNode->name(), "svg:tspan") == 0) {
			if (! tspanArray) {
				tspanArray = g_ptr_array_new();
			}
			g_ptr_array_add(tspanArray, tspanNode);
		} else {
			if (tspanArray && tspanArray->len > 1) {
				mergeTspanList(tspanArray);
			}
			if (tspanArray) g_ptr_array_free(tspanArray, false);
			tspanArray = 0;
		}
		tspanNode = tspanNode->next();
	}
	if (tspanArray && tspanArray->len > 1) {
		mergeTspanList(tspanArray);
	}
	if (tspanArray) g_ptr_array_free(tspanArray, false);
	return 0;
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


// TSPAN merger
// scan node tree
// try find svg:text node and start TSPAN merge for each
void scanTextNodes(Inkscape::XML::Node *mainNode, Inkscape::Extension::Internal::MergeBuilder *mergeBuilder) {
	Inkscape::XML::Node *tmpNode = mainNode->firstChild();
	while(tmpNode) {
		if (strcmp(tmpNode->name(), "svg:text") == 0) {
			mergeBuilder->mergeTspan(tmpNode);
		} else {
			if (tmpNode->childCount()) {
				scanTextNodes(tmpNode, mergeBuilder);
			}
		}
		tmpNode = tmpNode->next();
	}
}

// compare all component except "translate"
bool isCompatibleAffine(Geom::Affine firstAffine, Geom::Affine secondAffine) {
	for(int i = 0; i < 4; i++) {
		if (firstAffine[i] != secondAffine[i])
			return false;
	}
	return true;
}

// SVG:TEXT merger
// Merge text node and change TSPAN x,y
void mergeTextNodesToFirst(GPtrArray *listNodes, SvgBuilder *builder) {
	// load first text node
	Inkscape::XML::Node *mainTextNode = (Inkscape::XML::Node *)g_ptr_array_index(listNodes, 0);
	SPItem *mainSpText = (SPItem*)builder->getSpDocument()->getObjectByRepr(mainTextNode);
	Geom::Affine mainAffine = mainSpText->transform;

	// process for each text node
	for(int i = 1; i < listNodes->len; i++) {
		// load current text node
		Inkscape::XML::Node *currentTextNode = (Inkscape::XML::Node *)g_ptr_array_index(listNodes, i);
		SPItem *currentSpText = (SPItem*)builder->getSpDocument()->getObjectByRepr(currentTextNode);
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
					if (! sp_repr_get_double(currentTspan, "sodipodi:end_x", &adjEndX)) adjEndX = 0;
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
					sp_repr_set_svg_double(currentTspan, "sodipodi:end_x", endPoint.x());
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

			// if was merged all TSPAN nones
			if (! currentTspan) {
				currentTextNode->parent()->removeChild(currentTextNode);
			}
		} else {
			// if affine is not compatible we must start from other "main text node" (current node)
			// so start new merge transaction
			mainTextNode = currentTextNode;
			mainSpText = (SPItem*)builder->getSpDocument()->getObjectByRepr(mainTextNode);
			mainAffine = mainSpText->transform;
		}
	}
}

// SVG:TEXT merger
// collect list of nodes sort it and Start merg of list
Inkscape::XML::Node *textLayerMergeProc(Inkscape::XML::Node *startNode, SvgBuilder *builder) {
	GPtrArray *listNodes = g_ptr_array_new();
	Inkscape::XML::Node *tmpNode = startNode;
	while(tmpNode && (strcmp(tmpNode->name(), "svg:text") == 0)) {
		g_ptr_array_add(listNodes, tmpNode);
		tmpNode = tmpNode->next();
	}
	if (listNodes->len > 1) {
		mergeTextNodesToFirst(listNodes, builder);
	}

	g_ptr_array_free(listNodes, false);
	return tmpNode;
}

// SVG:TEXT merger
// scan document. Try found svg:text and merge text nodes with same style
void scanForTextNodesAndMerge(Inkscape::XML::Node *startNode, SvgBuilder *builder) {
	SPDocument *spDoc = builder->getSpDocument();
	Inkscape::XML::Node *spNode = startNode->firstChild();

	while(spNode) {
		if (strcmp(spNode->name(), "svg:text") == 0) {
			spNode = textLayerMergeProc(spNode, builder);
		} else {
			// recurse
			scanForTextNodesAndMerge(spNode, builder);
			spNode = spNode->next();
		}
	}
}

// TSPAN merger
// enter point from pdf-input
void mergeTspan (SvgBuilder *builder) {
	// init variables
	Inkscape::XML::Node *root = builder->getRoot();
	Inkscape::Extension::Internal::MergeBuilder *mergeBuilder =
			new Inkscape::Extension::Internal::MergeBuilder(root, sp_export_svg_path_sh);

	scanTextNodes(mergeBuilder->getSourceSubVisual(), mergeBuilder);

    delete mergeBuilder;
}

void mergeNearestTextToOnetag(SvgBuilder *builder) {

	// init variables
	Inkscape::XML::Node *root = builder->getRoot();
	Inkscape::Extension::Internal::MergeBuilder *mergeBuilder =
			new Inkscape::Extension::Internal::MergeBuilder(root, sp_export_svg_path_sh);

	scanTextNodes(mergeBuilder->getSourceSubVisual(), mergeBuilder);

	// search two nearest text node
	// mergeFindNearestNodes(mergeBuilder->getSourceSubVisual(), mergeBuilder);
	scanForTextNodesAndMerge(mergeBuilder->getSourceSubVisual(), builder);

	delete mergeBuilder;
}

Inkscape::XML::Node *MergeBuilder::compressGNode(Inkscape::XML::Node *gNode){

	return 0;
}

void scanGtagForCompress(Inkscape::XML::Node *mainNode, SvgBuilder *builder) {
	Inkscape::XML::Node *tmpNode = mainNode->firstChild();
	Inkscape::XML::Node *posNode;
	while(tmpNode) {
		bool fl = false;
		posNode = tmpNode;
		if (strcmp(tmpNode->name(), "svg:g") == 0) {
			fl = true;
			Inkscape::Util::List<const Inkscape::XML::AttributeRecord > attrList = tmpNode->attributeList();
			// Remove only group with <text> tag
			if (tmpNode->childCount() > 0 &&
			    strcmp(tmpNode->firstChild()->name(), "svg:text") != 0 &&
				strcmp(tmpNode->firstChild()->name(), "svg:path") != 0 )
			{
				fl = false;
			}
			while( attrList && fl) {
				// if one from compare is right (== 0) if = false
				if (strncmp(g_quark_to_string(attrList->key), "sodipodi:", 8) &&
				    strcmp(g_quark_to_string(attrList->key), "data-layer") &&
					strcmp(g_quark_to_string(attrList->key), "transform") &&
					strcmp(g_quark_to_string(attrList->key), "id")
				) {
					fl = false;
					break;
				}
			    attrList++;
			}
			// Adjust children and remove group
			if (fl) {
				Geom::Affine mainAffine;
				sp_svg_transform_read(tmpNode->attribute("transform"), &mainAffine);
				if (tmpNode->childCount()) {
					Inkscape::XML::Node *tmpChild = tmpNode->firstChild();
					while(tmpChild) {
						Geom::Affine childAffine;
						// if child have any affine we must merge it with affine from <g> which we want remove.
						sp_svg_transform_read(tmpChild->attribute("transform"), &childAffine);
						char *buf = sp_svg_transform_write(childAffine * mainAffine);
						tmpChild->setAttribute("transform", buf);
						free(buf);
						// move it to the main text node
						posNode->parent()->addChild(tmpChild->duplicate(tmpChild->document()) ,posNode);
						posNode = posNode->next();

						tmpChild = tmpChild->next();
					}
				}
				Inkscape::XML::Node *remNode = tmpNode;
				tmpNode = tmpNode->next();
				remNode->parent()->removeChild(remNode);
			}
		}
		// repeat all for children nodes
		if (tmpNode && tmpNode->childCount()) {
			scanGtagForCompress(tmpNode, builder);
		}
		// If we don't remove <g> node in this cycle - go to next node.
		// else we have next node already
		if ((!fl) && tmpNode)
			tmpNode = tmpNode->next();
	}
}

void compressGtag(SvgBuilder *builder){
	// init variables
	Inkscape::XML::Node *root = builder->getRoot();
	Inkscape::Extension::Internal::MergeBuilder *mergeBuilder =
			new Inkscape::Extension::Internal::MergeBuilder(root, sp_export_svg_path_sh);

	scanGtagForCompress(mergeBuilder->getSourceSubVisual(), builder);

	delete mergeBuilder;
}

void moveTextNode(SvgBuilder *builder, Inkscape::XML::Node *mainNode, Inkscape::XML::Node *currNode, Geom::Affine aff) {
	Inkscape::XML::Node *pos = mainNode; // position for inserting new node
	Inkscape::XML::Node *nextNode;
	if (! currNode) {
		currNode = mainNode;
	}
	Inkscape::XML::Node *chNode = currNode->firstChild();
	while(chNode) {
		nextNode = chNode->next();
		if (strcmp(chNode->name(), "svg:text") == 0) {
			chNode->parent()->removeChild(chNode);
			Inkscape::XML::Node *gNode = builder->createElement("svg:g");
			char *transBuff =  sp_svg_transform_write(aff);
			gNode->setAttribute("transform", transBuff);
			free(transBuff);
			gNode->addChild(chNode, 0);
			pos->parent()->addChild(gNode, pos);
			pos = gNode;
		} else {
			if (chNode->childCount() > 0) {
				Geom::Affine aff2;
				sp_svg_transform_read(chNode->attribute("transform"), &aff2);
				moveTextNode(builder, pos, chNode, aff*aff2);
			}
		}
		chNode = nextNode;
	}
}

void moveTextNode(SvgBuilder *builder, Inkscape::XML::Node *mainNode, Inkscape::XML::Node *currNode=0) {
	Geom::Affine aff;
	sp_svg_transform_read(mainNode->attribute("transform"), &aff);
	moveTextNode(builder, mainNode, 0, aff);
}

/**
 * @describe do merge tags without text between
 * @param builder representation of SVG document
 * @param simulate if true - do not change source document (default false)
 *
 * @return count of generated images
 * */

uint mergeImagePathToLayerSave(SvgBuilder *builder, bool simulate) {
  //================== merge paths and images ==================
  sp_merge_images_sh = (sp_merge_limit_sh > 0) &&
			 (sp_merge_limit_sh <= builder->getCountOfImages());
  sp_merge_path_sh = (sp_merge_limit_path_sh > 0) &&
			 (sp_merge_limit_path_sh <= builder->getCountOfPath());
  uint img_count = 0;

  if (sp_merge_images_sh || sp_merge_path_sh) {
	Inkscape::XML::Node *root = builder->getRoot();
	//Inkscape::XML::Node *remNode;
	Inkscape::XML::Node *toImageNode;
	gchar *tmpName;

	Inkscape::Extension::Internal::MergeBuilder *mergeBuilder;

	int countMergedNodes = 0;
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
	int numberInNode = 0;
	Inkscape::XML::Node *mergeNode = mergeBuilder->findFirstNode(&numberInNode);
	Inkscape::XML::Node *visualNode;
	std::vector<Inkscape::XML::Node *> remNodes;
	if (mergeNode) visualNode = mergeNode->parent();

	while(mergeNode) {
		countMergedNodes = numberInNode - 1;
		if (! simulate) mergeBuilder->clearMerge();
		while (mergeNode->next() && mergeBuilder->haveTagFormList(mergeNode->next(), &countMergedNodes)) {
			if (! simulate) {
				if (has_visible_text(builder->getSpDocument()->getObjectById(mergeNode->attribute("id")))) {
					moveTextNode(builder, mergeNode);
					// we eject text node so can't merge with it
					break;
				}
				mergeBuilder->addImageNode(mergeNode, sp_export_svg_path_sh);
				remNodes.push_back(mergeNode);
			} else {
				// simulate merge only
				if (has_visible_text(builder->getSpDocument()->getObjectById(mergeNode->attribute("id")))) {
					break;
				}
			}
			mergeNode = mergeNode->next();
		}

		//merge image
		if (countMergedNodes) {
			img_count++;
			if (! simulate) {
				if (has_visible_text(builder->getSpDocument()->getObjectById(mergeNode->attribute("id")))) {
					moveTextNode(builder, mergeNode);
				}
				mergeBuilder->addImageNode(mergeNode, sp_export_svg_path_sh);
				remNodes.push_back(mergeNode);

				tmpName = g_strdup_printf("%s_img%s", builder->getDocName(), mergeNode->attribute("id"));

				// Insert node with merged image
				Inkscape::XML::Node *sumNode = mergeBuilder->saveImage(tmpName, builder);
				visualNode->addChild(sumNode, mergeNode);
				//mergeBuilder->removeRelateDefNodes(remNode);
				//mergeBuilder->removeGFromNode(remNode);
				mergeNode = sumNode->next();
				if (! simulate) {
					for(int i = 0; i < remNodes.size(); i++) {
						mergeBuilder->removeRelateDefNodes(remNodes[i]);
						mergeBuilder->removeGFromNode(remNodes[i]);
					}
					remNodes.clear();
				}
			} else {
				mergeNode = mergeNode->next();
			}

		}
		else { // if do not have two nearest images - can not merge
			mergeNode = mergeNode->next();
		}

		while( mergeNode && (! mergeBuilder->haveTagFormList(mergeNode, &numberInNode))) {
			mergeNode = mergeNode->next();
		}
	}
	delete mergeBuilder;
  }
  return img_count;
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



void mergeMaskToImage(SvgBuilder *builder) {
	  //================== merge images with masks =================
	  if (sp_merge_mask_sh) {
		  // init MergeBuilder
		  Inkscape::Extension::Internal::MergeBuilder *mergeBuilder =
				  new Inkscape::Extension::Internal::MergeBuilder(builder->getRoot(), sp_export_svg_path_sh);
		  // queue for delete
		  std::vector<Inkscape::XML::Node *> remNodes;

		  // queue for merge
		  std::vector<Inkscape::XML::Node *> listNodes;

		  // merge masked images
		  mergeBuilder->findImageMaskNode(mergeBuilder->getSourceSubVisual(), &listNodes);

		  const gchar *tmpName;
		  mergeBuilder->clearMerge();
		  for(int i = 0; i < listNodes.size(); i++) {
			  Inkscape::XML::Node *mergingNode = listNodes[i];
			  // find text nodes and save it
			  moveTextNode(builder, mergingNode);
			  Inkscape::XML::Node *addedNode = mergeBuilder->addImageNode(mergingNode, sp_export_svg_path_sh);
			  remNodes.push_back(mergingNode);
			  // if next node in list near current we can merge it together
			  if ((i + 1) < listNodes.size() && listNodes[i+1] == mergingNode->next()) {
				  continue;
			  }
			  // if we have gap between nodes we must merge it separately.
			  // generate name of new image
			  tmpName = mergingNode->attribute("id");
			  char *fName = g_strdup_printf("%s_img%s", builder->getDocName(), tmpName);
			  // Save merged image
			  Inkscape::XML::Node *sumNode = mergeBuilder->saveImage(fName, builder, false);
			  // sumNode have affine related from mainVisualNode node.
			  // We must adjust affine transform for current parent
			  Geom::Affine sumAff;
			  Geom::Affine aff =  mergeBuilder->getAffine(addedNode->parent());
			  sp_svg_transform_read(sumNode->attribute("transform"), &sumAff);
			  sumAff *= aff.inverse();
			  char *buff = sp_svg_transform_write(sumAff);
			  if (buff) {
				 sumNode->setAttribute("transform", buff);
				 free(buff);
			  }
			  mergingNode->parent()->addChild(sumNode, mergingNode);
			  mergeBuilder->clearMerge();
			  free(fName);
		  }
		  // remove old nodes
		  for(int i = 0; i < remNodes.size(); i++) {
			  remNodes[i]->parent()->removeChild(remNodes[i]);
			  mergeBuilder->removeRelateDefNodes(remNodes[i]);
			  delete remNodes[i];
		  }

		  delete mergeBuilder;
	  }
}

void mergeMaskGradientToLayer(SvgBuilder *builder) {
	  //================== merge images with masks =================
	  if (sp_merge_mask_sh) {
		  // init MergeBuilder
		  Inkscape::Extension::Internal::MergeBuilder *mergeBuilder =
				  new Inkscape::Extension::Internal::MergeBuilder(builder->getRoot(), sp_export_svg_path_sh);
		  mergeBuilder->addTagName(g_strdup_printf("%s", "svg:image"));
		  mergeBuilder->addTagName(g_strdup_printf("%s", "svg:path"));
		  mergeBuilder->addTagName(g_strdup_printf("%s", "svg:rect"));

		  mergeBuilder->addAttrName(g_strdup_printf("%s", "mask"));
		  mergeBuilder->addAttrName(g_strdup_printf("%s", "style"));
		  // queue for delete
		  std::vector<Inkscape::XML::Node *> remNodes;

		  // merge masked images
		  Inkscape::XML::Node *mergeNode = mergeBuilder->findFirstAttrNode();
		  //Inkscape::XML::Node *remNode;
		  Inkscape::XML::Node *visualNode;
		  if (mergeNode) visualNode = mergeNode->parent();
		  const gchar *tmpName;
		  mergeBuilder->clearMerge();
		  while(mergeNode) {
			// find text nodes and save it
			moveTextNode(builder, mergeNode);

			// merge
			mergeBuilder->addImageNode(mergeNode, sp_export_svg_path_sh);
			remNodes.push_back(mergeNode);

			if ( ! mergeBuilder->haveTagAttrFormList(mergeNode->next())) {
				// generate name of new image
				tmpName = mergeNode->attribute("id");
				char *fName = g_strdup_printf("%s_img%s", builder->getDocName(), tmpName);

				// Save merged image
				Inkscape::XML::Node *sumNode = mergeBuilder->saveImage(fName, builder);
				visualNode->addChild(sumNode, mergeNode);
				mergeBuilder->clearMerge();
				mergeNode = sumNode->next();
				free(fName);
			} else {
				mergeNode = mergeNode->next();
			}

			mergeNode = mergeBuilder->findNextAttrNode(mergeNode);
		  }
		  // remove old nodes
		  for(int i = 0; i < remNodes.size(); i++) {
			  remNodes[i]->parent()->removeChild(remNodes[i]);
			  mergeBuilder->removeRelateDefNodes(remNodes[i]);
			  delete remNodes[i];
		  }

		  delete mergeBuilder;
	  }

}

void enumerationTagsStart(SvgBuilder *builder){
	Inkscape::XML::Node *visualNode = ((SPObject*) builder->getSpDocument()->getDefs())->getRepr();
	while(visualNode && strcmp(visualNode->name(), "svg:g") !=0 ) {
		visualNode = visualNode->next();
	}
	enumerationTags(visualNode);
}

void enumerationTags(Inkscape::XML::Node *inNode) {
	Inkscape::XML::Node *tmpNode = inNode;
	int num = 0;
	while(tmpNode) {
		sp_repr_set_int(tmpNode, "data-layer", num);
		if (tmpNode->childCount()) {
			enumerationTags(tmpNode->firstChild());
		}
		num++;
		tmpNode = tmpNode->next();
	}
}

#if PROFILER_ENABLE == 1
double profiler_timer[TIMER_NUMBER];
double profiler_timer_up[TIMER_NUMBER];
#endif

double GetTickCount(void)
{
  struct timespec now;
  if (clock_gettime(CLOCK_MONOTONIC, &now))
    return 0;
  return now.tv_sec * 1000.0 + now.tv_nsec / 1000000.0;
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

char *readLineFromFile(FILE *fl) {
	char buff[512];
	int n = 0;
	int c;
	c = fgetc(fl);
	while(c == '\n' || c == '\r') {
		c = fgetc(fl);
	}
	while(c != EOF && c != '\n' && c != '\r') {
		buff[n++] = (char)c;
		buff[n] = 0;
		c = fgetc(fl);
	}
	if (n == 0) {
		return 0;
	}
	char *res = strdup(buff);
	return res;
}


