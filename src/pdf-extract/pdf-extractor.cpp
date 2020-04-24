#include <stdio.h>
#include <poppler-config.h>
#include <Page.h>
#include "PDFDoc.h"
#include <popt.h>
#include <stdlib.h>
#include "goo/GooString.h"
#include "goo/GooList.h"
#include "string"
#include <Outline.h>
#include "PagesCatalog.h"
#include "PdfAnchor.h"
#include "tools.h"
#include <locale>
#include <codecvt>

struct AppParams {
	char* imputFileName;
	char* outputFileName;
};

AppParams parameters = {
		nullptr,
		nullptr,
};



struct poptOption optionsTable[] = {
     { "input", 'i', POPT_ARG_STRING, &parameters.imputFileName, 0,
     "input pdf file", 0 },
     { "output", 'o', POPT_ARG_STRING, &parameters.outputFileName, 0,
     "output json file", 0 },
     POPT_AUTOHELP
     { NULL, 0, 0, NULL, 0 }
};

void parseCLIoptions(int argc, const char **argv, poptOption *optionsTable)
{
	int opt;
	poptContext optCon = poptGetContext(NULL, argc, argv, optionsTable, 0);

	if ( argc < 2 )
	{
		poptPrintHelp(optCon, stderr, 0);
		exit(0);
	}

	while ((opt = poptGetNextOpt(optCon)) >= 0)
	{
		switch (opt)
		{
		case 1:
			poptPrintHelp(optCon, stderr, 0);
			exit(0);
		}
	}
	poptFreeContext(optCon);
}
void inspectPDFTree(Object* rootObj, const int intend = 0);

std::wstring charToWString(const char* str)
{
	std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
	return converter.from_bytes(str);
}

std::string wstrintToUtf8(const wchar_t* wstr)
{
	std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
	return converter.to_bytes(wstr);
}

void printPdfArray(Array* arr, const int intend = 0)
{
	std::string level;
	for(int i = 0; i < intend; i++) level.append("\t");
	const int arrLen = arr->getLength();
	for(int i = 0; i < arrLen; i++)
	{
		Object obj;
		arr->getNF(i, &obj);
		if (obj.isRef())
		{
			printf("%s[%i %i obj]\n", level.c_str(), obj.getRefNum(), obj.getRefGen());
			arr->get(i, &obj);
		}
		switch (obj.getType()){
		case ObjType::objString :
			printf("%s==>arrString(\"%s\")\n", level.c_str(), obj.getString()->getCString());
			break;
		case ObjType::objArray :
			printPdfArray(obj.getArray(), intend + 1);
			break;
		case ObjType::objDict :
			printf("%s==[\n", level.c_str());
			inspectPDFTree(&obj, intend + 1);
			printf("%s==]\n", level.c_str());
			break;
		}
	}
}

void inspectPDFTree(Object* rootObj, const int intend)
{
	Object* inspectObj = rootObj;

	if (inspectObj->isDict())
	{
		std::string level;
		for(int i = 0; i < intend; i++) level.append("\t");

		Dict* dict = inspectObj->getDict();
		const int dictLen = dict->getLength();
		//printf("%s[%i %i R]", level.c_str(), inspectObj->getRefNum(), inspectObj->getRefGen());
		for(int i = 0; i < dictLen; i++)
		{
			const char* keyName = dict->getKey(i);
			printf("%skey(%i)=\"%s\"\n", level.c_str(), i, keyName);
			Object childObj;
			dict->getValNF(i, &childObj);
			if (childObj.isRef())
			{
				printf("%s[%i %i obj]\n", level.c_str(),childObj.getRefNum(), childObj.getRefGen());
				dict->getVal(i, &childObj);
			}
			switch (childObj.getType()) {
			case ObjType::objDict :
				if (strcasecmp(keyName, "Parent") == 0) continue;
				if (strcasecmp(keyName, "Prev") == 0) continue;
				if (strcasecmp(keyName, "Last") == 0) continue;
				if (strcasecmp(keyName, "Next") == 0) {
					inspectPDFTree(&childObj, intend);
				} else {
					inspectPDFTree(&childObj, intend + 1);
				}
				break;
			case ObjType::objString :
				printf("%s==>String(\"%s\")\n", level.c_str(), childObj.getString()->getCString());
				break;
			case ObjType::objArray :
				Array* arr = childObj.getArray();
				printPdfArray(arr, intend + 1);

				break;
			}
		}
	}
}

