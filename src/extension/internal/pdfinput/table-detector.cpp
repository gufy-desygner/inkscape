/*
 * table-detector.cpp
 *
 *  Created on: Jan 4, 2022
 *      Author: sergey
 */

#include "table-detector.h"

static int getPos(Inkscape::XML::Node *node)
{
	if (node->parent() == nullptr) return 0;
	Inkscape::XML::Node *currChild = node->parent()->firstChild();

	int pos = 1;
	while(currChild != node)
	{
		pos++;
		currChild = currChild->next();
	}
	return pos;
}

TableList* detectTables(SvgBuilder *builder, TableList* tables) {
  bool splitRegions = true;


	Inkscape::XML::Node *root = builder->getRoot();

	std::vector<std::string> tags;
	tags.push_back("svg:path");
	tags.push_back("svg:rect");
	tags.push_back("svg:line");

	auto regions = builder->getRegions(tags);
	for(NodeStateList& vecRegionNodes : *regions)
	{
		TableRegion* tabRegionStat= new TableRegion(builder);
		bool isTable = true;
		if (vecRegionNodes.size() < 2) continue;
		//printf("==============region===========\n");
		int regionPos = 0;
		Inkscape::XML::Node* regionParent = nullptr;
		if (! vecRegionNodes.empty())
		{
			regionPos = getPos(vecRegionNodes[0]->node);
			regionParent = vecRegionNodes[0]->node->parent();
		}

		for(auto& nodeStat: vecRegionNodes)
		{
			isTable = tabRegionStat->addLine(nodeStat->node);
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
						Inkscape::XML::Node* imgGroup = imageNode;
						while(imgGroup->parent() != nullptr && imgGroup->parent() != regionParent)
							imgGroup = imgGroup->parent();
						bool underTheTbale = (imgGroup->parent() != nullptr && regionPos > getPos(imgGroup));
						if (! underTheTbale)
						{
							isTable = false;
							break;
						}
					}
				}
			}
		}

		if (isTable) tables->push_back(tabRegionStat);
	}

	delete(regions);
	return tables;
}
