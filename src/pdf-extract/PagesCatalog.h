/*
 * PagesCatalog.h
 *
 *  Created on: 2 февр. 2020 г.
 *      Author: sergey
 */

#ifndef SRC_PAGESCATALOG_H_
#define SRC_PAGESCATALOG_H_

#include <stdio.h>
#include <poppler-config.h>
#include "PDFDoc.h"
#include <popt.h>
#include <stdlib.h>
#include "goo/GooString.h"
//#include "goo/GooList.h"
#include "string"

class PagesCatalog {
public:
	PagesCatalog(){};
	PagesCatalog(XRef* docXRef);

	void add(Ref page) { pagesRef.push_back(page); };
	bool addPages(Object* obj);
	bool addArray(Array* arr);
private:
	std::vector<Ref> pagesRef;
};

#endif /* SRC_PAGESCATALOG_H_ */
