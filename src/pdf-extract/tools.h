/*
 * tools.h
 *
 *  Created on: 3 февр. 2020 г.
 *      Author: sergey
 */

#ifndef SUBPROJECTS__PDF_EXTRACT_TOOLS_H_
#define SUBPROJECTS__PDF_EXTRACT_TOOLS_H_

#include <vector>
#include <string>

#include <poppler-config.h>
#include <Page.h>
#include "PDFDoc.h"

std::vector<std::string> explode(std::string const & s, char delim);
Object* getObjectByPath(const char* path, Object* srcObj, Object* destObj, bool getRef = false);

#endif /* SUBPROJECTS__PDF_EXTRACT_TOOLS_H_ */
