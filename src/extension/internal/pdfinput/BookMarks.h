/*
 * BookMarks.h
 *
 *  Created on: 15 февр. 2020 г.
 *      Author: sergey
 */

#ifndef SRC_EXTENSION_INTERNAL_PDFINPUT_BOOKMARKS_H_
#define SRC_EXTENSION_INTERNAL_PDFINPUT_BOOKMARKS_H_

#include <jsoncpp/json/json.h>
#include "svg-builder.h"
#include "sp-root.h"

class BookMarks {
public:
	BookMarks(const char* fileName);
	int getCount() const { return weBrandBooks.size(); };
	Json::Value getItem(int i) const { return weBrandBooks[i]; };
	Json::Value getItemVal(int i, const char* valName) const;
	std::string getItemValStr(int i, const char* valName) const;
	double getItemValD(int i, const char* valName, double defVal) const;
	void MergeWithSvgBuilder(Inkscape::Extension::Internal::SvgBuilder* builder);
	bool isOk() const { return ok; };
private:
	bool ok;
	Json::Value weBrandBooks;
	Json::Value root;
};

#endif /* SRC_EXTENSION_INTERNAL_PDFINPUT_BOOKMARKS_H_ */