class DestParams : public LinkDest
{
public:
	DestParams(Array* destArray, const char* destName =""): LinkDest(destArray) { name = destName; };
	const char* getName() const { return name.c_str(); };
private:
	std::string name;
};

wchar_t* anchorPrepareTitle(PdfAnchor* anchor)
{
	wchar_t* titlePtr = (wchar_t*) anchor->getTitle();
	int titleLen = anchor->getTitleLength();
	wchar_t* titleCutted = (wchar_t*)malloc(sizeof(wchar_t) * (titleLen + 1));
	memcpy(titleCutted, titlePtr, sizeof(wchar_t) * titleLen);
	titleCutted[titleLen] = 0;
	return titleCutted;
}

std::vector<DestParams*> parseDestNamesArray(Array* destNamesArray)
{
	std::vector<DestParams*> result;

	const int arrLen = destNamesArray->getLength();
	for(int i = 0; i < arrLen; i+=2)
	{
		Object nameObj;
		Object destObj;
		Array* destPointArray = nullptr;
		char* destName = nullptr;

		destNamesArray->get(i, &nameObj);
		if(nameObj.isString())
		{
			char* tmpName = nameObj.getString()->getCString();
			GooString* gooDestName = nameObj.getString();
			if (gooDestName->hasUnicodeMarker())
			{
				std::string utf8DestName = std::wstring_convert<
				        std::codecvt_utf8_utf16<char16_t>, char16_t>{}.to_bytes((char16_t*)(gooDestName->getCString() + 3));
				destName = (char*)malloc(utf8DestName.size() + 1);
				memcpy(destName, utf8DestName.c_str(), utf8DestName.size() + 1);
			} else
				destName = nameObj.getString()->getCString();
		}

		destNamesArray->get(i + 1, &destObj);
		if(destObj.isArray())
			destPointArray = destObj.getArray();

		if (destName and destPointArray) {
			DestParams* destParams = new DestParams(destPointArray, destName);
			result.push_back(destParams);
		}
	}

	return result;
}

DestParams* findDest(std::vector<DestParams*> dests, const char* name)
{
	for(auto dest : dests)
	{
		if (strcasecmp(dest->getName(), name) == 0 )
			return dest;
	}
	return nullptr;
}

void printAnchors(PdfAnchor* aTree, std::vector<DestParams*> dests,  std::string intend = "")
{
	PdfAnchor* tmpItem = aTree;
	PdfAnchor* deleteClass = nullptr;
	while(tmpItem != nullptr)
	{
		wprintf(L"%s{\n", intend.c_str());
		char *destName = tmpItem->getDestName();
		DestParams* dest = nullptr;
		if (destName == nullptr)
		{
			wprintf(L"%s  title: \"%S\",\n", intend.c_str() ,(wchar_t*)aTree->getTitle());
		}
		else
		{
			dest = findDest(dests, tmpItem->getDestName());
			if (dest)
			{
				wchar_t* title = anchorPrepareTitle(tmpItem);
				wprintf(L"%s  title : \"%S\", dest_name : \"%s\", x : %f, y : %f,\n",
						intend.c_str(),
						title,
						tmpItem->getDestName(),
						dest->getLeft(), dest->getTop());
				free(title);
			}
			else
			{
				wprintf(L"%s  title : \"%S\", dest_name = \"%s\",\n", intend.c_str() ,(wchar_t*)tmpItem->getTitle(), tmpItem->getDestName());
			}
		}

		if (tmpItem->hasKids())
		{
			PdfAnchor* child = aTree->firstChild();
			std::string newIntend = intend + "    ";
			wprintf(L"%s  kinds : [\n", intend.c_str());
			printAnchors(child, dests, newIntend);
			wprintf(L"%s\n  ],\n", intend.c_str());
		}

		wprintf(L"%s}", intend.c_str());
		if (tmpItem != aTree) deleteClass = tmpItem;

		tmpItem = tmpItem->next();
		delete(deleteClass);
		if (tmpItem != nullptr) wprintf(L",\n");

	}
}

