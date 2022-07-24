/*
 * tools.cpp
 *
 *  Created on: 3 февр. 2020 г.
 *      Author: sergey
 */

#include "tools.h"
#include <sstream>

std::vector<std::string> explode(std::string const & s, char delim)
{
    std::vector<std::string> result;
    std::istringstream iss(s);

    for (std::string token; std::getline(iss, token, delim); )
    {
        result.push_back(std::move(token));
    }

    return result;
}

Object* getObjectByPath(const char* path, Object* srcObj, Object* destObj, bool getRef)
{
	static Object nullObj;
	Object* loopObj = srcObj;

	auto arrPath = explode(path, '/');
	const size_t segmentsCount = arrPath.size();
	for(int i = 0; i < segmentsCount; i++)
	{
		Object dictObj;
		if (loopObj->isDict())
		{
			Dict* loopDict = loopObj->getDict();
			if (loopDict->hasKey(arrPath[i].c_str()))
			{
				if (loopObj == srcObj) loopObj = destObj;

				if ((i == (segmentsCount - 1)) && getRef)
					*loopObj = loopDict->lookupNF(arrPath[i].c_str()).copy();
				else
					*loopObj = loopDict->lookup(arrPath[i].c_str()).copy();
			}
		} else
			return &nullObj;
	}

	return destObj;
}
