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

Inkscape::XML::Node *MergeBuilder::findFirstAttrNodeWithPattern(void) {
	return findAttrNodeWithPattern(_sourceSubVisual->firstChild());
}

Inkscape::XML::Node *MergeBuilder::findNextAttrNodeWithPattern(Inkscape::XML::Node *node) {
	return findAttrNodeWithPattern(node);
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

Inkscape::XML::Node *MergeBuilder::findAttrNodeWithPattern(Inkscape::XML::Node *node) {
	Inkscape::XML::Node *tmpNode;
	Inkscape::XML::Node *resNode = NULL;
	tmpNode = node;
	while(tmpNode) {
		if (haveTagAttrPattern(tmpNode)) {
			return tmpNode;
		}
		tmpNode = tmpNode->next();
	}
	return resNode;
}

Inkscape::XML::Node *MergeBuilder::findAttrNode(Inkscape::XML::Node *node) {
	Inkscape::XML::Node *tmpNode;
	Inkscape::XML::Node *resNode = NULL;
	tmpNode = node;
	while(tmpNode) {
		if (haveTagAttrFormList(tmpNode) || haveTagAttrPattern(tmpNode)) {
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
								  // Don't merge gradients with pattern
								  /*(strstr(styleValue, "url(#pattern") > 0) ||*/
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

bool MergeBuilder::haveTagAttrPattern(Inkscape::XML::Node *node) {
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
						additionCond = (strstr(styleValue, "url(#pattern") > 0);
					  }
					  if (strcmp(attrName, "fill") == 0) {
						  additionCond = (strstr("url(#pattern", styleValue) > 0);
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
	  }


	  return attr;
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
		if (tmpNode->attribute("id") && strcmp(tmpNode->attribute("id"), id) == 0) {
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

void NodeState::initGeometry(SPDocument *spDoc)
{
	spNode = (SPItem*)spDoc->getObjectByRepr(node);

	Geom::OptRect visualBound(spNode->visualBounds());
	if (visualBound.is_initialized())
	{
		sqBBox = visualBound.get();
		Geom::Affine nodeAffine = spNode->getRelativeTransform(spDoc->getRoot());
		sqBBox = sqBBox * nodeAffine;
	}
}

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

Inkscape::XML::Node *MergeBuilder::copyAsChild(Inkscape::XML::Node *destNode, Inkscape::XML::Node *childNode, char *rebasePath, Inkscape::XML::Document *doc) {
	if (doc == nullptr) doc = _xml_doc;
	Inkscape::XML::Node *tempNode = doc->createElement(childNode->name());
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
		double firstX;
		double firstEndX;
		double secondX;
		double secondEndX;
		double spaceSize;
		Inkscape::XML::Node *tspan1 = (Inkscape::XML::Node *)g_ptr_array_index(tspanArray, i);
		Inkscape::XML::Node *tspan2 = (Inkscape::XML::Node *)g_ptr_array_index(tspanArray, i + 1);
		sp_repr_get_double(tspan1, "y", &firstY);
		sp_repr_get_double(tspan2, "y", &secondY);
		sp_repr_get_double(tspan1, "x", &firstX);

		const char* tspan1Content = tspan1->firstChild()->content();
		const char* tspan2Content = tspan2->firstChild()->content();
		double lenTspan1 = strlen(tspan1Content);
		double lenTspan2 = strlen(tspan2Content);

		if (! sp_repr_get_double(tspan1, "data-endX", &firstEndX)) firstEndX = 0;
		if (! sp_repr_get_double(tspan2, "x", &secondX)) secondX = 0;
		if (! sp_repr_get_double(tspan2, "data-endX", &secondEndX)) secondEndX = 0;
		if (! sp_repr_get_double(tspan1, "sodipodi:spaceWidth", &spaceSize)) spaceSize = 0;
		const char* align1 = tspan1->attribute("data-align");
		const char* align2 = tspan2->attribute("data-align");
		if ( spaceSize <= 0 ) {
			spaceSize = textSize / 3;
		}

		int nbrSpacesEndTspan1 = 0;
		// Calculate number of spaces in the end of tspan1
		for (int idx = strlen(tspan1Content) - 1; idx > 0 ; idx--) {
				if (tspan1Content[idx] == ' ')
						nbrSpacesEndTspan1++;
				else
						break;
		}

		/**
		 * Following this forum: http://www.magazinedesigning.com/columns-pt-2-line-lengths-and-column-width/
		 * We will check if this is a multi-column text, for example if textSize = 10pt, textWidth < 240pt and gap > 18pt ==> Do not group
		 * In other words, if textWidth / 24 < textSize && gap/1.8 > textSize, we will not merge the 2 tspans.
		 */

		// get the width of the smallest between the 2 tspans:
		double minTspanWidth = std::min(firstEndX - firstX, secondEndX - secondX);
		double gapX = secondX - firstEndX;
		double gapXWithoutSpaces = gapX + (nbrSpacesEndTspan1 * spaceSize);
		double maxSpaceGap = spaceSize * 3;

		if (textSize == 0) textSize = 0.00001;
		// round Y to 20% of font size and compare
		// if gap more then 3.5 of text size - mind other column
		if (fabs(firstY - secondY)/textSize < 0.2 &&
			// Is this a multi-column text?
			(minTspanWidth/24 >= textSize || gapX/1.8 < textSize) &&
			// litle negative gap
				(fabs(firstEndX - secondX)/textSize < 0.2 || (firstEndX <= secondX)) &&
				(gapXWithoutSpaces < maxSpaceGap) &&
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
		if (! approxEqual(firstAffine[i], secondAffine[i], std::fabs(firstAffine[i]/10)))
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
	mainSpText->updateRepr(2);  // without it we can't compare style direct
	Geom::Affine mainAffine = mainSpText->transform;

	// process for each text node
	for(int i = 1; i < listNodes->len; i++) {
		// load current text node
		Inkscape::XML::Node *currentTextNode = (Inkscape::XML::Node *)g_ptr_array_index(listNodes, i);
		SPItem *currentSpText = (SPItem*)builder->getSpDocument()->getObjectByRepr(currentTextNode);
		currentSpText->updateRepr(2); // without it we can't compare style direct
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

	//remove empty text nodes
	tmpNode = startNode;
	while(tmpNode && (strcmp(tmpNode->name(), "svg:text") == 0)) {
		Inkscape::XML::Node *current = tmpNode;
		tmpNode = tmpNode->next();
		if (current->childCount() == 0) current->parent()->removeChild(current);
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

Inkscape::XML::Node *MergeBuilder::AddClipPathToMyDefs(Inkscape::XML::Node *originalNode, SvgBuilder *builder, char* patternId, gchar *rebasePath) {
	
	SPObject *obj = builder->getSpDocument()->getObjectById(originalNode->attribute("id"));
	Geom::OptRect visualBound(SP_ITEM(obj)->visualBounds());

	Geom::Rect sq = Geom::Rect();
	double x1, x2, y1, y2;

	sq = visualBound.get();
	x1 = sq[Geom::X][0];
	x2 = sq[Geom::X][1];
	y1 = sq[Geom::Y][0];
	y2 = sq[Geom::Y][1];

	char dMatrix[500];
	memset((void*)dMatrix, 0, 500);
	snprintf(dMatrix, 500, "m %d,%d h %d v %d h -%d z", int(x1), int(y1), int(abs(x2 - x1)), int(abs(y2 - y1)), int(abs(x2 - x1)));

	Inkscape::XML::Node * newRect = builder->createElement("svg:path");
	newRect->setAttribute("fill", patternId);
	newRect->setAttribute("d", dMatrix);

	addImageNode(newRect, sp_export_svg_path_sh);

	Inkscape::XML::Node * newClipPath = builder->createElement("svg:clipPath");
	copyAsChild(newClipPath, originalNode, rebasePath, builder->getSpDocument()->getReprDoc());

	Inkscape::XML::Node *defsNode = ((SPObject*) builder->getSpDocument()->getDefs())->getRepr();
	defsNode->appendChild(newClipPath);
	char clipPathCustomeId[50];
	memset((void*)clipPathCustomeId, 0, 50);
	snprintf(clipPathCustomeId, 50, "pattern_%s", newClipPath->attribute("id"));
	newClipPath->setAttribute("id", clipPathCustomeId);
	return newClipPath;
}

char *removePatternFromStyle(char *style, char *pattern) {
    size_t len = strlen(pattern);
    if (len > 0) {
        char *p = style;
        while ((p = strstr(p, pattern)) != NULL) {
            memmove(p, p + len, strlen(p + len) + 1);
        }
    }
    return style;
}

void mergePatternToLayer(SvgBuilder *builder) {
	  //================== merge images with patterns =================
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

		  // merge images with patterns
		  Inkscape::XML::Node *mergeNode = mergeBuilder->findFirstAttrNodeWithPattern();
		  //Inkscape::XML::Node *remNode;
		  Inkscape::XML::Node *visualNode;
		  if (mergeNode) visualNode = mergeNode->parent();
		  const gchar *tmpName;
		  mergeBuilder->clearMerge();
		  while(mergeNode) {

			if (! mergeBuilder->haveTagAttrPattern(mergeNode)) {
				mergeNode = mergeNode->next();
				continue;
			}

			// find text nodes and save it
			moveTextNode(builder, mergeNode);

			Inkscape::XML::Node *clipPathNode = nullptr;
			char patternId[100];
			{
				// check if the pattern itself has a clip path
				char* patternId = (char*)(mergeNode->attribute((char*)"fill"));
				if (patternId == nullptr) {
					// style tag 
					char* styleValue =  (char*)(mergeNode->attribute((char*)"style"));
					if (styleValue && (strstr(styleValue, "url(#pattern") > 0)) {
						const char *grIdStart = strstr(styleValue, "url(#pattern");
						const char *grIdEnd;
						if (grIdStart) {
							grIdEnd = strstr(grIdStart, ")");
							if (grIdStart && grIdEnd) {
								char patternNodeId[100];
								grIdEnd += 1;
								memcpy(patternNodeId, grIdStart, grIdEnd + 1 - grIdStart);
								patternNodeId[grIdEnd - grIdStart] = 0;
								styleValue = removePatternFromStyle(styleValue, patternNodeId);
								mergeNode->setAttribute("style", styleValue);
								
								clipPathNode = mergeBuilder->AddClipPathToMyDefs(mergeNode, builder, patternNodeId, sp_export_svg_path_sh);
							}
						}
					}
				} else {
					mergeNode->setAttribute("fill", nullptr);
					clipPathNode = mergeBuilder->AddClipPathToMyDefs(mergeNode, builder, patternId, sp_export_svg_path_sh);
				}
				
			}

			remNodes.push_back(mergeNode);

			{
				// generate name of new image
				tmpName = mergeNode->attribute("id");
				char *fName = g_strdup_printf("%s_img%s", builder->getDocName(), tmpName);

				// Save merged image
				double resultDpi;
				Inkscape::XML::Node *sumNode = mergeBuilder->saveImage(fName, builder, true, resultDpi);

				Inkscape::XML::Node * newParent = builder->createElement("svg:g");

				if (clipPathNode) {
					char clipPathUrl[50];
					memset((void*)clipPathUrl, 0, 50);
					snprintf(clipPathUrl, 50, "url(#%s)", clipPathNode->attribute("id"));
					newParent->setAttribute("clip-path", clipPathUrl);
				}
				newParent->appendChild(sumNode);
				visualNode->addChild(newParent, mergeNode);
				mergeBuilder->clearMerge();
				mergeNode = newParent->next();
				free(fName);
			}
			mergeNode = mergeBuilder->findNextAttrNodeWithPattern(mergeNode);
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

bool rectHasCommonEdgePoint(Geom::Rect& rect1, Geom::Rect& rect2)
{
	const double firstX1 = rect1.left();
	const double firstY1 = rect1.top();
	const double firstX2 = rect1.right();
	const double firstY2 = rect1.bottom();

	const double secondX1 = rect2.left();
	const double secondY1 = rect2.top();
	const double secondX2 = rect2.right();
	const double secondY2 = rect2.bottom();

	return rectHasCommonEdgePoint(firstX1, firstY1, firstX2, firstY2,
			secondX1, secondY1, secondX2, secondY2);
}

bool rectHasCommonEdgePoint( const double firstX1, const double firstY1, const double firstX2, const double firstY2,
		const double secondX1, const double secondY1, const double secondX2, const double secondY2 )
{
    const double APPROX = 2;

	const bool xApproxEqual = 	approxEqual(firstX1, secondX1, APPROX/2) ||
								approxEqual(firstX1, secondX2, APPROX/2) ||
								approxEqual(firstX2, secondX1, APPROX/2) ||
								approxEqual(firstX2, secondX2, APPROX/2);

	const bool yApproxEqual = 	approxEqual(firstY1, secondY1, APPROX/2) ||
								approxEqual(firstY1, secondY2, APPROX/2) ||
								approxEqual(firstY2, secondY1, APPROX/2) ||
								approxEqual(firstY2, secondY2, APPROX/2);

	const bool xIntersectInterval =
			(firstX1 > (secondX1 -APPROX) && (firstX2 > secondX2 -APPROX) && firstX1 < (secondX2 +APPROX)) ||
			(firstX1 < (secondX1 +APPROX) && (firstX2 < secondX2 +APPROX) && firstX2 > (secondX1 -APPROX));

	const bool yIntersectInterval =
			(firstY1 > (secondY1 -APPROX) && (firstY2 > secondY2 -APPROX) && firstY1 < (secondY2 +APPROX)) ||
			(firstY1 < (secondY1 +APPROX) && (firstY2 < secondY2 +APPROX) && firstY2 > (secondY1 -APPROX));

	const bool xContainInterval =
			(firstX1 < (secondX1 +APPROX) && (firstX2 > secondX2 -APPROX)) ||
			(firstX1 > (secondX1 -APPROX) && (firstX2 < secondX2 +APPROX));

	const bool yContainInterval =
			(firstY1 < (secondY1 +APPROX) && (firstY2 > secondY2 -APPROX)) ||
			(firstY1 > (secondY1 -APPROX) && (firstY2 < secondY2 +APPROX));


	const bool touch =
			(xContainInterval && yApproxEqual) ||
			(yContainInterval && xApproxEqual) ||
			(xIntersectInterval && yIntersectInterval) ||
			(xContainInterval && yIntersectInterval) ||
			(yContainInterval && xIntersectInterval);

	return touch;
}

bool intApproxEqual(const int first, const int second, int epsilon)
{

	return std::abs(first - second) < epsilon;
}

bool rectHasCommonEdgePoint( const int firstX1, const int firstY1, const int firstX2, const int firstY2,
		const int secondX1, const int secondY1, const int secondX2, const int secondY2, const int APPROX)
{
	const bool xApproxEqual = 	intApproxEqual(firstX1, secondX1, APPROX/2) ||
			intApproxEqual(firstX1, secondX2, APPROX/2) ||
			intApproxEqual(firstX2, secondX1, APPROX/2) ||
			intApproxEqual(firstX2, secondX2, APPROX/2);

	const bool yApproxEqual = 	intApproxEqual(firstY1, secondY1, APPROX/2) ||
			intApproxEqual(firstY1, secondY2, APPROX/2) ||
			intApproxEqual(firstY2, secondY1, APPROX/2) ||
			intApproxEqual(firstY2, secondY2, APPROX/2);

	const bool xIntersectInterval =
			(firstX1 > (secondX1 -APPROX) && (firstX2 > secondX2 -APPROX) && firstX1 < (secondX2 +APPROX)) ||
			(firstX1 < (secondX1 +APPROX) && (firstX2 < secondX2 +APPROX) && firstX2 > (secondX1 -APPROX));

	const bool yIntersectInterval =
			(firstY1 > (secondY1 -APPROX) && (firstY2 > secondY2 -APPROX) && firstY1 < (secondY2 +APPROX)) ||
			(firstY1 < (secondY1 +APPROX) && (firstY2 < secondY2 +APPROX) && firstY2 > (secondY1 -APPROX));

	const bool xContainInterval =
			(firstX1 < (secondX1 +APPROX) && (firstX2 > secondX2 -APPROX)) ||
			(firstX1 > (secondX1 -APPROX) && (firstX2 < secondX2 +APPROX));

	const bool yContainInterval =
			(firstY1 < (secondY1 +APPROX) && (firstY2 > secondY2 -APPROX)) ||
			(firstY1 > (secondY1 -APPROX) && (firstY2 < secondY2 +APPROX));


	const bool touch =
			(xContainInterval && yApproxEqual) ||
			(yContainInterval && xApproxEqual) ||
			(xIntersectInterval && yIntersectInterval) ||
			(xContainInterval && yIntersectInterval) ||
			(yContainInterval && xIntersectInterval);

	return touch;
}

inline bool definitelyBigger(const float a, const float b, const float epsilon)
{
   return ((a - epsilon) > b);
}