void outAnchors(PdfAnchor* aTree, std::vector<DestParams*> dests, FILE* outFile, int page)
{
	PdfAnchor* tmpItem = aTree;
	PdfAnchor* deleteClass = nullptr;

	fwrite("[", 1, 1, outFile);
	while(tmpItem != nullptr)
	{
		fwrite("{", 1, 1, outFile);
		const char *destName = tmpItem->getDestName();
		std::string utf8DestName;
		if (destName[0] == '\xfe' && destName[1] == '\xff')
		{
			utf8DestName = std::wstring_convert<
			        std::codecvt_utf8_utf16<char16_t>, char16_t>{}.to_bytes((char16_t*)(destName + 3));
			destName = utf8DestName.c_str();
		}
		DestParams* dest = nullptr;
		if (destName != nullptr)
		{
			dest = findDest(dests, destName);
		}

		// we should split bookmarks by pages
		if (destName == nullptr || dest == nullptr || dest->getPageNum() != page)
		{
			const char* titleStr = "\"title\": \"";
			fwrite( titleStr, 1, strlen(titleStr), outFile);
			std::string utf8Title = wstrintToUtf8(anchorPrepareTitle(tmpItem));
			fwrite(utf8Title.c_str(), 1, utf8Title.size() , outFile);
			fwrite("\"", 1, 1 , outFile);
		}
		else
		{
			if (dest)
			{
				wchar_t* title = anchorPrepareTitle(tmpItem);

				std::string utf8Title =wstrintToUtf8(title);
				std::string buf("\"title\" :\"" + utf8Title +
						"\", \"dest_name\" : \"" + destName /*tmpItem->getDestName()*/ +
						"\", \"x\" : " + std::to_string(dest->getLeft()) +
						", \"y\" : " + std::to_string(dest->getTop()));
				free(title);
				fwrite(buf.c_str(), 1, buf.size(), outFile);
			}
			else
			{
				//wprintf(L"%s  title : \"%S\", dest_name = \"%s\",\n", intend.c_str() ,(wchar_t*)tmpItem->getTitle(), tmpItem->getDestName());
			}
		}

		if (tmpItem->hasKids())
		{
			PdfAnchor* child = tmpItem->firstChild();
			//std::string kinds(", \"kinds\" : [");
			std::string kinds(", \"kinds\" : ");
			fwrite(kinds.c_str(), 1, kinds.size(), outFile);
			outAnchors(child, dests, outFile, page);
			//fwrite("]", 1, 1, outFile);
			//if (tmpItem->next()) fwrite(",", 1, 1, outFile);
		}


		if (tmpItem != aTree) deleteClass = tmpItem;

		tmpItem = tmpItem->next();
		fwrite("}", 1, 1, outFile);
		delete(deleteClass);
		if (tmpItem != nullptr) fwrite(",", 1, 1, outFile);
	}
	fwrite("]", 1, 1, outFile);
}

int main(int argc, const char** argv)
{
	parseCLIoptions(argc, argv, optionsTable);


	GooString *gfileName = new GooString(parameters.imputFileName);
	PDFDoc *doc = new PDFDoc (gfileName, NULL, NULL, NULL);

	if (!doc->isOk()) {
		printf("PDF file is damaged\n");
		delete doc;
		return -1;
	}
	XRef* docXRef = doc->getXRef();

	// get root PDF's object
	const int rootGen = docXRef->getRootGen();
	const int rootNum = docXRef->getRootNum();
	Object rootObj;
	docXRef->fetch(rootNum, rootGen, &rootObj);

	// Get destination of links array
	Object destsNamesObj;
	getObjectByPath("Names/Dests/Names", &rootObj, &destsNamesObj);
	std::vector<DestParams*> dests;
	if (destsNamesObj.isArray())
	{
		Array* destsArray= destsNamesObj.getArray();
		dests = parseDestNamesArray(destsArray);
	}

	// get hierarchy of bookmarks
	Object rootAnchor;
	getObjectByPath("Outlines/First", &rootObj, &rootAnchor);
	if (! rootAnchor.isDict())
		return 0;

	PdfAnchor aTree(rootAnchor);

	// Spool JSON with matched destination and bookmarks
	if (parameters.outputFileName)
	{
		const int numOfPages = doc->getNumPages();
		for(int i = 1; i <= numOfPages; i++)
		{
			Page* page = doc->getPage(i);
			const int pageObjNum = page->getRef().num;
			std::string fileName(parameters.outputFileName + std::to_string(i) + ".json");
			FILE* outFile = fopen(fileName.c_str(), "w+");
			outAnchors(&aTree, dests, outFile, pageObjNum);
		}
	} else {
		printAnchors(&aTree, dests);
		printf("\n");
	}

	return 0;
}
