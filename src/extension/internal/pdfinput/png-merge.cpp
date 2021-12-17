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
#include "sp-image.h"
#include "path-chemistry.h"
#include "xml/text-node.h"
#include "extension/db.h"
#include "extension/system.h"
#include "sp-path.h"
#include "2geom/curve.h"

/**
 * @describe how big part of kind rectangle intersected with main rectangle
 *
 * @return percent
 */
double rectIntersect(const Geom::Rect& main, const Geom::Rect& kind)
{
	if (! main.intersects(kind)) return 0;

	double squareOfKind = std::fabs(kind[Geom::X][0] - kind[Geom::X][1]) * std::fabs(kind[Geom::Y][0] - kind[Geom::Y][1]);
	if (squareOfKind == 0) return 0;

	double x0 = (kind[Geom::X][0] < main[Geom::X][0]) ? main[Geom::X][0] : kind[Geom::X][0];
	double x1 = (kind[Geom::X][1] > main[Geom::X][1]) ? main[Geom::X][1] : kind[Geom::X][1];

	double y0 = (kind[Geom::Y][0] < main[Geom::Y][0]) ? main[Geom::Y][0] : kind[Geom::Y][0];
	double y1 = (kind[Geom::Y][1] > main[Geom::Y][1]) ? main[Geom::Y][1] : kind[Geom::Y][1];

	double squareOfintersects = std::fabs(x1 - x0) * std::fabs(y1 - y0);

	return (squareOfintersects/squareOfKind) * 100;
}

bool isNotTable(Inkscape::XML::Node *node)
{
	const char* classes = node->attribute("class");
	if (classes == nullptr) return true;
	return (strcmp(classes, "table") != 0);
}


namespace Inkscape {
namespace Extension {
namespace Internal {

bool isTableNode(Inkscape::XML::Node* node)
{
	if (node == nullptr) return false;
	const char* className = node->attribute("class");
	if (className == nullptr) return false;
	return (strcmp(className,"table") == 0);
}

MergeBuilder::MergeBuilder(Inkscape::XML::Node *sourceTree, gchar *rebasePath)
{
	_sizeListMergeTag = 0;
	_sizeListMergeAttr = 0;
	_doc = SPDocument::createNewDoc(NULL, TRUE, TRUE);
	Inkscape::DocumentUndo::setUndoSensitive(_doc, false);
	_root = _doc->rroot;
	_xml_doc = _doc->getReprDoc();
	_sourceRoot = sourceTree;

	// copy all attributes to root element
	Inkscape::Util::List<const Inkscape::XML::AttributeRecord > attrList = sourceTree->attributeList();
	while( attrList ) {
	    _root->setAttribute(g_quark_to_string(attrList->key), attrList->value);
	    attrList++;
	}


	SPDefs *spDefs = _doc->getDefs();
	_myDefs = spDefs->getRepr();
	Inkscape::XML::Node *tmpNode = sourceTree->firstChild();
	// Copy all no visual nodes from original doc
	Inkscape::XML::Node *waitNode;
	while(tmpNode) {
		if ( strcmp(tmpNode->name(),"svg:metadata") == 0 )
		{
			tmpNode = tmpNode->next();
			continue;
		}
		if ( strcmp(tmpNode->name(),"svg:g") != 0 ) {
			if ( strcmp(tmpNode->name(),"svg:defs") == 0 ) {
				_defs = tmpNode;
				//_myDefs = waitNode;

				Inkscape::XML::Node *defNode = tmpNode->firstChild();
				while(defNode)
				{
					waitNode = copyAsChild(_myDefs, defNode, rebasePath);
					defNode = defNode->next();
				}
				tmpNode = tmpNode->next();
				continue;
			}
			waitNode = copyAsChild(_root, tmpNode, rebasePath);
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
    	Inkscape::XML::Node *tmpNode = _mainSubVisual->firstChild();
    	_mainSubVisual->removeChild(_mainSubVisual->firstChild());
    	delete(tmpNode);
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
			if (! sp_merge_mask_clean_sp) {
				while(! haveContent(onlyG->parent()) && onlyG->parent() != _sourceSubVisual) {
					onlyG = onlyG->parent();
				}
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

Inkscape::XML::Node *MergeBuilder::generateNode(const char* imgPath, SvgBuilder *builder, Geom::Rect *rt, Geom::Affine affine) {
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
	//tmpNode->setAttribute("sodipodi:img_width", "10");
	//tmpNode->setAttribute("sodipodi:img_height", "10");

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

bool MergeBuilder::haveTagFormList(Inkscape::XML::Node *node, int *count, int level, bool excludeTable) {
	  Inkscape::XML::Node *tmpNode = node;
	  int countOfnodes = 0;
	  if (node == 0) return false;
	  if (excludeTable && isTableNode(node)) return false;
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
								  (strstr(styleValue, "url(#pattern") > 0) ||
								  (strstr(styleValue, "fill:url(#linearGradient") > 0);
						  //printf("style=%s\n",styleValue);
						  //printf("id=%s\n",tmpNode->attribute("id"));
						  //printf("%s\n", additionCond ? "true" : "false");
					  }
					  if (strcmp(attrName, "mask") == 0) {
					  	  return true;
					  }
					  if (strcmp(attrName, "fill") == 0) {
						  additionCond = (strstr("url(#linearGradient", styleValue) > 0);
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

		  if (tmpNode->childCount() > 0)
		  {
			  if(haveTagAttrFormList(tmpNode->firstChild()))
			  	  return true;
		  }

		  if (/*(tmpNode->childCount() == 0) && */tmpNode->next()) {
			  //tmpNode = tmpNode->next();
			  coun--;
		  } else {
		      //tmpNode = tmpNode->firstChild();
		  }
		  tmpNode = tmpNode->next();
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
		//SPItem *spNode = (SPItem*)_doc->getObjectByRepr(tmpNode);
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
	if (tmpNode == nullptr) tmpNode = _sourceSubVisual;
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

std::vector<Geom::Rect> MergeBuilder::getRegions()
{
	std::vector<std::string> tags;
	tags.push_back("svg:path");
	tags.push_back("svg:image");
	tags.push_back("svg:rect");
	std::vector<Geom::Rect> result;

   	SPDocument *spDoc = _doc;
	SPRoot* spRoot = spDoc->getRoot();
	te_update_layout_now_recursive(spRoot);

	Inkscape::XML::Node *mainNode = _mainVisual;// getMainNode();
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
			//NodeList region;
			double x1,x2,y1,y2;
			bool squareInit = false;
			for(NodeState* regionNode : currentRegion)
			{
				if (! squareInit)
				{
					x1 = regionNode->sqBBox[Geom::X][0];
					x2 = regionNode->sqBBox[Geom::X][1];
					y1 = regionNode->sqBBox[Geom::Y][0];
					y2 = regionNode->sqBBox[Geom::Y][1];
					squareInit = true;
				}
				if (regionNode->sqBBox[Geom::X][0] < x1)
					x1 = regionNode->sqBBox[Geom::X][0];
				if (regionNode->sqBBox[Geom::X][1] > x2)
					x2 = regionNode->sqBBox[Geom::X][1];
				if (regionNode->sqBBox[Geom::Y][0] < y1)
					y1 = regionNode->sqBBox[Geom::Y][0];
				if (regionNode->sqBBox[Geom::Y][1] > y2)
					y2 = regionNode->sqBBox[Geom::Y][1];
				//region.push_back(regionNode->node);
			}
			Geom::Rect sqBBox(x1,y1,x2,y2);
			result.push_back(sqBBox);
		}
		else break;
	};

	return result;
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
		const char* attrKey = g_quark_to_string(attrList->key);
		if ((strcmp(tempNode->name(), "svg:image") == 0) &&
			(strcmp(attrKey, "xlink:href") == 0 ) &&
			rebasePath) {
			tempNode->setAttribute(g_quark_to_string(attrList->key),
					        g_strdup_printf("%s%s", rebasePath, attrList->value));
		} else{
		    tempNode->setAttribute(attrKey, attrList->value);
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



int64_t svg_get_number_of_objects(Inkscape::XML::Node *node, ApproveNode* approve) {
	int64_t count_of_nodes = 0;
	Inkscape::XML::Node *childNode = node->firstChild();
	while(childNode) {
		if (approve != nullptr && (! approve(childNode))) {
			childNode = childNode->next();
			continue;
		}
		count_of_nodes++;
		count_of_nodes += svg_get_number_of_objects(childNode, approve);
		childNode = childNode->next();
	}
	return count_of_nodes;
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
    Geom::Rect sqBBox;
    SPViewBox spViewBox;
    SPObject *obj = builder->getSpDocument()->getObjectById(mainNode->parent()->attribute("id"));
    Geom::OptRect viewBox(builder->getSpDocument()->getViewBox());
    Geom::OptRect visualBound(SP_ITEM(obj)->visualBounds());
    if (visualBound) {
    	sqBBox = visualBound.get();
    }

    if (viewBox) {
    	sq = viewBox.get();
    }

    if (sp_bleed_marks_sh) {
		double x1 = sq[Geom::X][0] + sp_bleed_left_sh;
		double x2 = sq[Geom::X][1] - sp_bleed_right_sh;
		double y1 = sq[Geom::Y][0] + sp_bleed_top_sh ;
		double y2 = sq[Geom::Y][1] - sp_bleed_bottom_sh;

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
		double x1 = sqBBox[Geom::X][0];
		double x2 = sqBBox[Geom::X][1];
		double y1 = sqBBox[Geom::Y][0];
		double y2 = sqBBox[Geom::Y][1];

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

Inkscape::XML::Node *MergeBuilder::saveImage(gchar *name, SvgBuilder *builder, bool visualBound, double &resultDpi, Geom::Rect* rect) {

	// Save merged image
	gchar* mergedImagePath = g_strdup_printf("%s%s.png", sp_export_svg_path_sh, name);
	gchar *fName;
	Geom::Rect rct = save(mergedImagePath, visualBound, resultDpi, rect);
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
	Inkscape::XML::Node *node = generateNode(fName, builder, &rct, affine);
	g_free(fName);
	return node;
}

void MergeBuilder::getMinMaxDpi(SPItem* node, double &min, double &max, Geom::Affine &innerAffine)
{

	SPItem* tmpNode = (SPItem*)node->firstChild();

	while(tmpNode)
	{
		Geom::Affine transform = innerAffine * tmpNode->transform;
		Geom::Point zoom = transform.expansion();

		Inkscape::XML::Node* xmlNode = tmpNode->getRepr();

		if (strcmp(xmlNode->name(), "svg:image") == 0)
		{
			double width, height;
			const char* strWidth = xmlNode->attribute("sodipodi:img_width");
			const char* strHeight = xmlNode->attribute("sodipodi:img_height");
			width = (strWidth ? std::strtod(strWidth, nullptr) : 0);
			height = (strHeight ? std::strtod(strHeight, nullptr) : 0);
			const float wDpi = width/zoom.x() * 96;
			const float hDpi = height/zoom.y() * 96;
			min = MIN(min, MIN(wDpi, hDpi));
			max = MAX(max, MAX(wDpi, hDpi));
		}

		getMinMaxDpi((SPItem*)tmpNode, min, max, transform);
		tmpNode = (SPItem*)tmpNode->next;
	}
}


Geom::Rect MergeBuilder::save(gchar const *filename, bool adjustVisualBound, double &resultDpi, Geom::Rect* rect) {
	std::vector<SPItem*> x;
	char *c;
	Geom::Rect sq = Geom::Rect();
	double x1, x2, y1, y2;
	float width = strtof(_root->attribute("width"), &c);
	float height = strtof(_root->attribute("height"), &c);

	SPItem *item = (SPItem*)_doc->getRoot()->get_child_by_repr(_mainVisual);


	if (adjustVisualBound && sp_adjust_mask_size_sh)
	{
		Geom::OptRect testVisualBound = _doc->getRoot()->documentVisualBounds();
		if (!testVisualBound)
			_doc->getRoot()->updateRepr(15); // 15 is flags b1111
	}
	Geom::OptRect visualBound = _doc->getRoot()->documentVisualBounds();

	double minDpi = 1200, maxDpi = 0;
	if (sp_preserve_dpi_sp) {
		getMinMaxDpi(item, minDpi, maxDpi, item->transform);
		if (maxDpi < minDpi) maxDpi = minDpi;
	}

	if (visualBound && adjustVisualBound) {
		if (rect) sq = *rect;
		else sq = visualBound.get();
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

	if ( aproxW < 2048 && aproxH < 2048 && maxDpi > 0) {
		if (sp_preserve_dpi_sp) {
			if (minDpi >= 310) {
				resultDpi = 310;
			} else if (minDpi >= 290 && minDpi < 310) {
				resultDpi = minDpi;
			} else if (maxDpi >= 290) {
				resultDpi = 285;
			} else if (maxDpi < 290) {
				resultDpi = maxDpi;
			}
		} else {
			resultDpi = 3 * 96;
		}
		aproxW *= resultDpi/96.0;
		aproxH *= resultDpi/96.0;
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

static int tspan_compare_position(Inkscape::XML::Node **first, Inkscape::XML::Node **second)
{
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

static void mergeTspanList(GPtrArray *tspanArray) {
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
			g_ptr_array_remove_index(tspanArray, i+1);
			i--;
		}
	}
}

NodeList gArrayNodesToVectorNodes(GPtrArray *tspanArray)
{
	NodeList nodeList;
	for(int i = 0; i < tspanArray->len - 1; i++)
	{
		Inkscape::XML::Node *tspan = (Inkscape::XML::Node *)g_ptr_array_index(tspanArray, i);
		nodeList.push_back(tspan);
	}
	return nodeList;
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
		// repeat all for children nodes
		if (tmpNode && tmpNode->childCount()) {
			scanGtagForCompress(tmpNode, builder);
		}
		if (strcmp(tmpNode->name(), "svg:g") == 0) {
			fl = true;
			// Remove only group with <text> tag
			if (tmpNode->childCount() > 0 &&
			    strcmp(tmpNode->firstChild()->name(), "svg:text") != 0 &&
				strcmp(tmpNode->firstChild()->name(), "svg:path") != 0 )
			{
				fl = false;
			}

			SvgBuilder::todoRemoveClip canRemoveClip = builder->checkClipAroundText(tmpNode);
			if (canRemoveClip == SvgBuilder::todoRemoveClip::OUT_OF_CLIP)
			{
				Inkscape::XML::Node* delNode = tmpNode;
				tmpNode = tmpNode->next();
				delNode->parent()->removeChild(delNode);
				continue;
			}
			Inkscape::Util::List<const Inkscape::XML::AttributeRecord > attrList = tmpNode->attributeList();
			// We can remove only empty <g> or if it contains only info-attributes
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
						Inkscape::XML::ElementNode *ppp = (Inkscape::XML::ElementNode *)(void *)posNode;
						//ppp->lock = true;
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

/**
 * @description recreate all text nodes from child of mainNode as child of mainNode
 *
 * @builder representation of SVG
 * @param mainNode node after which will put all text nodes
 * @param currNode node from which do scan for text tags
 * @param aff affine matrix for transformation objects from currNode to mainNode
 */
void moveTextNode(SvgBuilder *builder, Inkscape::XML::Node *mainNode, Inkscape::XML::Node *currNode, Geom::Affine aff, ApproveNode* approve)
{
	// position for inserting new node
	// each new text node will shift this position to self
	Inkscape::XML::Node *pos = mainNode;
	Inkscape::XML::Node *nextNode;
	if (! currNode) {
		currNode = mainNode;
	}
	Inkscape::XML::Node *chNode = currNode->firstChild();
	while(chNode) {
		if (approve != nullptr && (! approve(chNode)))
		{
			chNode = chNode->next();
			continue;
		}
		nextNode = chNode->next();
		// move found text node
		if (strcmp(chNode->name(), "svg:text") == 0) {
			if (chNode->parent() != mainNode->parent()) { // already have right position
				Geom::Affine affText;
				sp_svg_transform_read(chNode->attribute("transform"), &affText);
				// disconnect from previous parent
				chNode->parent()->removeChild(chNode);
				// create new representation
				char *transBuff =  sp_svg_transform_write(affText * aff);
				chNode->setAttribute("transform", transBuff);
				free(transBuff);
				pos->parent()->addChild(chNode, pos);
				// shift position for insert next node
				pos = chNode;
			}
		} else {
			// if is not TEXT node and have child - applay move for children
			if (chNode->childCount() > 0) {
				//accumulate affine transform
				Geom::Affine aff2;
				sp_svg_transform_read(chNode->attribute("transform"), &aff2);
				// move children to 'pos'
				moveTextNode(builder, pos, chNode, aff2 * aff, approve);
			}
		}
		chNode = nextNode;
	}
}

void moveTextNode(SvgBuilder *builder, Inkscape::XML::Node *mainNode, Inkscape::XML::Node *currNode, ApproveNode* approve) {
	Geom::Affine aff;
	if (mainNode->parent() != currNode)
		sp_svg_transform_read(mainNode->attribute("transform"), &aff);
	moveTextNode(builder, mainNode, currNode, aff, approve);
}

TabLine::TabLine(Inkscape::XML::Node* node, const Geom::Curve& curve, SPDocument* spDoc) :
		isVert(false),
		node(node)
{
	lookLikeTab = false;

	if (strcmp(node->name(), "svg:path") != 0)
		return;

	SPPath* spPath = (SPPath*)spDoc->getObjectByRepr(node);
	spCurve = spPath->getCurve();
	//size_t segmentCount = curve->get_segment_count();
	//if (segmentCount > 1 )
	//	return;

	if (! curve.isLineSegment())
		return;

	Geom::Point start = curve.initialPoint();
	Geom::Point end = curve.finalPoint();

	x1 = start[0];
	x2 = end[0];
	y1 = start[1];
	y2 = end[1];
	//printf("   line (%f %f) (%f %f)\n", x1, y1, x2, y2);
	if (approxEqual(x1, x2) || approxEqual(y1, y2))
		lookLikeTab = true;

	if (approxEqual(x1, x2)) isVert = true;
}

TabRect::TabRect(double _x1, double _y1, double _x2, double _y2, Inkscape::XML::Node* _node) :
	x1(_x1),
	x2(_x2),
	y1(_y1),
	y2(_y2),
	node(_node)
{

}

TabRect::TabRect(Geom::Point point1, Geom::Point point2, Inkscape::XML::Node* _node) :
	x1(point1.x()),
	x2(point2.x()),
	y1(point1.y()),
	y2(point2.y()),
	node(_node)
{


}



TabRect* TableRegion::matchRect(double _x1, double _y1, double _x2, double _y2)
{
	Geom::Rect inRect(_x1, _y1, _x2, _y2);
	TabRect* possibleRect = nullptr;
	for(auto& rect : rects)
	{
		// Always use round for better results, this will avoid rects not being mactched!
		Geom::Rect currentRect(round(rect->x1), round(rect->y1), round(rect->x2), round(rect->y2));
		if (rectIntersect(currentRect, inRect) > 90)
		{
			SPItem* spNode = (SPItem*) _builder->getSpDocument()->getObjectByRepr(rect->node);
			const char* fillStyle = spNode->getStyleProperty("fill", "none");

			if (strncmp(fillStyle, "#ffffff", 7) == 0)
			{
				possibleRect = rect;
				continue;
			}

			if (strncmp(fillStyle, "none", 4) != 0)
				return rect;
		}
	}

	return possibleRect;
}

TabLine* TableRegion::searchByPoint(double xCoord, double yCoord, bool isVerticale)
{

	TabLine* rectLine = nullptr;
	for(auto& line : lines)
	{
		if (isVerticale)
		{
			if (! line->isVertical()) continue;
			if (! approxEqual(line->x1, xCoord, 0.51)) continue;
			if (line->y1 > yCoord || line->y2 < yCoord) continue;

			size_t segmentCount = line->curveSegmentsCount();
			if (segmentCount > 1) rectLine = line; // simple lines is more contrast boorder
			else return line;
		} else {
			if (line->isVertical()) continue;
			if (! approxEqual(line->y1, yCoord, 0.51)) continue;
			if (line->x1 > xCoord || line->x2 < xCoord) continue;

			size_t segmentCount = line->curveSegmentsCount();
			if (segmentCount > 1) rectLine = line; // simple lines is more contrast boorder
			else return line;
		}
	}

	return rectLine;
}


std::string doubleToCss(double num)
{
	Inkscape::CSSOStringStream os;
	os << num;
	return os.str();
}

SPCSSAttr* adjustStroke(TabLine* tabLine)
{
	double strokeWidth;
	SPCSSAttr *style = sp_repr_css_attr(tabLine->node, "style");
	const gchar *strokeWidthStr = sp_repr_css_property(style, "stroke-width", "1");
	if (! sp_svg_number_read_d(strokeWidthStr, &strokeWidth))
		strokeWidth = 0;

	if (tabLine->isVertical())
		strokeWidth *= tabLine->affineToMainNode[0];
	else
		strokeWidth *= tabLine->affineToMainNode[3];

	sp_repr_css_set_property(style, "stroke-width", doubleToCss(std::fabs(strokeWidth)).c_str());
	return style;
}

Inkscape::XML::Node* TableDefenition::getTopBorder(SvgBuilder *builder, int c, int r, Geom::Affine aff)
{
	/*<line class="table-border table-border-v index-r-0 index-c-0 table-border-editor"
	 * stroke="#f40101" stroke-dasharray="0 0" stroke-linecap="round" stroke-width="1" x1="47.5" x2="47.5" y1="321" y2="371"
	 * id="svg_109"></line>
	 */

	NodeList borders;
	TableCell* cell = getCell(c, r);
	if (cell->topLine == nullptr) return nullptr;

	SPCSSAttr* cssStyle = adjustStroke(cell->topLine);

	Inkscape::XML::Node* borderNode = builder->createElement("svg:line");
	std::string classOfBorder("table-border table-border-h index-r-" +
			std::to_string(r) +
			" index-c-" +
			// Bug 10
			//"  table-border-editor");
			std::to_string(c));
	//borderNode->setAttribute("style", style);
	sp_repr_css_set(borderNode, cssStyle, "style");
	delete(cssStyle);

	borderNode->setAttribute("class", classOfBorder.c_str());

	borderNode->setAttribute("x1", doubleToCss(cell->x).c_str());
	borderNode->setAttribute("y1", doubleToCss(cell->y + cell->height).c_str());
	borderNode->setAttribute("x2", doubleToCss(cell->x + cell->width).c_str());
	borderNode->setAttribute("y2", doubleToCss(cell->y + cell->height).c_str());

	return borderNode;
}

Inkscape::XML::Node* TableDefenition::getBottomBorder(SvgBuilder *builder, int c, int r, Geom::Affine aff)
{
	/*<line class="table-border table-border-v index-r-0 index-c-0 table-border-editor"
	 * stroke="#f40101" stroke-dasharray="0 0" stroke-linecap="round" stroke-width="1" x1="47.5" x2="47.5" y1="321" y2="371"
	 * id="svg_109"></line>
	 */

	NodeList borders;
	TableCell* cell = getCell(c, r);
	if (cell->bottomLine == nullptr) return nullptr;

	SPCSSAttr* cssStyle = adjustStroke(cell->bottomLine);

	Inkscape::XML::Node* borderNode = builder->createElement("svg:line");
	std::string classOfBorder("table-border table-border-h index-r-" +
			std::to_string(r + 1) +
			" index-c-" +
			// Bug 10
			//"  table-border-editor");
			std::to_string(c));
	//borderNode->setAttribute("style", style);
	sp_repr_css_set(borderNode, cssStyle, "style");
	delete(cssStyle);
	borderNode->setAttribute("class", classOfBorder.c_str());

	borderNode->setAttribute("x1", doubleToCss(cell->x).c_str());
	borderNode->setAttribute("y1", doubleToCss(cell->y).c_str());
	borderNode->setAttribute("x2", doubleToCss(cell->x + cell->width).c_str());
	borderNode->setAttribute("y2", doubleToCss(cell->y).c_str());

	return borderNode;
}

bool TableDefenition::isHidCell(int c, int r)
{
	TableCell* cell = getCell(c, r);
	return ((! cell->isMax) && cell->mergeIdx > 0);
}

Inkscape::XML::Node* TableDefenition::getLeftBorder(SvgBuilder *builder, int c, int r, Geom::Affine aff)
{
	NodeList borders;
	TableCell* cell = getCell(c, r);
	if (cell->leftLine == nullptr) return nullptr;

	SPCSSAttr* cssStyle = adjustStroke(cell->leftLine);

	Inkscape::XML::Node* borderNode = builder->createElement("svg:line");
	std::string classOfBorder("table-border table-border-v index-r-" +
			std::to_string(r) +
			" index-c-" +
			// Bug 10
			//"  table-border-editor");
			std::to_string(c));
	//borderNode->setAttribute("style", style);
	sp_repr_css_set(borderNode, cssStyle, "style");
	delete(cssStyle);

	borderNode->setAttribute("class", classOfBorder.c_str());

	borderNode->setAttribute("x1", doubleToCss(cell->x).c_str());
	borderNode->setAttribute("y2", doubleToCss(cell->y).c_str());
	borderNode->setAttribute("x2", doubleToCss(cell->x).c_str());
	borderNode->setAttribute("y1", doubleToCss(cell->height + cell->y).c_str());

	return borderNode;
}

Inkscape::XML::Node* TableDefenition::getRightBorder(SvgBuilder *builder, int c, int r, Geom::Affine aff)
{
	/*<line class="table-border table-border-v index-r-0 index-c-0 table-border-editor"
	 * stroke="#f40101" stroke-dasharray="0 0" stroke-linecap="round" stroke-width="1" x1="47.5" x2="47.5" y1="321" y2="371"
	 * id="svg_109"></line>
	 */

	NodeList borders;
	TableCell* cell = getCell(c, r);
	if (cell->rightLine == nullptr) return nullptr;

	SPCSSAttr* cssStyle = adjustStroke(cell->rightLine);

	Inkscape::XML::Node* borderNode = builder->createElement("svg:line");
	std::string classOfBorder("table-border table-border-v index-r-" +
			std::to_string(r) +
			" index-c-" +
			// Bug 10
			//"  table-border-editor");
			std::to_string(c + 1));
	//borderNode->setAttribute("style", style);
	sp_repr_css_set(borderNode, cssStyle, "style");
	delete(cssStyle);

	borderNode->setAttribute("class", classOfBorder.c_str());

	borderNode->setAttribute("x1", doubleToCss(cell->x + cell->width).c_str());
	borderNode->setAttribute("y2", doubleToCss(cell->y).c_str());
	borderNode->setAttribute("x2", doubleToCss(cell->x + cell->width).c_str());
	borderNode->setAttribute("y1", doubleToCss(cell->height + cell->y).c_str());

	return borderNode;
}

Inkscape::XML::Node* TableDefenition::cellRender(SvgBuilder *builder, int c, int r, Geom::Affine aff)
{
	TableCell* cell = getCell(c, r);

    //printf("+++++++++++++++++++++++++++++++++++++++++++++++++++++++\n");
	//printf("cell: r = %d c = %d x = %f y = %f h = %f w = %f\n", r, c, cell->x, cell->y, cell->height, cell->width);

	Inkscape::XML::Node* nodeCellIdx = builder->createElement("svg:g");
	std::string classForNodeIdx("table-cell index-r-" + std::to_string(r) + " index-c-" + std::to_string(c));
	if (cell->mergeIdx > 0) {
		classForNodeIdx.append(" m-" + std::to_string(cell->mergeIdx));
		if (cell->isMax) {
			classForNodeIdx.append(" table-cell-max" );
		} else {
			classForNodeIdx.append(" table-cell-min" );
		}
	}
	nodeCellIdx->setAttribute("class", classForNodeIdx.c_str());

	Inkscape::XML::Node* nodeTextAttribs = builder->createElement("svg:g");
	// Bug 5
	//std::string classForNodeTextAttribs("textarea subelement active original-font-size-24");
	std::string classForNodeTextAttribs("textarea subelement active");
	nodeTextAttribs->setAttribute("class", classForNodeTextAttribs.c_str());

	Inkscape::XML::Node* nodeCellRect = builder->createElement("svg:rect");
	nodeCellRect->setAttribute("x", doubleToCss(cell->x).c_str());
	nodeCellRect->setAttribute("y", doubleToCss(cell->y).c_str());
	nodeCellRect->setAttribute("width", doubleToCss(cell->width).c_str());
	nodeCellRect->setAttribute("height", doubleToCss(cell->height).c_str());
	nodeCellRect->setAttribute("fill", "none");
	SPDocument* spDoc = builder->getSpDocument();
	if (cell->rect != nullptr && cell->rect->node != nullptr)
	{

		SPItem* spNode = (SPItem*) spDoc->getObjectByRepr(cell->rect->node);
		const char* fillStyle = spNode->getStyleProperty("fill", "none");
		nodeCellRect->setAttribute("fill", fillStyle);
	}

	// Bug 6
	//nodeCellRect->setAttribute("stroke-width", "1");
	//nodeCellRect->setAttribute("stroke", "blue");

	nodeTextAttribs->appendChild(nodeCellRect);

	//printf("[%d,%d] (x=%f, y=%f, w=%f, h=%f) #lines = %d\n", r, c, cell->x, cell->y, cell->width, cell->height, nLinesInCell);

	std::vector<SvgTextPosition> textInAreaList;
	if (!(cell->mergeIdx > 0 && cell->isMax == false))
	{
		textInAreaList = builder->getTextInArea(cell->x, cell->y, cell->x + cell->width, cell->y + cell->height);
	}
	// Even if the cell doesnt contain any text,
	// We need to add <g class="text"><text></text></g>
	// TODO: reverify this, we're setting the text only in the TOP LEFT cell for now.
	if (textInAreaList.size() == 0 || cell->mergeIdx > 0 && cell->isMax == false) {
		Inkscape::XML::Node* stringNode = builder->createTextNode("");
		Inkscape::XML::Node* tSpanNode = builder->createElement("svg:tspan");
		tSpanNode->setAttribute("x", doubleToCss(cell->x));
		tSpanNode->setAttribute("y", doubleToCss(cell->y +1));

		Inkscape::XML::Node* tTextNode = builder->createElement("svg:text");
		tTextNode->setAttribute("style", "fill:none;font-size:1px");

		tSpanNode->appendChild(stringNode);
		tTextNode->appendChild(tSpanNode);
		nodeTextAttribs->addChild(tTextNode, nodeCellRect);
		nodeTextAttribs->setAttribute("font-size", "1");
	} else {
		size_t nLinesInCell = textInAreaList.size();
		for (int idxList = 0; idxList < nLinesInCell; idxList++)
		{
			Inkscape::XML::Node* newTextNode = textInAreaList[idxList].ptextNode->parent()->duplicate(spDoc->getReprDoc());
			newTextNode->setAttribute("transform", sp_svg_transform_write(textInAreaList[idxList].affine));
			Inkscape::XML::Node* child = newTextNode->firstChild();
			while(child)
			{
				Inkscape::XML::Node* nextChild = child->next();
				child->parent()->removeChild(child);
				child = nextChild;
			}
			textInAreaList[idxList].ptextNode->parent()->removeChild(textInAreaList[idxList].ptextNode);

			// create new representation
			newTextNode->appendChild(textInAreaList[idxList].ptextNode);
			nodeTextAttribs->addChild(newTextNode, nodeCellRect);
		}
	}

	nodeCellIdx->appendChild(nodeTextAttribs);

	return nodeCellIdx;
}

Inkscape::XML::Node* TableDefenition::render(SvgBuilder *builder, Geom::Affine aff)
{
	Inkscape::XML::Node* nodeTable = builder->createElement("svg:g");
	nodeTable->setAttribute("class", "table");

	Inkscape::XML::Node* nodeTableRect = builder->createElement("svg:rect");
	nodeTableRect->setAttribute("x", doubleToCss(x).c_str());
	nodeTableRect->setAttribute("y", doubleToCss(y).c_str());
	nodeTableRect->setAttribute("width", doubleToCss(width).c_str());
	nodeTableRect->setAttribute("height", doubleToCss(height).c_str());
	nodeTableRect->setAttribute("fill", "none");
	// g.textarea rect must set fill="none" or (for example) fill="#fff000".
	// Currently it uses inline style which is not correct (the editor doesn't allow this rect have style)
	//nodeTableRect->setAttribute("stroke-width", "0");
	nodeTable->appendChild(nodeTableRect);

	Inkscape::XML::Node* nodeBorders = builder->createElement("svg:g");
	nodeBorders->setAttribute("class", "table-borders");

	for (int rowIdx = 0; rowIdx < countRow; rowIdx++)
	{
		Inkscape::XML::Node* nodeRow = builder->createElement("svg:g");
		std::string rowCalsses("table-row index-"); rowCalsses.append(std::to_string(rowIdx));
		nodeRow->setAttribute("class", rowCalsses.c_str());

		nodeTable->appendChild(nodeRow);

		for (int colIdx = 0; colIdx < countCol; colIdx++)
		{
			TableCell* cell = getCell(colIdx, rowIdx);
			nodeRow->appendChild(cellRender(builder, colIdx, rowIdx, aff));
			Inkscape::XML::Node* leftLine = getLeftBorder(builder, colIdx, rowIdx, aff);
			if (leftLine && (! isHidCell(colIdx, rowIdx)))
				nodeBorders->appendChild(leftLine);

			Inkscape::XML::Node* topLine = getTopBorder(builder, colIdx, rowIdx, aff);
			if (topLine && (! isHidCell(colIdx, rowIdx)))
				nodeBorders->appendChild(topLine);

			if (rowIdx == (countRow - 1))
			{
				Inkscape::XML::Node* bottomLine = nullptr;
				if (isHidCell(colIdx, rowIdx))
				{
					if (colIdx == cell->maxCol)
						bottomLine = getBottomBorder(builder, cell->maxCol, cell->maxRow, aff);
				} else {
					bottomLine = getBottomBorder(builder, colIdx, rowIdx, aff);
				}
				if (bottomLine)
					nodeBorders->appendChild(bottomLine);
			}

			if (colIdx == (countCol- 1))
			{
				Inkscape::XML::Node* rightLine = nullptr;
				if (isHidCell(colIdx, rowIdx))
				{
					if (rowIdx == cell->maxRow)
							rightLine = getRightBorder(builder, cell->maxCol, cell->maxRow, aff);
				} else {
					rightLine = getRightBorder(builder, colIdx, rowIdx, aff);
				}
				if (rightLine)
					nodeBorders->appendChild(rightLine);
			}
		}
	}

	nodeTable->appendChild(nodeBorders);
	builder->removeNodesByTextPositionList();

	return nodeTable;
}

TableCell* TableDefenition::getCell(int xIdx, int yIdx)
{
	return &_cells[yIdx * countCol + xIdx];
}

void TableDefenition::setStroke(int xIdx, int yIdx, TabLine *topLine, TabLine *bottomLine, TabLine *leftLine, TabLine *rightLine)
{
	TableCell *cell = getCell(xIdx, yIdx);
	cell->topLine = topLine;
	cell->bottomLine = bottomLine;
	cell->leftLine = leftLine;
	cell->rightLine = rightLine;
}

void TableDefenition::setVertex(int xIdx, int yIdx, double xStart, double yStart, double xEnd, double yEnd)
{
	TableCell *cell = getCell(xIdx, yIdx);
	cell->x = xStart;
	cell->width = xEnd - xStart;
	cell->y = yStart;
	cell->height = yEnd - yStart;
}

static bool tableRowsSorter(const double &a, const double &b)
{
	return a > b;
}

struct CellCoord {
	int col, row;
	CellCoord(int c, int r) :
		col(c),
		row(r)
	{
	}
};

struct SkipCells {
	std::vector<CellCoord> list;

	void addRect(int col1, int row1, int col2, int row2, bool ignoreFirst = false)
	{
		for(int colIdx = col1; colIdx <= col2; colIdx++)
		{
			for(int rowIdx = row1; rowIdx <= row2; rowIdx++)
			{
				if (ignoreFirst && colIdx == col1 && rowIdx == row1) continue;
				list.push_back(CellCoord(colIdx, rowIdx));
			}
		}
	}

	bool isExist(int col, int row)
	{
		for(auto& cell : list)
		{
			if ( cell.col == col && cell.row == row ) return true;
		}
		return false;
	}
};

void TableDefenition::setRect(int col, int row, TabRect* rect)
{
	TableCell* cell = getCell(col, row);
	cell->rect = rect;
}

void TableDefenition::setMergeIdx(int col1, int row1, int mergeIdx)
{
	TableCell* cell = getCell(col1, row1);
	cell->mergeIdx = mergeIdx;
}

void TableDefenition::setMergeIdx(int col1, int row1, int col2, int row2, int mergeIdx)
{
	TableCell* cellMax = getCell(col1, row1);
	for(int colIdx = col1; colIdx <= col2; colIdx++)
	{
		for(int rowIdx = row1; rowIdx <= row2; rowIdx++)
		{
			TableCell* cell = getCell(colIdx, rowIdx);
			if (colIdx == col1 && rowIdx == row1)
			{
				cell->isMax = true;
			} else
			{
				cell->maxCol = col1;
				cell->maxRow = row1;
				cell->cellMax = cellMax;
				cell->isMax = false;
			}
			cell->mergeIdx = mergeIdx;
		}
	}
}

bool predAproxUniq(const float &a, const float &b)
{
	return approxEqual(a, b, 0.5); //minimal cell size/resolution.
}

bool TableRegion::buildKnote(SvgBuilder *builder)
{
	std::vector<double> xList;
	std::vector<double> yList;

	SPDocument* spDoc = builder->getSpDocument();
	SPObject* spMainNode = spDoc->getObjectByRepr( builder->getMainNode() );
	SkipCells skipCell;

// calculate simple grid
	for(auto& line : this->lines)
	{
		SPPath* spLine = (SPPath*)spDoc->getObjectByRepr(line->node);
		Geom::Affine lineAffine = spLine->getRelativeTransform(spMainNode);
		Geom::Point firstPoint(line->x1, line->y1);
		Geom::Point secondPoint(line->x2, line->y2);

		firstPoint = firstPoint * lineAffine;
		secondPoint = secondPoint * lineAffine;

		line->x1 = firstPoint.x() < secondPoint.x() ? firstPoint.x() : secondPoint.x();
		line->y1 = firstPoint.y() < secondPoint.y() ? firstPoint.y() : secondPoint.y();
		line->x2 = firstPoint.x() > secondPoint.x() ? firstPoint.x() : secondPoint.x();
		line->y2 = firstPoint.y() > secondPoint.y() ? firstPoint.y() : secondPoint.y();

		if (approxEqual(line->x1, line->x2))
		    xList.push_back(line->x1);

        if (approxEqual(line->y1, line->y2))
	    	yList.push_back(line->y1);

	}

	if (xList.empty() || yList.empty()) return false;

	std::sort(xList.begin(), xList.end());
	std::sort(yList.begin(), yList.end(), &tableRowsSorter); // invert sort order

	if (! approxEqual(this->x1, xList.front(), 5))
		xList.push_back(this->x1);
	if (! approxEqual(this->x2, xList.back(), 5))
		xList.push_back(this->x2);

	if (! approxEqual(this->y1, yList.back(), 5))
		yList.push_back(this->y1);
	if (! approxEqual(this->y2, yList.front(), 5))
		yList.push_back(this->y2);
	std::sort(xList.begin(), xList.end());
	std::sort(yList.begin(), yList.end(), &tableRowsSorter); // invert sort order

	auto lastX = std::unique(xList.begin(), xList.end(), predAproxUniq);
	auto lastY = std::unique(yList.begin(), yList.end(), predAproxUniq);
	xList.erase(lastX, xList.end());
	yList.erase(lastY, yList.end());

	if (((yList.size() -1) * (xList.size() - 1)) < 4 )
		return false;

/*
	for(auto& xPos : xList)
	{
		printf("%f ", xPos);
	}
	printf("\n");

	for(auto& yPos : yList)
	{
		printf("%f ", yPos);
	}
	printf("\n");
*/
	tableDef = new TableDefenition(xList.size() -1 , yList.size() -1);
	double xStart, yStart, xEnd, yEnd;
	xStart = xList[0];
	yStart = yList[0];


// set table size
	tableDef->x = xList[0];
	tableDef->y = yList[yList.size() - 1];
	tableDef->width = xList[xList.size() - 1] - xStart;
	tableDef->height = yStart - yList[yList.size() - 1];

	std::vector<SvgTextPosition> textList = _builder->getTextInArea(tableDef->x, tableDef->y,
			tableDef->x + tableDef->width, tableDef->y + tableDef->height, true);

	if (textList.empty()) return false;

	for(int yIdx = 1; yIdx < yList.size() ; yIdx++)
	{
		xStart = xList[0];
		for(int xIdx = 1; xIdx < xList.size() ; xIdx++)
		{
			double xMediane = (xList[xIdx] - xStart)/2 + xStart;
			double yMediane = (yStart - yList[yIdx])/2 + yList[yIdx];
			TabLine* topLine = searchByPoint(xMediane, yStart, false);
			TabLine* leftLine = searchByPoint(xStart, yMediane, true);

			TabLine* rightLine = searchByPoint(xList[xIdx], yMediane, true);
			int xShift = 0;
			while(rightLine == nullptr && (xIdx + 1 + xShift) < xList.size())
			{
				xShift++;
				rightLine = searchByPoint(xList[xIdx + xShift], yMediane, true);
			}

			TabLine* bottomLine = searchByPoint(xMediane, yList[yIdx], false);
			int yShift = 0;
			while(bottomLine == nullptr && (yIdx + 1 + yShift) < yList.size())
			{
				yShift++;
				bottomLine = searchByPoint(xMediane, yList[yIdx + yShift], false);
			}

			if (skipCell.isExist(xIdx - 1, yIdx - 1))
			{
				tableDef->setStroke(xIdx - 1, yIdx - 1, topLine, bottomLine, leftLine, rightLine);
				tableDef->setVertex(xIdx - 1, yIdx - 1, xStart, yList[yIdx], xList[xIdx], yStart);
				xStart = xList[xIdx];
				tableDef->setRect(xIdx -1, yIdx -1, nullptr);

				continue;
			} else {
				tableDef->setStroke(xIdx - 1, yIdx - 1, topLine, bottomLine, leftLine, rightLine);
				tableDef->setVertex(xIdx - 1, yIdx - 1, xStart, yList[yIdx + yShift], xList[xIdx + xShift], yStart);
			}

			tableDef->setMergeIdx(xIdx - 1, yIdx - 1, -1);

			static int mergeIdx = 0;
			mergeIdx++;

			if (xShift >0 || yShift >0)
			{
				tableDef->setMergeIdx(xIdx -1, yIdx -1, xIdx + xShift -1, yIdx + yShift -1, mergeIdx);
				skipCell.addRect(xIdx -1, yIdx -1, xIdx + xShift -1, yIdx + yShift -1, true);
			}

			TabRect* rect = matchRect(xStart, yList[yIdx], xList[xIdx], yStart);
			tableDef->setRect(xIdx -1, yIdx -1, rect);

			xStart = xList[xIdx];
		}
		yStart = yList[yIdx];
	}
	return true;
}

Inkscape::XML::Node* TableRegion::render(SvgBuilder *builder, Geom::Affine aff)
{
	Inkscape::XML::Node* result = tableDef->render(builder, aff);
	NodeList deleteNode;
	for(auto line : lines)
	{
		deleteNode.push_back(line->node);
		//line->node->parent()->removeChild(line->node);
		//delete(line->node);
	}

	std::sort(deleteNode.begin(), deleteNode.end());
	auto lastNode = std::unique(deleteNode.begin(), deleteNode.end());
	deleteNode.erase(lastNode, deleteNode.end());

	for(auto& node : deleteNode)
	{
		node->parent()->removeChild(node);
		delete(node);
	}

	return result;
}


Geom::Rect TableRegion::getBBox()
{
	return Geom::Rect(x1, y1, x2, y2);
}

double TableRegion::getAreaSize()
{
	return std::fabs((x1 - x2) * (y1 - y2));
}

bool TabLine::intersectRect(Geom::Rect rect)
{
	Geom::Rect line(Geom::Point(x1, y1) * affineToMainNode,
			Geom::Point(x2, y2) * affineToMainNode);
	return line.intersects(rect);

}

bool TableRegion::recIntersectLine(Geom::Rect rect)
{
	for(auto& line : lines)
	{
		if (line->intersectRect(rect))
			return true;
	}
	return false;
}

bool TableRegion::checkTableLimits()
{
	int freeLines = lines.size() - rects.size() * 4;
	if (freeLines < 2 && rects.size() < 4)
		return false;

	return true;
}

/**
 * @describe do merge tags without text between
 * @param builder representation of SVG document
 * @param simulate if true - do not change source document (default false)
 *
 * @return count of generated images
 * */
TableList* detectTables(SvgBuilder *builder, TableList* tables) {
  bool splitRegions = true;


	Inkscape::XML::Node *root = builder->getRoot();

	std::vector<std::string> tags;
	tags.push_back("svg:path");
	tags.push_back("svg:rect");
	tags.push_back("svg:line");

	auto regions = builder->getRegions(tags);
	for(NodeList& vecRegionNodes : *regions)
	{
		TableRegion* tabRegionStat= new TableRegion(builder);
		bool isTable = true;
		if (vecRegionNodes.size() < 2) continue;
		//printf("==============region===========\n");
		for(Inkscape::XML::Node* node: vecRegionNodes)
		{
			isTable = tabRegionStat->addLine(node);
			//printf("node id = %s\n", node->attribute("id"));
			if (! isTable)
				break;
		}

		if (isTable)
			isTable = tabRegionStat->checkTableLimits();
			//isTable = tabRegionStat->hasHorizontalLine();

		if (isTable)
		{
			//if table region contain image - exclude it
			Geom::Rect tabBBox = tabRegionStat->getBBox();
			NodeList imgList;
			builder->getNodeListByTag("svg:image", &imgList, builder->getMainNode());
			for(auto& imageNode : imgList)
			{
				Geom::Rect imgRect = builder->getNodeBBox(imageNode);

				if (rectIntersect(imgRect, tabBBox) > 0)
				{
					if (tabRegionStat->recIntersectLine(imgRect))
					{
						isTable = false;
						break;
					}
				}
			}
		}

		if (isTable) tables->push_back(tabRegionStat);
	}

	return tables;
}

TableRegion::~TableRegion()
{
	if (tableDef)
		delete(tableDef);
	for(auto& line : lines)
	{
		delete line;
	}
}

bool TableRegion::addLine(Inkscape::XML::Node* node)
{
	//printf("node id =%s\n", node->attribute("id"));

	SPPath* spPath = (SPPath*)spDoc->getObjectByRepr(node);
	SPCurve* curve = spPath->getCurve();
	SPObject* spMainNode = spDoc->getObjectByRepr(_builder->getMainNode() );
	Geom::Affine pathAffine = spPath->getRelativeTransform(spMainNode);


	const Geom::PathVector pathArray = curve->get_pathvector();

	for (const Geom::Path& itPath : pathArray)
	{
		double _x1 = 1e6;
		double _y1 = 1e6;
		double _x2 = 0;
		double _y2 = 0;
		for(const Geom::Curve& simplCurve : itPath) {
			TabLine* line = new TabLine(node, simplCurve, spDoc);
			line->affineToMainNode = pathAffine;
			lines.push_back(line);
			if (! line->isTableLine())
			{
				_isTable = false;
			}

			_x1 = _x1 < line->x1 ? _x1 : line->x1;
			_y1 = _y1 < line->y1 ? _y1 : line->y1;
			_x2 = _x2 > line->x2 ? _x2 : line->x2;
			_y2 = _y2 > line->y2 ? _y2 : line->y2;
		}
		if (! _isTable) continue;

		Geom::Point point1(_x1, _y1);
		Geom::Point point2(_x2, _y2);
		point1 = point1 * pathAffine;
		point2 = point2 * pathAffine;
		this->x1 = this->x1 < point1[Geom::X] ? this->x1 : point1[Geom::X];
		this->y1 = this->y1 < point1[Geom::Y] ? this->y1 : point1[Geom::Y];
		this->x2 = this->x2 > point2[Geom::X] ? this->x2 : point2[Geom::X];
		this->y2 = this->y2 > point2[Geom::Y] ? this->y2 : point2[Geom::Y];

		if (itPath.size() == 4 && itPath.closed())
		{
			TabRect* rect = new TabRect(point1, point2, node);
			rects.push_back(rect);
		}
	}

	return isTable();
}

/**
 * @describe do merge tags without text between
 * @param builder representation of SVG document
 * @param simulate if true - do not change source document (default false)
 *
 * @return count of generated images
 * */

uint mergeImagePathToLayerSave(SvgBuilder *builder, bool splitRegions, bool simulate, uint* regionsCount) {
  //================== merge paths and images ==================
  sp_merge_images_sh = (sp_merge_limit_sh > 0) &&
			 (sp_merge_limit_sh <= builder->getCountOfImages());
  sp_merge_path_sh = (sp_merge_limit_path_sh > 0) &&
			 (sp_merge_limit_path_sh <= builder->getCountOfPath());
  uint img_count = 0;

  // if we have number of paths or images more than "limit" we should merge it to image
  if (sp_merge_images_sh || sp_merge_path_sh) {
	Inkscape::XML::Node *root = builder->getRoot();
	//Inkscape::XML::Node *remNode;
	Inkscape::XML::Node *toImageNode;
	gchar *tmpName;

	Inkscape::Extension::Internal::MergeBuilder *mergeBuilder;

	int countMergedNodes = 0;
	//find image nodes
	mergeBuilder = new Inkscape::Extension::Internal::MergeBuilder(root, sp_export_svg_path_sh);

	// if number of images more than limit - add <image> to merge treating
	if (sp_merge_images_sh) {
	  mergeBuilder->addTagName(g_strdup_printf("%s", "svg:image"));
	  if (! simulate ) warning3tooManyImages = TRUE;
	}

	// if number of paths more than limit - add <path> and <image> to merge treating
	if (sp_merge_path_sh) {
      mergeBuilder->addTagName(g_strdup_printf("svg:path"));
      if (sp_rect_how_path_sh) {
    	  mergeBuilder->addTagName(g_strdup_printf("svg:rect"));
      }
      if (! simulate ) warning2wasRasterized = TRUE;
	}
	int numberInNode = 0;
	Inkscape::XML::Node *mergeNode = mergeBuilder->findFirstNode(&numberInNode);
	Inkscape::XML::Node *visualNode;
	std::vector<Inkscape::XML::Node *> remNodes;
	if (mergeNode) visualNode = mergeNode->parent();

	while(mergeNode) {
		countMergedNodes = numberInNode - 1;
		mergeBuilder->clearMerge();
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
			}
			mergeBuilder->addImageNode(mergeNode, sp_export_svg_path_sh);
			remNodes.push_back(mergeNode);

			std::vector<Geom::Rect> regions;
			if (splitRegions)
			{
				regions = mergeBuilder->getRegions();
				if (regionsCount)
					*regionsCount += regions.size();
			}

			if (! simulate) {

				// Insert node with merged image
				double resultDpi;

				if (splitRegions)
				{
					Inkscape::XML::Node *tmpMergedNode = mergeNode;
					int counter = 0;
					for(Geom::Rect rect : regions)
					{
						counter++;
						tmpName = g_strdup_printf("%s_img%i%s", builder->getDocName(), counter, mergeNode->attribute("id"));
						Inkscape::XML::Node *sumNode = mergeBuilder->saveImage(tmpName, builder, true, resultDpi, &rect);
						visualNode->addChild(sumNode, tmpMergedNode);
						tmpMergedNode = sumNode;
					}
					mergeNode = tmpMergedNode->next();
				} else
				{
					tmpName = g_strdup_printf("%s_img%s", builder->getDocName(), mergeNode->attribute("id"));
					Inkscape::XML::Node *sumNode = mergeBuilder->saveImage(tmpName, builder, true, resultDpi);
					visualNode->addChild(sumNode, mergeNode);
					mergeNode = sumNode->next();
				}

				for(int i = 0; i < remNodes.size(); i++) {
					mergeBuilder->removeRelateDefNodes(remNodes[i]);
					mergeBuilder->removeGFromNode(remNodes[i]);
				}
				remNodes.clear();

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
void mergeImagePathToOneLayer(SvgBuilder *builder, ApproveNode* approve) {
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

			if ((approve != nullptr && !approve(mergeNode)) || (! mergeBuilder->haveTagFormList(mergeNode))) {
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
			double resultDpi;
			visualNode->addChild(mergeBuilder->saveImage(fName, builder, true, resultDpi), NULL);
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
			  double resultDpi;
			  Inkscape::XML::Node *sumNode = mergeBuilder->saveImage(fName, builder, sp_adjust_mask_size_sh, resultDpi);
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

		  if (remNodes.size() > 1)
		      warning2wasRasterized = TRUE;

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
		  mergeBuilder->addAttrName(g_strdup_printf("%s", "fill"));
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
				double resultDpi;
				Inkscape::XML::Node *sumNode = mergeBuilder->saveImage(fName, builder, true, resultDpi);
				visualNode->addChild(sumNode, mergeNode);
				mergeBuilder->clearMerge();
				mergeNode = sumNode->next();
				free(fName);
			} else {
				mergeNode = mergeNode->next();
			}

			mergeNode = mergeBuilder->findNextAttrNode(mergeNode);
		  }

		  if (remNodes.size() > 1)
		      warning2wasRasterized = TRUE;

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
	if (node == NULL ) return;
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

bool objStreamToFile(Object* obj, const char* fileName)
{
	  if (obj->isStream())
	  {
		  Stream* str = obj->getStream();
		  GooString gooStr;
		  str->fillGooString(&gooStr);
		  int length = gooStr.getLength();
		  FILE* strFile = fopen(fileName, "w");
		  fwrite(gooStr.getCString(), length, 1, strFile);
		  fclose(strFile);
		  return true;
	  }
	  return false;
}

bool rectHasCommonEdgePoint(Geom::Rect rect1, Geom::Rect rect2)
{
	std::vector<Geom::Rect> lines1;
	std::vector<Geom::Rect> lines2;

	//top line of first rectangle
	lines1.push_back(Geom::Rect(rect1.top(), rect1.right()+1, rect1.top(), rect1.left()-1));

	//bottom line of first rectangle
	lines1.push_back(Geom::Rect(rect1.bottom(), rect1.right()+1, rect1.bottom(), rect1.left()-1));

	//left line of first rectangle
	lines1.push_back(Geom::Rect(rect1.top()-1, rect1.left(), rect1.bottom()+1, rect1.left()));

	//right line of first rectangle
	lines1.push_back(Geom::Rect(rect1.top()-1, rect1.right(), rect1.bottom()+1, rect1.right()));

	//top line of second rectangle
	lines2.push_back(Geom::Rect(rect2.top(), rect2.right()+1, rect2.top(), rect2.left()-1));

	//bottom line of second rectangle
	lines2.push_back(Geom::Rect(rect2.bottom(), rect2.right()+1, rect2.bottom(), rect2.left()-1));

	//left line of second rectangle
	lines2.push_back(Geom::Rect(rect2.top()-1, rect2.left(), rect2.bottom()+1, rect2.left()));

	//right line of second rectangle
	lines2.push_back(Geom::Rect(rect2.top()-1, rect2.right(), rect2.bottom()+1, rect2.right()));

	for (auto& line1 : lines1)
	{
		for (auto& line2 : lines2)
		{
			if (line1.intersects(line2)) return true;

		}
	}

	return false;
}

bool approxEqual(const float x, const float y, const float epsilon)
{
   return (std::fabs(x - y) < epsilon);
}

inline bool definitelyBigger(const float a, const float b, const float epsilon)
{
   return ((a - epsilon) > b);
}


