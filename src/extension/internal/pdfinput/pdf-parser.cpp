 /*
 * PDF parsing using libpoppler.
 *
 * Derived from poppler's Gfx.cc
 *
 * Authors:
 *   Jon A. Cruz <jon@joncruz.org>
 *
 * Copyright 2012 authors
 * Copyright 1996-2003 Glyph & Cog, LLC
 *
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#ifdef HAVE_POPPLER

#ifdef USE_GCC_PRAGMAS
#pragma implementation
#endif
#define CURL_STATICLIB 1
extern "C" {
        
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <math.h>

}

#include <CharCodeToUnicode.h>
#include "svg-builder.h"
#include "Gfx.h"
#include "pdf-parser.h"
#include "util/units.h"
#include "preferences.h"
#include "XRef.h"

#include "goo/gmem.h"
#include "goo/GooTimer.h"
//#include "goo/GooHash.h"
#include "GlobalParams.h"
#include "CharTypes.h"
#include "Object.h"
#include "Array.h"
#include "Dict.h"
#include "Stream.h"
#include "Lexer.h"
#include "Parser.h"
#include "GfxFont.h"
#include "GfxState.h"
#include "OutputDev.h"
#include "Page.h"
#include "Annot.h"
#include "Error.h"
#include "shared_opt.h"
#include "xml/node.h"
#include "xml/element-node.h"
#include "xml/attribute-record.h"
#include "util/list.h"
#include "png-merge.h"
#include "sp-item.h"
#include "helper/png-write.h"
#include <curl/curl.h>
#include <ft2build.h>
#include <pthread.h>
#include "shared_opt.h"
#include FT_FREETYPE_H
#include <sys/stat.h>

// the MSVC math.h doesn't define this
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

//------------------------------------------------------------------------
// constants
//------------------------------------------------------------------------

// Default max delta allowed in any color component for a shading fill.
#define defaultShadingColorDelta (dblToCol( 1 / 2.0 ))

// Default max recursive depth for a shading fill.
#define defaultShadingMaxDepth 6

// Max number of operators kept in the history list.
#define maxOperatorHistoryDepth 16

//------------------------------------------------------------------------
// Operator table
//------------------------------------------------------------------------

PdfOperator PdfParser::opTab[] = {
  {"\"",  3, {tchkNum,    tchkNum,    tchkString},
          &PdfParser::opMoveSetShowText},
  {"'",   1, {tchkString},
          &PdfParser::opMoveShowText},
  {"B",   0, {tchkNone},
          &PdfParser::opFillStroke},
  {"B*",  0, {tchkNone},
          &PdfParser::opEOFillStroke},
  {"BDC", 2, {tchkName,   tchkProps},
          &PdfParser::opBeginMarkedContent},
  {"BI",  0, {tchkNone},
          &PdfParser::opBeginImage},
  {"BMC", 1, {tchkName},
          &PdfParser::opBeginMarkedContent},
  {"BT",  0, {tchkNone},
          &PdfParser::opBeginText},
  {"BX",  0, {tchkNone},
          &PdfParser::opBeginIgnoreUndef},
  {"CS",  1, {tchkName},
          &PdfParser::opSetStrokeColorSpace},
  {"DP",  2, {tchkName,   tchkProps},
          &PdfParser::opMarkPoint},
  {"Do",  1, {tchkName},
          &PdfParser::opXObject},
  {"EI",  0, {tchkNone},
          &PdfParser::opEndImage},
  {"EMC", 0, {tchkNone},
          &PdfParser::opEndMarkedContent},
  {"ET",  0, {tchkNone},
          &PdfParser::opEndText},
  {"EX",  0, {tchkNone},
          &PdfParser::opEndIgnoreUndef},
  {"F",   0, {tchkNone},
          &PdfParser::opFill},
  {"G",   1, {tchkNum},
          &PdfParser::opSetStrokeGray},
  {"ID",  0, {tchkNone},
          &PdfParser::opImageData},
  {"J",   1, {tchkInt},
          &PdfParser::opSetLineCap},
  {"K",   4, {tchkNum,    tchkNum,    tchkNum,    tchkNum},
          &PdfParser::opSetStrokeCMYKColor},
  {"M",   1, {tchkNum},
          &PdfParser::opSetMiterLimit},
  {"MP",  1, {tchkName},
          &PdfParser::opMarkPoint},
  {"Q",   0, {tchkNone},
          &PdfParser::opRestore},
  {"RG",  3, {tchkNum,    tchkNum,    tchkNum},
          &PdfParser::opSetStrokeRGBColor},
  {"S",   0, {tchkNone},
          &PdfParser::opStroke},
  {"SC",  -4, {tchkNum,   tchkNum,    tchkNum,    tchkNum},
          &PdfParser::opSetStrokeColor},
  {"SCN", -33, {tchkSCN,   tchkSCN,    tchkSCN,    tchkSCN,
	        tchkSCN,   tchkSCN,    tchkSCN,    tchkSCN,
	        tchkSCN,   tchkSCN,    tchkSCN,    tchkSCN,
	        tchkSCN,   tchkSCN,    tchkSCN,    tchkSCN,
	        tchkSCN,   tchkSCN,    tchkSCN,    tchkSCN,
	        tchkSCN,   tchkSCN,    tchkSCN,    tchkSCN,
	        tchkSCN,   tchkSCN,    tchkSCN,    tchkSCN,
	        tchkSCN,   tchkSCN,    tchkSCN,    tchkSCN,
	        tchkSCN},
          &PdfParser::opSetStrokeColorN},
  {"T*",  0, {tchkNone},
          &PdfParser::opTextNextLine},
  {"TD",  2, {tchkNum,    tchkNum},
          &PdfParser::opTextMoveSet},
  {"TJ",  1, {tchkArray},
          &PdfParser::opShowSpaceText},
  {"TL",  1, {tchkNum},
          &PdfParser::opSetTextLeading},
  {"Tc",  1, {tchkNum},
          &PdfParser::opSetCharSpacing},
  {"Td",  2, {tchkNum,    tchkNum},
          &PdfParser::opTextMove},
  {"Tf",  2, {tchkName,   tchkNum},
          &PdfParser::opSetFont},
  {"Tj",  1, {tchkString},
          &PdfParser::opShowText},
  {"Tm",  6, {tchkNum,    tchkNum,    tchkNum,    tchkNum,
	      tchkNum,    tchkNum},
          &PdfParser::opSetTextMatrix},
  {"Tr",  1, {tchkInt},
          &PdfParser::opSetTextRender},
  {"Ts",  1, {tchkNum},
          &PdfParser::opSetTextRise},
  {"Tw",  1, {tchkNum},
          &PdfParser::opSetWordSpacing},
  {"Tz",  1, {tchkNum},
          &PdfParser::opSetHorizScaling},
  {"W",   0, {tchkNone},
          &PdfParser::opClip},
  {"W*",  0, {tchkNone},
          &PdfParser::opEOClip},
  {"b",   0, {tchkNone},
          &PdfParser::opCloseFillStroke},
  {"b*",  0, {tchkNone},
          &PdfParser::opCloseEOFillStroke},
  {"c",   6, {tchkNum,    tchkNum,    tchkNum,    tchkNum,
	      tchkNum,    tchkNum},
          &PdfParser::opCurveTo},
  {"cm",  6, {tchkNum,    tchkNum,    tchkNum,    tchkNum,
	      tchkNum,    tchkNum},
          &PdfParser::opConcat},
  {"cs",  1, {tchkName},
          &PdfParser::opSetFillColorSpace},
  {"d",   2, {tchkArray,  tchkNum},
          &PdfParser::opSetDash},
  {"d0",  2, {tchkNum,    tchkNum},
          &PdfParser::opSetCharWidth},
  {"d1",  6, {tchkNum,    tchkNum,    tchkNum,    tchkNum,
	      tchkNum,    tchkNum},
          &PdfParser::opSetCacheDevice},
  {"f",   0, {tchkNone},
          &PdfParser::opFill},
  {"f*",  0, {tchkNone},
          &PdfParser::opEOFill},
  {"g",   1, {tchkNum},
          &PdfParser::opSetFillGray},
  {"gs",  1, {tchkName},
          &PdfParser::opSetExtGState},
  {"h",   0, {tchkNone},
          &PdfParser::opClosePath},
  {"i",   1, {tchkNum},
          &PdfParser::opSetFlat},
  {"j",   1, {tchkInt},
          &PdfParser::opSetLineJoin},
  {"k",   4, {tchkNum,    tchkNum,    tchkNum,    tchkNum},
          &PdfParser::opSetFillCMYKColor},
  {"l",   2, {tchkNum,    tchkNum},
          &PdfParser::opLineTo},
  {"m",   2, {tchkNum,    tchkNum},
          &PdfParser::opMoveTo},
  {"n",   0, {tchkNone},
          &PdfParser::opEndPath},
  {"q",   0, {tchkNone},
          &PdfParser::opSave},
  {"re",  4, {tchkNum,    tchkNum,    tchkNum,    tchkNum},
          &PdfParser::opRectangle},
  {"rg",  3, {tchkNum,    tchkNum,    tchkNum},
          &PdfParser::opSetFillRGBColor},
  {"ri",  1, {tchkName},
          &PdfParser::opSetRenderingIntent},
  {"s",   0, {tchkNone},
          &PdfParser::opCloseStroke},
  {"sc",  -4, {tchkNum,   tchkNum,    tchkNum,    tchkNum},
          &PdfParser::opSetFillColor},
  {"scn", -33, {tchkSCN,   tchkSCN,    tchkSCN,    tchkSCN,
	        tchkSCN,   tchkSCN,    tchkSCN,    tchkSCN,
	        tchkSCN,   tchkSCN,    tchkSCN,    tchkSCN,
	        tchkSCN,   tchkSCN,    tchkSCN,    tchkSCN,
	        tchkSCN,   tchkSCN,    tchkSCN,    tchkSCN,
	        tchkSCN,   tchkSCN,    tchkSCN,    tchkSCN,
	        tchkSCN,   tchkSCN,    tchkSCN,    tchkSCN,
	        tchkSCN,   tchkSCN,    tchkSCN,    tchkSCN,
	        tchkSCN},
          &PdfParser::opSetFillColorN},
  {"sh",  1, {tchkName},
          &PdfParser::opShFill},
  {"v",   4, {tchkNum,    tchkNum,    tchkNum,    tchkNum},
          &PdfParser::opCurveTo1},
  {"w",   1, {tchkNum},
          &PdfParser::opSetLineWidth},
  {"y",   4, {tchkNum,    tchkNum,    tchkNum,    tchkNum},
          &PdfParser::opCurveTo2}
};

#define numOps (sizeof(opTab) / sizeof(PdfOperator))

namespace {

GfxPatch blankPatch()
{
    GfxPatch patch;
    memset(&patch, 0, sizeof(patch)); // quick-n-dirty
    return patch;
}

} // namespace

//------------------------------------------------------------------------
// ClipHistoryEntry
//------------------------------------------------------------------------

class ClipHistoryEntry {
public:

    ClipHistoryEntry(GfxPath *clipPath = NULL, GfxClipType clipType = clipNormal);
    virtual ~ClipHistoryEntry();

    // Manipulate clip path stack
    ClipHistoryEntry *save();
    ClipHistoryEntry *restore();
    GBool hasSaves() { return saved != NULL; }
    void setClip(GfxPath *newClipPath, GfxClipType newClipType = clipNormal);
    GfxPath *getClipPath() { return clipPath; }
    GfxClipType getClipType() { return clipType; }

private:

    ClipHistoryEntry *saved;    // next clip path on stack
        
    GfxPath *clipPath;        // used as the path to be filled for an 'sh' operator
    GfxClipType clipType;

    ClipHistoryEntry(ClipHistoryEntry *other);
};

//------------------------------------------------------------------------
// PdfParser
//------------------------------------------------------------------------

PdfParser::PdfParser(XRef *xrefA,
		     Inkscape::Extension::Internal::SvgBuilder *builderA,
                     int /*pageNum*/,
		     int rotate,
		     Dict *resDict,
                     PDFRectangle *box,
		     PDFRectangle *cropBox) :
    xref(xrefA),
    builder(builderA),
    subPage(gFalse),
    printCommands(false),
    res(new GfxResources(xref, resDict, NULL)), // start the resource stack
    state(new GfxState(sp_export_dpi_sh/*72.0*/, sp_export_dpi_sh /*72.0*/, box, rotate, gTrue)),
    fontChanged(gFalse),
    clip(clipNone),
	actualtextString(NULL),
    ignoreUndef(0),
    baseMatrix(),
    formDepth(0),
    parser(NULL),
	actualMarkerBegin(FALSE),
    colorDeltas(),
    maxDepths(),
    clipHistory(new ClipHistoryEntry()),
    operatorHistory(NULL),
    backgroundCandidat(NULL),
	layoutIsNew(false),
	layoutProperties(NULL),
	simulate(false),
	creator(nullptr)
{
  setDefaultApproximationPrecision();
  builder->setDocumentSize(Inkscape::Util::Quantity::convert(state->getPageWidth(), "pt", "px"),
                           Inkscape::Util::Quantity::convert(state->getPageHeight(), "pt", "px"));

  const double *ctm = state->getCTM();
  double scaledCTM[6];
  for (int i = 0; i < 6; ++i) {
    baseMatrix[i] = ctm[i];
    scaledCTM[i] = Inkscape::Util::Quantity::convert(ctm[i], "pt", "px");
  }
  saveState();
  builder->setTransform((double*)&scaledCTM);
  formDepth = 0;

  // set crop box
  if (cropBox) {
    if (printCommands)
        printf("cropBox: %f %f %f %f\n", cropBox->x1, cropBox->y1, cropBox->x2, cropBox->y2);
    // do not clip if it's not needed
    if (cropBox->x1 != 0.0 || cropBox->y1 != 0.0 ||
        cropBox->x2 != state->getPageWidth() || cropBox->y2 != state->getPageHeight()) {
        
        state->moveTo(cropBox->x1, cropBox->y1);
        state->lineTo(cropBox->x2, cropBox->y1);
        state->lineTo(cropBox->x2, cropBox->y2);
        state->lineTo(cropBox->x1, cropBox->y2);
        state->closePath();
        state->clip();
        clipHistory->setClip((GfxPath*)state->getPath(), clipNormal);
        builder->setClipPath(state);
        state->clearPath();
    }
  }
  pushOperator("startPage");
  cidFontList = g_ptr_array_new();
  savedFontsList = g_ptr_array_new();
  exportFontThreads = g_ptr_array_new();
}

PdfParser::PdfParser(XRef *xrefA,
		     Inkscape::Extension::Internal::SvgBuilder *builderA,
                     Dict *resDict,
		     PDFRectangle *box) :
    xref(xrefA),
    builder(builderA),
    subPage(gTrue),
    printCommands(false),
    res(new GfxResources(xref, resDict, NULL)), // start the resource stack
    state(new GfxState(72, 72, box, 0, gFalse)),
    fontChanged(gFalse),
    clip(clipNone),
    ignoreUndef(0),
    baseMatrix(),
    formDepth(0),
	actualtextString(NULL),
    parser(NULL),
	actualMarkerBegin(FALSE),
    colorDeltas(),
    maxDepths(),
    clipHistory(new ClipHistoryEntry()),
    operatorHistory(NULL),
	backgroundCandidat(NULL),
	layoutProperties(NULL),
	simulate(false),
	creator(nullptr)
{
  setDefaultApproximationPrecision();
  
  for (int i = 0; i < 6; ++i) {
    baseMatrix[i] = state->getCTM()[i];
  }
  formDepth = 0;
  cidFontList = g_ptr_array_new();
  savedFontsList = g_ptr_array_new();
  exportFontThreads = g_ptr_array_new();
}

PdfParser::~PdfParser() {
  while(operatorHistory) {
    OpHistoryEntry *tmp = operatorHistory->next;
    delete operatorHistory;
    operatorHistory = tmp;
  }

  while (state && state->hasSaves()) {
    restoreState();
  }

  if (!subPage) {
    //out->endPage();
  }

  while (res) {
    popResources();
  }

  if (state) {
    delete state;
    state = NULL;
  }

  if (clipHistory) {
    delete clipHistory;
    clipHistory = NULL;
  }
  g_ptr_array_free(cidFontList, true);
  g_ptr_array_free(savedFontsList, false);
}

void PdfParser::simulateParse(Object *obj, GBool topLevel) {
	simulate = true;
	parse(obj, topLevel);
	simulate = false;
}

void PdfParser::parse(Object *obj, GBool topLevel) {
  Object obj2;
  if (topLevel)
	  cmdCounter = 0;

  if (obj->isArray()) {
    for (int i = 0; i < obj->arrayGetLength(); ++i) {
      obj2 = obj->arrayGet(i);
      if (!obj2.isStream()) {
	error(errInternal, -1, "Weird page contents");
	return;
      }
    }
  } else if (!obj->isStream()) {
	error(errInternal, -1, "Weird page contents");
    	return;
  }
  //parser = new Parser(xref, new Lexer(xref, obj), gFalse);
  parser = new Parser(xref, obj, gFalse);
  go(topLevel);


  // if founded bigest path mark it how class="background"
  if (backgroundCandidat && sp_add_background_sh)
    backgroundCandidat->setAttribute("class", "background");
  delete parser;
  parser = NULL;
}

void PdfParser::go(GBool /*topLevel*/)
{
  Object obj;
  Object args[maxArgs];

  // scan a sequence of objects
  int numArgs = 0;
  obj = parser->getObj();
  while (!obj.isEOF()) {

    // got a command - execute it
    if (obj.isCmd()) {
      if (printCommands) {
	obj.print(stdout);
	for (int i = 0; i < numArgs; ++i) {
	  printf(" ");
	  args[i].print(stdout);
	}
	printf("\n");
	fflush(stdout);
      }
      // Run the operation
      cmdCounter++;
      if (! simulate)
    	  execOp(&obj, args, numArgs);

      numArgs = 0;

    // got an argument - save it
    } else if (numArgs < maxArgs) {
    	args[numArgs++] = std::move(obj);

    // too many arguments - something is wrong
    } else {
      error(errSyntaxError, getPos(), "Too many args in content stream");
      if (printCommands) {
	printf("throwing away arg: ");
	obj.print(stdout);
	printf("\n");
	fflush(stdout);
      }
    }

    // grab the next object
    obj = parser->getObj();
  }

  // args at end with no command
  if (numArgs > 0) {
    error(errSyntaxError, getPos(), "Leftover args in content stream");
    if (printCommands) {
      printf("%d leftovers:", numArgs);
      for (int i = 0; i < numArgs; ++i) {
	printf(" ");
	args[i].print(stdout);

      }
      printf("\n");
      fflush(stdout);
    }
   /* for (int i = 0; i < numArgs; ++i)
      args[i].free();*/
  }

}

void PdfParser::pushOperator(const char *name)
{
    OpHistoryEntry *newEntry = new OpHistoryEntry;
    newEntry->name = name;
    newEntry->state = NULL;
    newEntry->depth = (operatorHistory != NULL ? (operatorHistory->depth+1) : 0);
    newEntry->next = operatorHistory;
    operatorHistory = newEntry;

    // Truncate list if needed
    if (operatorHistory->depth > maxOperatorHistoryDepth) {
        OpHistoryEntry *curr = operatorHistory;
        OpHistoryEntry *prev = NULL;
        while (curr && curr->next != NULL) {
            curr->depth--;
            prev = curr;
            curr = curr->next;
        }
        if (prev) {
            if (curr->state != NULL)
                delete curr->state;
            delete curr;
            prev->next = NULL;
        }
    }
}

const char *PdfParser::getPreviousOperator(unsigned int look_back) {
    OpHistoryEntry *prev = NULL;
    if (operatorHistory != NULL && look_back > 0) {
        prev = operatorHistory->next;
        while (--look_back > 0 && prev != NULL) {
            prev = prev->next;
        }
    }
    if (prev != NULL) {
        return prev->name;
    } else {
        return "";
    }
}

void PdfParser::execOp(Object *cmd, Object args[], int numArgs) {
  PdfOperator *op;
  const char *name;
  Object *argPtr;
  int i;

  // find operator
  name = cmd->getCmd();
  if (!(op = findOp((char*)name))) {
    if (ignoreUndef == 0)
      error(errSyntaxError, getPos(), "Unknown operator '{0:s}'", name);
    return;
  }

  // type check args
  argPtr = args;
  if (op->numArgs >= 0) {
    if (numArgs < op->numArgs) {
      error(errSyntaxError, getPos(), "Too few ({0:d}) args to '{1:d}' operator", numArgs, name);
      return;
    }
    if (numArgs > op->numArgs) {
#if 0
      error(errSyntaxError, getPos(), "Too many ({0:d}) args to '{1:s}' operator", numArgs, name);
#endif
      argPtr += numArgs - op->numArgs;
      numArgs = op->numArgs;
    }
  } else {
    if (numArgs > -op->numArgs) {
      error(errSyntaxError, getPos(), "Too many ({0:d}) args to '{1:s}' operator",
	    numArgs, name);
      return;
    }
  }
  for (i = 0; i < numArgs; ++i) {
    if (!checkArg(&argPtr[i], op->tchk[i])) {
      error(errSyntaxError, getPos(), "Arg #{0:d} to '{1:s}' operator is wrong type ({2:s})",
	    i, name, argPtr[i].getTypeName());
      return;
    }
  }

  // add to history
  pushOperator((char*)&op->name);

  // do it
  (this->*op->func)(argPtr, numArgs);
}

PdfOperator* PdfParser::findOp(char *name) {
  int a = -1;
  int b = numOps;
  int cmp = -1;
  // invariant: opTab[a] < name < opTab[b]
  while (b - a > 1) {
    const int m = (a + b) / 2;
    cmp = strcmp(opTab[m].name, name);
    if (cmp < 0)
      a = m;
    else if (cmp > 0)
      b = m;
    else
      a = b = m;
  }
  if (cmp != 0)
    return NULL;
  return &opTab[a];
}

GBool PdfParser::checkArg(Object *arg, TchkType type) {
  switch (type) {
  case tchkBool:   return arg->isBool();
  case tchkInt:    return arg->isInt();
  case tchkNum:    return arg->isNum();
  case tchkString: return arg->isString();
  case tchkName:   return arg->isName();
  case tchkArray:  return arg->isArray();
  case tchkProps:  return arg->isDict() || arg->isName();
  case tchkSCN:    return arg->isNum() || arg->isName();
  case tchkNone:   return gFalse;
  }
  return gFalse;
}

int PdfParser::getPos() {
  return parser ? parser->getPos() : -1;
}

//------------------------------------------------------------------------
// graphics state operators
//------------------------------------------------------------------------

void PdfParser::opSave(Object /*args*/[], int /*numArgs*/)
{
  saveState();
}

void PdfParser::opRestore(Object /*args*/[], int /*numArgs*/)
{
  restoreState();
}

// TODO not good that numArgs is ignored but args[] is used:
void PdfParser::opConcat(Object args[], int /*numArgs*/)
{
  state->concatCTM(args[0].getNum(), args[1].getNum(),
		   args[2].getNum(), args[3].getNum(),
		   args[4].getNum(), args[5].getNum());
  const char *prevOp = getPreviousOperator();
  double a0 = args[0].getNum();
  double a1 = args[1].getNum();
  double a2 = args[2].getNum();
  double a3 = args[3].getNum();
  double a4 = args[4].getNum();
  double a5 = args[5].getNum();
  if (!strcmp(prevOp, "q")) {
      builder->setTransform(a0, a1, a2, a3, a4, a5);
  } else if (!strcmp(prevOp, "cm") || !strcmp(prevOp, "startPage")) {
      // multiply it with the previous transform
      double otherMatrix[6];
      if (!builder->getTransform(otherMatrix)) { // invalid transform
          // construct identity matrix
          otherMatrix[0] = otherMatrix[3] = 1.0;
          otherMatrix[1] = otherMatrix[2] = otherMatrix[4] = otherMatrix[5] = 0.0;
      }
      double c0 = a0*otherMatrix[0] + a1*otherMatrix[2];
      double c1 = a0*otherMatrix[1] + a1*otherMatrix[3];
      double c2 = a2*otherMatrix[0] + a3*otherMatrix[2];
      double c3 = a2*otherMatrix[1] + a3*otherMatrix[3];
      double c4 = a4*otherMatrix[0] + a5*otherMatrix[2] + otherMatrix[4];
      double c5 = a4*otherMatrix[1] + a5*otherMatrix[3] + otherMatrix[5];
      builder->setTransform(c0, c1, c2, c3, c4, c5);
  } else {
      builder->pushGroup();
      builder->setTransform(a0, a1, a2, a3, a4, a5);
  }
  fontChanged = gTrue;
}

// TODO not good that numArgs is ignored but args[] is used:
void PdfParser::opSetDash(Object args[], int /*numArgs*/)
{
  double *dash = 0;

  Array *a = args[0].getArray();
  int length = a->getLength();
  if (length != 0) {
    dash = (double *)gmallocn(length, sizeof(double));
    for (int i = 0; i < length; ++i) {
      dash[i] = a->get(i).getNum();
    }
  }
  state->setLineDash(dash, length, args[1].getNum());
  builder->updateStyle(state);
}

// TODO not good that numArgs is ignored but args[] is used:
void PdfParser::opSetFlat(Object args[], int /*numArgs*/)
{
  state->setFlatness((int)args[0].getNum());
}

// TODO not good that numArgs is ignored but args[] is used:
void PdfParser::opSetLineJoin(Object args[], int /*numArgs*/)
{
  state->setLineJoin(args[0].getInt());
  builder->updateStyle(state);
}

// TODO not good that numArgs is ignored but args[] is used:
void PdfParser::opSetLineCap(Object args[], int /*numArgs*/)
{
  state->setLineCap(args[0].getInt());
  builder->updateStyle(state);
}

// TODO not good that numArgs is ignored but args[] is used:
void PdfParser::opSetMiterLimit(Object args[], int /*numArgs*/)
{
  state->setMiterLimit(args[0].getNum());
  builder->updateStyle(state);
}

// TODO not good that numArgs is ignored but args[] is used:
void PdfParser::opSetLineWidth(Object args[], int /*numArgs*/)
{
  state->setLineWidth(args[0].getNum());
  builder->updateStyle(state);
}

// TODO not good that numArgs is ignored but args[] is used:
void PdfParser::opSetExtGState(Object args[], int /*numArgs*/)
{
  Object obj1, obj2, obj3, obj4, obj5;
  Function *funcs[4] = {0, 0, 0, 0};
  GfxColor backdropColor;
  GBool haveBackdropColor = gFalse;
  GBool alpha = gFalse;

  obj1 = res->lookupGState(args[0].getName());
  if (obj1.isNull()) {
    return;
  }
  if (!obj1.isDict()) {
    error(errSyntaxError, getPos(), "ExtGState '{0:s}' is wrong type"), args[0].getName();
    //obj1.free();
    return;
  }
  if (printCommands) {
    printf("  gfx state dict: ");
    obj1.print();
    printf("\n");
  }

  // transparency support: blend mode, fill/stroke opacity
  obj2 = obj1.dictLookup(const_cast<char*>("BM"));
  if (! obj2.isNull()) {
    GfxBlendMode mode = gfxBlendNormal;
    if (state->parseBlendMode(&obj2, &mode)) {
      state->setBlendMode(mode);
    } else {
      error(errSyntaxError, getPos(), "Invalid blend mode in ExtGState");
    }
  }

  obj2 = obj1.dictLookup(const_cast<char*>("ca"));
  if (obj2.isNum()) {
    state->setFillOpacity(obj2.getNum());
  }

  obj2 = obj1.dictLookup(const_cast<char*>("CA"));
  if (obj2.isNum()) {
    state->setStrokeOpacity(obj2.getNum());
  }

  // fill/stroke overprint
  GBool haveFillOP = gFalse;
  obj2 = obj1.dictLookup(const_cast<char*>("op"));
  if (haveFillOP = (obj2.isBool())) {
    state->setFillOverprint(obj2.getBool());
  }

  obj2 = obj1.dictLookup(const_cast<char*>("OP"));
  if (obj2.isBool()) {
    state->setStrokeOverprint(obj2.getBool());
    if (!haveFillOP) {
      state->setFillOverprint(obj2.getBool());
    }
  }

  // stroke adjust
  obj2 = obj1.dictLookup(const_cast<char*>("SA"));
  if (obj2.isBool()) {
    state->setStrokeAdjust(obj2.getBool());
  }

  // transfer function
  obj2 = obj1.dictLookup(const_cast<char*>("TR2"));
  if (obj2.isNull()) {
    obj2 = obj1.dictLookup(const_cast<char*>("TR"));
  }
  if (obj2.isName(const_cast<char*>("Default")) ||
      obj2.isName(const_cast<char*>("Identity"))) {
    funcs[0] = funcs[1] = funcs[2] = funcs[3] = NULL;
    state->setTransfer(funcs);
  } else if (obj2.isArray() && obj2.arrayGetLength() == 4) {
    int pos = 4;
    for (int i = 0; i < 4; ++i) {
      obj3 = obj2.arrayGet(i);
      funcs[i] = Function::parse(&obj3);
      if (!funcs[i]) {
	pos = i;
	break;
      }
    }
    if (pos == 4) {
      state->setTransfer(funcs);
    }
  } else if (obj2.isName() || obj2.isDict() || obj2.isStream()) {
    if ((funcs[0] = Function::parse(&obj2))) {
      funcs[1] = funcs[2] = funcs[3] = NULL;
      state->setTransfer(funcs);
    }
  } else if (!obj2.isNull()) {
    error(errSyntaxError, getPos(), "Invalid transfer function in ExtGState");
  }

  // soft mask
  obj2 = obj1.dictLookup(const_cast<char*>("SMask"));
  if (!obj2.isNull()) {
    if (obj2.isName(const_cast<char*>("None"))) {
      builder->clearSoftMask(state);
    } else if (obj2.isDict()) {
      obj3 = obj2.dictLookup(const_cast<char*>("S"));
      if (obj3.isName(const_cast<char*>("Alpha"))) {
	    alpha = gTrue;
      } else { // "Luminosity"
	    alpha = gFalse;
      }

      funcs[0] = NULL;
      obj3 = obj2.dictLookup(const_cast<char*>("TR"));
      if (! obj3.isNull()) {
	    funcs[0] = Function::parse(&obj3);
	    if (funcs[0]->getInputSize() != 1 ||
	        funcs[0]->getOutputSize() != 1) {
	      error(errSyntaxError, getPos(), "Invalid transfer function in soft mask in ExtGState");
	     delete funcs[0];
	     funcs[0] = NULL;
	    }
      }

      obj3 = obj2.dictLookup(const_cast<char*>("BC"));
      if ((haveBackdropColor = obj3.isArray())) {
	    for (int i = 0; i < gfxColorMaxComps; ++i) {
	      backdropColor.c[i] = 0;
	    }
	    for (int i = 0; i < obj3.arrayGetLength() && i < gfxColorMaxComps; ++i) {
	      obj4 = obj3.arrayGet(i);
	      if (obj4.isNum()) {
	        backdropColor.c[i] = dblToCol(obj4.getNum());
	      }
	    }
      }

      obj3 = obj2.dictLookup(const_cast<char*>("G"));
      if (obj3.isStream()) {
    	obj4 = obj3.streamGetDict()->lookup(const_cast<char*>("Group"));
        if ( obj4.isDict() ) {
	      GfxColorSpace *blendingColorSpace = 0;
	      GBool isolated = gFalse;
	      GBool knockout = gFalse;
	  if (!_POPPLER_CALL_ARGS_DEREF(obj5, obj4.dictLookup, "CS").isNull()) {
	    blendingColorSpace = GfxColorSpace::parse(nullptr, &obj5, nullptr, state);
	  }
          _POPPLER_FREE(obj5);
	  if (_POPPLER_CALL_ARGS_DEREF(obj5, obj4.dictLookup, "I").isBool()) {
	    isolated = obj5.getBool();
	  }
          _POPPLER_FREE(obj5);
	  if (_POPPLER_CALL_ARGS_DEREF(obj5, obj4.dictLookup, "K").isBool()) {
	    knockout = obj5.getBool();
	  }
	  _POPPLER_FREE(obj5);

	      if (!haveBackdropColor) {
	        if (blendingColorSpace) {
	          blendingColorSpace->getDefaultColor(&backdropColor);
	        } else {
	      //~ need to get the parent or default color space (?)
	          for (int i = 0; i < gfxColorMaxComps; ++i) {
		        backdropColor.c[i] = 0;
	          }
	        }
	      }
	      doSoftMask(&obj3, alpha, blendingColorSpace,
		     isolated, knockout, funcs[0], &backdropColor);
	      if (funcs[0]) {
	        delete funcs[0];
	      }
	    } else {
	       error(errSyntaxError, getPos(), "Invalid soft mask in ExtGState - missing group");
	    }
      } else {
	error(errSyntaxError, getPos(), "Invalid soft mask in ExtGState - missing group");
      }

    } else if (!obj2.isNull()) {
      error(errSyntaxError, getPos(), "Invalid soft mask in ExtGState");
    }
  }
}

void PdfParser::doSoftMask(Object *str, GBool alpha,
		     GfxColorSpace *blendingColorSpace,
		     GBool isolated, GBool knockout,
		     Function *transferFunc, GfxColor *backdropColor) {
  Dict *dict, *resDict;
  double m[6], bbox[4];
  Object obj1, obj2;
  int i;

  // check for excessive recursion
  if (formDepth > 20) {
    return;
  }

  // get stream dict
  dict = str->streamGetDict();

  // check form type
  obj1 = dict->lookup(const_cast<char*>("FormType"));
  if (!(obj1.isNull() || (obj1.isInt() && obj1.getInt() == 1))) {
    error(errSyntaxError, getPos(), "Unknown form type");
  }
  //obj1.free();

  // get bounding box
  obj1 = dict->lookup(const_cast<char*>("BBox"));
  if (!obj1.isArray()) {
    //obj1.free();
    error(errSyntaxError, getPos(), "Bad form bounding box");
    return;
  }
  for (i = 0; i < 4; ++i) {
	obj2 = obj1.arrayGet(i);
    bbox[i] = obj2.getNum();
    //obj2.free();
  }
  //obj1.free();

  // get matrix
  obj1 = dict->lookup(const_cast<char*>("Matrix"));
  if (obj1.isArray()) {
    for (i = 0; i < 6; ++i) {
    	obj2 = obj1.arrayGet(i);
      m[i] = obj2.getNum();
      //obj2.free();
    }
  } else {
    m[0] = 1; m[1] = 0;
    m[2] = 0; m[3] = 1;
    m[4] = 0; m[5] = 0;
  }
  //obj1.free();

  // get resources
  obj1 = dict->lookup(const_cast<char*>("Resources"));
  resDict = obj1.isDict() ? obj1.getDict() : (Dict *)NULL;

  // draw it
  ++formDepth;
  doForm1(str, resDict, m, bbox, gTrue, gTrue,
	  blendingColorSpace, isolated, knockout,
	  alpha, transferFunc, backdropColor);
  --formDepth;

  if (blendingColorSpace) {
    delete blendingColorSpace;
  }
  //obj1.free();
}

void PdfParser::opSetRenderingIntent(Object /*args*/[], int /*numArgs*/)
{
}

//------------------------------------------------------------------------
// color operators
//------------------------------------------------------------------------

/**
 * Get a newly allocated color space instance by CS operation argument.
 *
 * Maintains a cache for named color spaces to avoid expensive re-parsing.
 */
GfxColorSpace *PdfParser::lookupColorSpaceCopy(Object &arg)
{
  assert(!arg.isNull());

  char const *name = arg.isName() ? arg.getName() : nullptr;
  GfxColorSpace *colorSpace = nullptr;

  if (name && (colorSpace = colorSpacesCache[name].get())) {
    return colorSpace->copy();
  }

  Object *argPtr = &arg;
  Object obj;

  if (name) {
    _POPPLER_CALL_ARGS(obj, res->lookupColorSpace, name);
    if (!obj.isNull()) {
      argPtr = &obj;
    }
  }

  colorSpace = GfxColorSpace::parse(res, argPtr, nullptr, state);

  _POPPLER_FREE(obj);

  if (name && colorSpace) {
    colorSpacesCache[name].reset(colorSpace->copy());
  }

  return colorSpace;
}

// TODO not good that numArgs is ignored but args[] is used:
void PdfParser::opSetFillGray(Object args[], int /*numArgs*/)
{
  GfxColor color;

  state->setFillPattern(NULL);
  state->setFillColorSpace(new GfxDeviceGrayColorSpace());
  color.c[0] = dblToCol(args[0].getNum());
  state->setFillColor(&color);
  builder->updateStyle(state);
}

// TODO not good that numArgs is ignored but args[] is used:
void PdfParser::opSetStrokeGray(Object args[], int /*numArgs*/)
{
  GfxColor color;

  state->setStrokePattern(NULL);
  state->setStrokeColorSpace(new GfxDeviceGrayColorSpace());
  color.c[0] = dblToCol(args[0].getNum());
  state->setStrokeColor(&color);
  builder->updateStyle(state);
}

// TODO not good that numArgs is ignored but args[] is used:
void PdfParser::opSetFillCMYKColor(Object args[], int /*numArgs*/)
{
  GfxColor color;
  int i;

  state->setFillPattern(NULL);
  state->setFillColorSpace(new GfxDeviceCMYKColorSpace());
  for (i = 0; i < 4; ++i) {
    color.c[i] = dblToCol(args[i].getNum());
  }
  state->setFillColor(&color);

  // if blend mode is multiple - change this to opacity
  if (state->getBlendMode() == gfxBlendMultiply && state->getFillOpacity() == 1 && sp_map_drop_color_sh)
  {
		GfxRGB fillColor;
		state->getFillRGB(&fillColor);
		double opacity = ((fillColor.r + fillColor.g + fillColor.b)/3.0/65535.0);

	    if (opacity != 1) {
	      builder->setGroupOpacity(opacity * state->getFillOpacity());
	      state->setFillOpacity(1);
	    }
  }
  builder->updateStyle(state);
}

// TODO not good that numArgs is ignored but args[] is used:
void PdfParser::opSetStrokeCMYKColor(Object args[], int /*numArgs*/)
{
  GfxColor color;

  state->setStrokePattern(NULL);
  state->setStrokeColorSpace(new GfxDeviceCMYKColorSpace());
  for (int i = 0; i < 4; ++i) {
    color.c[i] = dblToCol(args[i].getNum());
  }
  state->setStrokeColor(&color);
  builder->updateStyle(state);
}

// TODO not good that numArgs is ignored but args[] is used:
void PdfParser::opSetFillRGBColor(Object args[], int /*numArgs*/)
{
  GfxColor color;

  state->setFillPattern(NULL);
  state->setFillColorSpace(new GfxDeviceRGBColorSpace());
  for (int i = 0; i < 3; ++i) {
    color.c[i] = dblToCol(args[i].getNum());
  }
  state->setFillColor(&color);
  builder->updateStyle(state);
}

// TODO not good that numArgs is ignored but args[] is used:
void PdfParser::opSetStrokeRGBColor(Object args[], int /*numArgs*/) {
  GfxColor color;

  state->setStrokePattern(NULL);
  state->setStrokeColorSpace(new GfxDeviceRGBColorSpace());
  for (int i = 0; i < 3; ++i) {
    color.c[i] = dblToCol(args[i].getNum());
  }
  state->setStrokeColor(&color);
  builder->updateStyle(state);
}

// TODO not good that numArgs is ignored but args[] is used:
void PdfParser::opSetFillColorSpace(Object args[], int numArgs)
{
  assert(numArgs >= 1);
  GfxColorSpace *colorSpace = lookupColorSpaceCopy(args[0]);

  state->setFillPattern(nullptr);

  if (colorSpace) {
  GfxColor color;
    state->setFillColorSpace(colorSpace);
    colorSpace->getDefaultColor(&color);
    state->setFillColor(&color);
    builder->updateStyle(state);
  } else {
    error(errSyntaxError, getPos(), "Bad color space (fill)");
  }
}

// TODO not good that numArgs is ignored but args[] is used:
void PdfParser::opSetStrokeColorSpace(Object args[], int numArgs)
{
  assert(numArgs >= 1);
  GfxColorSpace *colorSpace = lookupColorSpaceCopy(args[0]);

  state->setStrokePattern(nullptr);

  if (colorSpace) {
    GfxColor color;
    state->setStrokeColorSpace(colorSpace);
    colorSpace->getDefaultColor(&color);
    state->setStrokeColor(&color);
    builder->updateStyle(state);
  } else {
    error(errSyntaxError, getPos(), "Bad color space (stroke)");
  }
}

void PdfParser::opSetFillColor(Object args[], int numArgs) {
  GfxColor color;
  int i;

  if (numArgs != state->getFillColorSpace()->getNComps()) {
    error(errSyntaxError, getPos(), "Incorrect number of arguments in 'sc' command");
    return;
  }
  state->setFillPattern(NULL);
  for (i = 0; i < numArgs; ++i) {
    color.c[i] = dblToCol(args[i].getNum());
  }
  state->setFillColor(&color);
  builder->updateStyle(state);
}

void PdfParser::opSetStrokeColor(Object args[], int numArgs) {
  GfxColor color;
  int i;

  if (numArgs != state->getStrokeColorSpace()->getNComps()) {
    error(errSyntaxError, getPos(), "Incorrect number of arguments in 'SC' command");
    return;
  }
  state->setStrokePattern(NULL);
  for (i = 0; i < numArgs; ++i) {
    color.c[i] = dblToCol(args[i].getNum());
  }
  state->setStrokeColor(&color);
  builder->updateStyle(state);
}

void PdfParser::opSetFillColorN(Object args[], int numArgs) {
  GfxColor color;
  int i;

  if (state->getFillColorSpace()->getMode() == csPattern) {
    if (numArgs > 1) {
      if (!((GfxPatternColorSpace *)state->getFillColorSpace())->getUnder() ||
	  numArgs - 1 != ((GfxPatternColorSpace *)state->getFillColorSpace())
	                     ->getUnder()->getNComps()) {
	error(errSyntaxError, getPos(), "Incorrect number of arguments in 'scn' command");
	return;
      }
      for (i = 0; i < numArgs - 1 && i < gfxColorMaxComps; ++i) {
	if (args[i].isNum()) {
	  color.c[i] = dblToCol(args[i].getNum());
	}
      }
      state->setFillColor(&color);
      builder->updateStyle(state);
    }
    GfxPattern *pattern;
#if defined(POPPLER_EVEN_NEWER_COLOR_SPACE_API)
    if (args[numArgs-1].isName() &&
	(pattern = res->lookupPattern(args[numArgs-1].getName(), NULL, NULL))) {
      state->setFillPattern(pattern);
      builder->updateStyle(state);
    }
#else
    if (args[numArgs-1].isName() &&
	(pattern = res->lookupPattern(args[numArgs-1].getName(), NULL))) {
      state->setFillPattern(pattern);
      builder->updateStyle(state);
    }
#endif

  } else {
    if (numArgs != state->getFillColorSpace()->getNComps()) {
      error(errSyntaxError, getPos(), "Incorrect number of arguments in 'scn' command");
      return;
    }
    state->setFillPattern(NULL);
    for (i = 0; i < numArgs && i < gfxColorMaxComps; ++i) {
      if (args[i].isNum()) {
	color.c[i] = dblToCol(args[i].getNum());
      }
    }
    state->setFillColor(&color);
    builder->updateStyle(state);
  }
}

void PdfParser::opSetStrokeColorN(Object args[], int numArgs) {
  GfxColor color;
  int i;

  if (state->getStrokeColorSpace()->getMode() == csPattern) {
    if (numArgs > 1) {
      if (!((GfxPatternColorSpace *)state->getStrokeColorSpace())
	       ->getUnder() ||
	  numArgs - 1 != ((GfxPatternColorSpace *)state->getStrokeColorSpace())
	                     ->getUnder()->getNComps()) {
	error(errSyntaxError, getPos(), "Incorrect number of arguments in 'SCN' command");
	return;
      }
      for (i = 0; i < numArgs - 1 && i < gfxColorMaxComps; ++i) {
	if (args[i].isNum()) {
	  color.c[i] = dblToCol(args[i].getNum());
	}
      }
      state->setStrokeColor(&color);
      builder->updateStyle(state);
    }
    GfxPattern *pattern;
#if defined(POPPLER_EVEN_NEWER_COLOR_SPACE_API)
    if (args[numArgs-1].isName() &&
	(pattern = res->lookupPattern(args[numArgs-1].getName(), NULL, NULL))) {
      state->setStrokePattern(pattern);
      builder->updateStyle(state);
    }
#else
    if (args[numArgs-1].isName() &&
	(pattern = res->lookupPattern(args[numArgs-1].getName(), NULL))) {
      state->setStrokePattern(pattern);
      builder->updateStyle(state);
    }
#endif

  } else {
    if (numArgs != state->getStrokeColorSpace()->getNComps()) {
      error(errSyntaxError, getPos(), "Incorrect number of arguments in 'SCN' command");
      return;
    }
    state->setStrokePattern(NULL);
    for (i = 0; i < numArgs && i < gfxColorMaxComps; ++i) {
      if (args[i].isNum()) {
	color.c[i] = dblToCol(args[i].getNum());
      }
    }
    state->setStrokeColor(&color);
    builder->updateStyle(state);
  }
}

//------------------------------------------------------------------------
// path segment operators
//------------------------------------------------------------------------

// TODO not good that numArgs is ignored but args[] is used:
void PdfParser::opMoveTo(Object args[], int /*numArgs*/)
{
  state->moveTo(args[0].getNum(), args[1].getNum());
}

// TODO not good that numArgs is ignored but args[] is used:
void PdfParser::opLineTo(Object args[], int /*numArgs*/)
{
  if (!state->isCurPt()) {
    error(errSyntaxError, getPos(), "No current point in lineto");
    return;
  }
  state->lineTo(args[0].getNum(), args[1].getNum());
}

// TODO not good that numArgs is ignored but args[] is used:
void PdfParser::opCurveTo(Object args[], int /*numArgs*/)
{
  if (!state->isCurPt()) {
    error(errSyntaxError, getPos(), "No current point in curveto");
    return;
  }
  double x1 = args[0].getNum();
  double y1 = args[1].getNum();
  double x2 = args[2].getNum();
  double y2 = args[3].getNum();
  double x3 = args[4].getNum();
  double y3 = args[5].getNum();
  state->curveTo(x1, y1, x2, y2, x3, y3);
}

// TODO not good that numArgs is ignored but args[] is used:
void PdfParser::opCurveTo1(Object args[], int /*numArgs*/)
{
  if (!state->isCurPt()) {
    error(errSyntaxError, getPos(), "No current point in curveto1");
    return;
  }
  double x1 = state->getCurX();
  double y1 = state->getCurY();
  double x2 = args[0].getNum();
  double y2 = args[1].getNum();
  double x3 = args[2].getNum();
  double y3 = args[3].getNum();
  state->curveTo(x1, y1, x2, y2, x3, y3);
}

// TODO not good that numArgs is ignored but args[] is used:
void PdfParser::opCurveTo2(Object args[], int /*numArgs*/)
{
  if (!state->isCurPt()) {
    error(errSyntaxError, getPos(), "No current point in curveto2");
    return;
  }
  double x1 = args[0].getNum();
  double y1 = args[1].getNum();
  double x2 = args[2].getNum();
  double y2 = args[3].getNum();
  double x3 = x2;
  double y3 = y2;
  state->curveTo(x1, y1, x2, y2, x3, y3);
}

// TODO not good that numArgs is ignored but args[] is used:
void PdfParser::opRectangle(Object args[], int /*numArgs*/)
{
  double x = args[0].getNum();
  double y = args[1].getNum();
  double w = args[2].getNum();
  double h = args[3].getNum();
  state->moveTo(x, y);
  state->lineTo(x + w, y);
  state->lineTo(x + w, y + h);
  state->lineTo(x, y + h);
  state->closePath();
}

void PdfParser::opClosePath(Object /*args*/[], int /*numArgs*/)
{
  if (!state->isCurPt()) {
    error(errSyntaxError, getPos(), "No current point in closepath");
    return;
  }
  state->closePath();
}

//------------------------------------------------------------------------
// path painting operators
//------------------------------------------------------------------------

void PdfParser::opEndPath(Object /*args*/[], int /*numArgs*/)
{
  doEndPath();
}

void PdfParser::opStroke(Object /*args*/[], int /*numArgs*/)
{
  if (!state->isCurPt()) {
    //error(getPos(), const_cast<char*>("No path in stroke"));
    return;
  }
  if (state->isPath()) {
    if (state->getStrokeColorSpace()->getMode() == csPattern &&
        !builder->isPatternTypeSupported(state->getStrokePattern())) {
          doPatternStrokeFallback();
    } else {
      builder->addPath(state, false, true);
    }
  }
  doEndPath();
}

void PdfParser::opCloseStroke(Object * /*args[]*/, int /*numArgs*/) {
  if (!state->isCurPt()) {
    //error(getPos(), const_cast<char*>("No path in closepath/stroke"));
    return;
  }
  state->closePath();
  if (state->isPath()) {
    if (state->getStrokeColorSpace()->getMode() == csPattern &&
        !builder->isPatternTypeSupported(state->getStrokePattern())) {
      doPatternStrokeFallback();
    } else {
      builder->addPath(state, false, true);
    }
  }
  doEndPath();
}

void PdfParser::opFill(Object /*args*/[], int /*numArgs*/)
{
  if (!state->isCurPt()) {
    //error(getPos(), const_cast<char*>("No path in fill"));
    return;
  }
  if (state->isPath()) {
    if (state->getFillColorSpace()->getMode() == csPattern &&
        !builder->isPatternTypeSupported(state->getFillPattern())) {
      doPatternFillFallback(gFalse);
    } else {
      builder->addPath(state, true, false);
    }
  }
  doEndPath();
}

void PdfParser::opEOFill(Object /*args*/[], int /*numArgs*/)
{
  if (!state->isCurPt()) {
    //error(getPos(), const_cast<char*>("No path in eofill"));
    return;
  }
  if (state->isPath()) {
    if (state->getFillColorSpace()->getMode() == csPattern &&
        !builder->isPatternTypeSupported(state->getFillPattern())) {
      doPatternFillFallback(gTrue);
    } else {
      builder->addPath(state, true, false, true);
    }
  }
  doEndPath();
}

void PdfParser::opFillStroke(Object /*args*/[], int /*numArgs*/)
{
  if (!state->isCurPt()) {
    //error(getPos(), const_cast<char*>("No path in fill/stroke"));
    return;
  }
  if (state->isPath()) {
    doFillAndStroke(gFalse);
  } else {
    builder->addPath(state, true, true);
  }
  doEndPath();
}

void PdfParser::opCloseFillStroke(Object /*args*/[], int /*numArgs*/)
{
  if (!state->isCurPt()) {
    //error(getPos(), const_cast<char*>("No path in closepath/fill/stroke"));
    return;
  }
  if (state->isPath()) {
    state->closePath();
    doFillAndStroke(gFalse);
  }
  doEndPath();
}

void PdfParser::opEOFillStroke(Object /*args*/[], int /*numArgs*/)
{
  if (!state->isCurPt()) {
    //error(getPos(), const_cast<char*>("No path in eofill/stroke"));
    return;
  }
  if (state->isPath()) {
    doFillAndStroke(gTrue);
  }
  doEndPath();
}

void PdfParser::opCloseEOFillStroke(Object /*args*/[], int /*numArgs*/)
{
  if (!state->isCurPt()) {
    //error(getPos(), const_cast<char*>("No path in closepath/eofill/stroke"));
    return;
  }
  if (state->isPath()) {
    state->closePath();
    doFillAndStroke(gTrue);
  }
  doEndPath();
}

void PdfParser::doFillAndStroke(GBool eoFill) {
    GBool fillOk = gTrue, strokeOk = gTrue;
    if (state->getFillColorSpace()->getMode() == csPattern &&
        !builder->isPatternTypeSupported(state->getFillPattern())) {
        fillOk = gFalse;
    }
    if (state->getStrokeColorSpace()->getMode() == csPattern &&
        !builder->isPatternTypeSupported(state->getStrokePattern())) {
        strokeOk = gFalse;
    }
    if (fillOk && strokeOk) {
        builder->addPath(state, true, true, eoFill);
    } else {
        doPatternFillFallback(eoFill);
        doPatternStrokeFallback();
    }
}

void PdfParser::doPatternFillFallback(GBool eoFill) {
  GfxPattern *pattern;

  if (!(pattern = state->getFillPattern())) {
    return;
  }
  switch (pattern->getType()) {
  case 1:
    break;
  case 2:
    doShadingPatternFillFallback(static_cast<GfxShadingPattern *>(pattern), gFalse, eoFill);
    break;
  default:
    error(errUnimplemented, getPos(), "Unimplemented pattern type (%d) in fill",
	  pattern->getType());
    break;
  }
}

void PdfParser::doPatternStrokeFallback() {
  GfxPattern *pattern;

  if (!(pattern = state->getStrokePattern())) {
    return;
  }
  switch (pattern->getType()) {
  case 1:
    break;
  case 2:
    doShadingPatternFillFallback(static_cast<GfxShadingPattern *>(pattern), gTrue, gFalse);
    break;
  default:
    error(errUnimplemented, getPos(), "Unimplemented pattern type ({0:d}) in stroke",
	  pattern->getType());
    break;
  }
}

void PdfParser::doShadingPatternFillFallback(GfxShadingPattern *sPat,
                                             GBool stroke, GBool eoFill) {
  GfxShading *shading;
  GfxPath *savedPath;
  const double *ctm, *btm, *ptm;
  double m[6], ictm[6], m1[6];
  double xMin, yMin, xMax, yMax;
  double det;

  shading = sPat->getShading();

  // save current graphics state
  savedPath = state->getPath()->copy();
  saveState();

  // clip to bbox
  if (0 ){//shading->getHasBBox()) {
    shading->getBBox(&xMin, &yMin, &xMax, &yMax);
    state->moveTo(xMin, yMin);
    state->lineTo(xMax, yMin);
    state->lineTo(xMax, yMax);
    state->lineTo(xMin, yMax);
    state->closePath();
    state->clip();
    //builder->clip(state);
    state->setPath(savedPath->copy());
  }

  // clip to current path
  if (stroke) {
    state->clipToStrokePath();
    //out->clipToStrokePath(state);
  } else {
    state->clip();
    if (eoFill) {
      builder->setClipPath(state, true);
    } else {
      builder->setClipPath(state);
    }
  }

  // set the color space
  state->setFillColorSpace(shading->getColorSpace()->copy());

  // background color fill
  if (shading->getHasBackground()) {
    state->setFillColor(shading->getBackground());
    builder->addPath(state, true, false);
  }
  state->clearPath();

  // construct a (pattern space) -> (current space) transform matrix
  ctm = state->getCTM();
  btm = baseMatrix;
  ptm = sPat->getMatrix();
  // iCTM = invert CTM
  det = 1 / (ctm[0] * ctm[3] - ctm[1] * ctm[2]);
  ictm[0] = ctm[3] * det;
  ictm[1] = -ctm[1] * det;
  ictm[2] = -ctm[2] * det;
  ictm[3] = ctm[0] * det;
  ictm[4] = (ctm[2] * ctm[5] - ctm[3] * ctm[4]) * det;
  ictm[5] = (ctm[1] * ctm[4] - ctm[0] * ctm[5]) * det;
  // m1 = PTM * BTM = PTM * base transform matrix
  m1[0] = ptm[0] * btm[0] + ptm[1] * btm[2];
  m1[1] = ptm[0] * btm[1] + ptm[1] * btm[3];
  m1[2] = ptm[2] * btm[0] + ptm[3] * btm[2];
  m1[3] = ptm[2] * btm[1] + ptm[3] * btm[3];
  m1[4] = ptm[4] * btm[0] + ptm[5] * btm[2] + btm[4];
  m1[5] = ptm[4] * btm[1] + ptm[5] * btm[3] + btm[5];
  // m = m1 * iCTM = (PTM * BTM) * (iCTM)
  m[0] = m1[0] * ictm[0] + m1[1] * ictm[2];
  m[1] = m1[0] * ictm[1] + m1[1] * ictm[3];
  m[2] = m1[2] * ictm[0] + m1[3] * ictm[2];
  m[3] = m1[2] * ictm[1] + m1[3] * ictm[3];
  m[4] = m1[4] * ictm[0] + m1[5] * ictm[2] + ictm[4];
  m[5] = m1[4] * ictm[1] + m1[5] * ictm[3] + ictm[5];

  // set the new matrix
  state->concatCTM(m[0], m[1], m[2], m[3], m[4], m[5]);
  builder->setTransform(m[0], m[1], m[2], m[3], m[4], m[5]);

  // do shading type-specific operations
  switch (shading->getType()) {
  case 1:
    doFunctionShFill(static_cast<GfxFunctionShading *>(shading));
    break;
  case 2:
  case 3:
    // no need to implement these
    break;
  case 4:
  case 5:
    doGouraudTriangleShFill(static_cast<GfxGouraudTriangleShading *>(shading));
    break;
  case 6:
  case 7:
    doPatchMeshShFill(static_cast<GfxPatchMeshShading *>(shading));
    break;
  }

  // restore graphics state
  restoreState();
  state->setPath(savedPath);
}

// TODO not good that numArgs is ignored but args[] is used:
void PdfParser::opShFill(Object args[], int /*numArgs*/)
{
  GfxShading *shading = 0;
  GfxPath *savedPath = NULL;
  double xMin, yMin, xMax, yMax;
  double xTemp, yTemp;
  double gradientTransform[6];
  double *matrix = NULL;
  GBool savedState = gFalse;

#if defined(POPPLER_EVEN_NEWER_COLOR_SPACE_API)
  if (!(shading = res->lookupShading(args[0].getName(), NULL, state))) {
    return;
  }
#else
  if (!(shading = res->lookupShading(args[0].getName(), NULL))) {
    return;
  }
#endif

  // save current graphics state
  if (shading->getType() != 2 && shading->getType() != 3) {
    savedPath = state->getPath()->copy();
    saveState();
    savedState = gTrue;
  } else {  // get gradient transform if possible
      // check proper operator sequence
      // first there should be one W(*) and then one 'cm' somewhere before 'sh'
      GBool seenClip, seenConcat;
      seenClip = (clipHistory->getClipPath() != NULL);
      seenConcat = gFalse;
      int i = 1;
      while (i <= maxOperatorHistoryDepth) {
        const char *opName = getPreviousOperator(i);
        if (!strcmp(opName, "cm")) {
          if (seenConcat) {   // more than one 'cm'
            break;
          } else {
            seenConcat = gTrue;
          }
        }
        i++;
      }

      if (seenConcat && seenClip) {
        if (builder->getTransform(gradientTransform)) {
          matrix = (double*)&gradientTransform;
          builder->setTransform(1.0, 0.0, 0.0, 1.0, 0.0, 0.0);  // remove transform
        }
      }
  }

  // clip to bbox
  if (shading->getHasBBox()) {
    shading->getBBox(&xMin, &yMin, &xMax, &yMax);
    if (matrix != NULL) {
        xTemp = matrix[0]*xMin + matrix[2]*yMin + matrix[4];
        yTemp = matrix[1]*xMin + matrix[3]*yMin + matrix[5];
        state->moveTo(xTemp, yTemp);
        xTemp = matrix[0]*xMax + matrix[2]*yMin + matrix[4];
        yTemp = matrix[1]*xMax + matrix[3]*yMin + matrix[5];
        state->lineTo(xTemp, yTemp);
        xTemp = matrix[0]*xMax + matrix[2]*yMax + matrix[4];
        yTemp = matrix[1]*xMax + matrix[3]*yMax + matrix[5];
        state->lineTo(xTemp, yTemp);
        xTemp = matrix[0]*xMin + matrix[2]*yMax + matrix[4];
        yTemp = matrix[1]*xMin + matrix[3]*yMax + matrix[5];
        state->lineTo(xTemp, yTemp);
    }
    else {
        state->moveTo(xMin, yMin);
        state->lineTo(xMax, yMin);
        state->lineTo(xMax, yMax);
        state->lineTo(xMin, yMax);
    }
    state->closePath();
    state->clip();
    if (savedState)
      builder->setClipPath(state);
    else
      builder->clip(state);
    state->clearPath();
  }

  // set the color space
  if (savedState)
    state->setFillColorSpace(shading->getColorSpace()->copy());

  // do shading type-specific operations
  switch (shading->getType()) {
  case 1:
    doFunctionShFill(static_cast<GfxFunctionShading *>(shading));
    break;
  case 2:
  case 3:
    if (clipHistory->getClipPath()) {
      builder->addShadedFill(shading, matrix, clipHistory->getClipPath(),
                             clipHistory->getClipType() == clipEO ? true : false);
    }
    break;
  case 4:
  case 5:
    doGouraudTriangleShFill(static_cast<GfxGouraudTriangleShading *>(shading));
    break;
  case 6:
  case 7:
    doPatchMeshShFill(static_cast<GfxPatchMeshShading *>(shading));
    break;
  }

  // restore graphics state
  if (savedState) {
    restoreState();
    state->setPath(savedPath);
  }

  delete shading;
}

void PdfParser::doFunctionShFill(GfxFunctionShading *shading) {
  double x0, y0, x1, y1;
  GfxColor colors[4];

  shading->getDomain(&x0, &y0, &x1, &y1);
  shading->getColor(x0, y0, &colors[0]);
  shading->getColor(x0, y1, &colors[1]);
  shading->getColor(x1, y0, &colors[2]);
  shading->getColor(x1, y1, &colors[3]);
  doFunctionShFill1(shading, x0, y0, x1, y1, colors, 0);
}

void PdfParser::doFunctionShFill1(GfxFunctionShading *shading,
			    double x0, double y0,
			    double x1, double y1,
			    GfxColor *colors, int depth) {
  GfxColor fillColor;
  GfxColor color0M, color1M, colorM0, colorM1, colorMM;
  GfxColor colors2[4];
  double functionColorDelta = colorDeltas[pdfFunctionShading-1];
  const double *matrix;
  double xM, yM;
  int nComps, i, j;

  nComps = shading->getColorSpace()->getNComps();
  matrix = shading->getMatrix();

  // compare the four corner colors
  for (i = 0; i < 4; ++i) {
    for (j = 0; j < nComps; ++j) {
      if (abs(colors[i].c[j] - colors[(i+1)&3].c[j]) > functionColorDelta) {
	break;
      }
    }
    if (j < nComps) {
      break;
    }
  }

  // center of the rectangle
  xM = 0.5 * (x0 + x1);
  yM = 0.5 * (y0 + y1);

  // the four corner colors are close (or we hit the recursive limit)
  // -- fill the rectangle; but require at least one subdivision
  // (depth==0) to avoid problems when the four outer corners of the
  // shaded region are the same color
  if ((i == 4 && depth > 0) || depth == maxDepths[pdfFunctionShading-1]) {

    // use the center color
    shading->getColor(xM, yM, &fillColor);
    state->setFillColor(&fillColor);

    // fill the rectangle
    state->moveTo(x0 * matrix[0] + y0 * matrix[2] + matrix[4],
		  x0 * matrix[1] + y0 * matrix[3] + matrix[5]);
    state->lineTo(x1 * matrix[0] + y0 * matrix[2] + matrix[4],
		  x1 * matrix[1] + y0 * matrix[3] + matrix[5]);
    state->lineTo(x1 * matrix[0] + y1 * matrix[2] + matrix[4],
		  x1 * matrix[1] + y1 * matrix[3] + matrix[5]);
    state->lineTo(x0 * matrix[0] + y1 * matrix[2] + matrix[4],
		  x0 * matrix[1] + y1 * matrix[3] + matrix[5]);
    state->closePath();
    builder->addPath(state, true, false);
    state->clearPath();

  // the four corner colors are not close enough -- subdivide the
  // rectangle
  } else {

    // colors[0]       colorM0       colors[2]
    //   (x0,y0)       (xM,y0)       (x1,y0)
    //         +----------+----------+
    //         |          |          |
    //         |    UL    |    UR    |
    // color0M |       colorMM       | color1M
    // (x0,yM) +----------+----------+ (x1,yM)
    //         |       (xM,yM)       |
    //         |    LL    |    LR    |
    //         |          |          |
    //         +----------+----------+
    // colors[1]       colorM1       colors[3]
    //   (x0,y1)       (xM,y1)       (x1,y1)

    shading->getColor(x0, yM, &color0M);
    shading->getColor(x1, yM, &color1M);
    shading->getColor(xM, y0, &colorM0);
    shading->getColor(xM, y1, &colorM1);
    shading->getColor(xM, yM, &colorMM);

    // upper-left sub-rectangle
    colors2[0] = colors[0];
    colors2[1] = color0M;
    colors2[2] = colorM0;
    colors2[3] = colorMM;
    doFunctionShFill1(shading, x0, y0, xM, yM, colors2, depth + 1);
    
    // lower-left sub-rectangle
    colors2[0] = color0M;
    colors2[1] = colors[1];
    colors2[2] = colorMM;
    colors2[3] = colorM1;
    doFunctionShFill1(shading, x0, yM, xM, y1, colors2, depth + 1);
    
    // upper-right sub-rectangle
    colors2[0] = colorM0;
    colors2[1] = colorMM;
    colors2[2] = colors[2];
    colors2[3] = color1M;
    doFunctionShFill1(shading, xM, y0, x1, yM, colors2, depth + 1);

    // lower-right sub-rectangle
    colors2[0] = colorMM;
    colors2[1] = colorM1;
    colors2[2] = color1M;
    colors2[3] = colors[3];
    doFunctionShFill1(shading, xM, yM, x1, y1, colors2, depth + 1);
  }
}

void PdfParser::doGouraudTriangleShFill(GfxGouraudTriangleShading *shading) {
  double x0, y0, x1, y1, x2, y2;
  GfxColor color0, color1, color2;
  int i;

  for (i = 0; i < shading->getNTriangles(); ++i) {
    shading->getTriangle(i, &x0, &y0, &color0,
			 &x1, &y1, &color1,
			 &x2, &y2, &color2);
    gouraudFillTriangle(x0, y0, &color0, x1, y1, &color1, x2, y2, &color2,
			shading->getColorSpace()->getNComps(), 0);
  }
}

void PdfParser::gouraudFillTriangle(double x0, double y0, GfxColor *color0,
			      double x1, double y1, GfxColor *color1,
			      double x2, double y2, GfxColor *color2,
			      int nComps, int depth) {
  double x01, y01, x12, y12, x20, y20;
  double gouraudColorDelta = colorDeltas[pdfGouraudTriangleShading-1];
  GfxColor color01, color12, color20;
  int i;

  for (i = 0; i < nComps; ++i) {
    if (abs(color0->c[i] - color1->c[i]) > gouraudColorDelta ||
       abs(color1->c[i] - color2->c[i]) > gouraudColorDelta) {
      break;
    }
  }
  if (i == nComps || depth == maxDepths[pdfGouraudTriangleShading-1]) {
    state->setFillColor(color0);
    state->moveTo(x0, y0);
    state->lineTo(x1, y1);
    state->lineTo(x2, y2);
    state->closePath();
    builder->addPath(state, true, false);
    state->clearPath();
  } else {
    x01 = 0.5 * (x0 + x1);
    y01 = 0.5 * (y0 + y1);
    x12 = 0.5 * (x1 + x2);
    y12 = 0.5 * (y1 + y2);
    x20 = 0.5 * (x2 + x0);
    y20 = 0.5 * (y2 + y0);
    //~ if the shading has a Function, this should interpolate on the
    //~ function parameter, not on the color components
    for (i = 0; i < nComps; ++i) {
      color01.c[i] = (color0->c[i] + color1->c[i]) / 2;
      color12.c[i] = (color1->c[i] + color2->c[i]) / 2;
      color20.c[i] = (color2->c[i] + color0->c[i]) / 2;
    }
    gouraudFillTriangle(x0, y0, color0, x01, y01, &color01,
			x20, y20, &color20, nComps, depth + 1);
    gouraudFillTriangle(x01, y01, &color01, x1, y1, color1,
			x12, y12, &color12, nComps, depth + 1);
    gouraudFillTriangle(x01, y01, &color01, x12, y12, &color12,
			x20, y20, &color20, nComps, depth + 1);
    gouraudFillTriangle(x20, y20, &color20, x12, y12, &color12,
			x2, y2, color2, nComps, depth + 1);
  }
}

void PdfParser::doPatchMeshShFill(GfxPatchMeshShading *shading) {
  int start, i;

  if (shading->getNPatches() > 128) {
    start = 3;
  } else if (shading->getNPatches() > 64) {
    start = 2;
  } else if (shading->getNPatches() > 16) {
    start = 1;
  } else {
    start = 0;
  }
  for (i = 0; i < shading->getNPatches(); ++i) {
    fillPatch((GfxPatch*)shading->getPatch(i), shading->getColorSpace()->getNComps(),
	      start);
  }
}

void PdfParser::fillPatch(GfxPatch *patch, int nComps, int depth) {
  GfxPatch patch00 = blankPatch();
  GfxPatch patch01 = blankPatch();
  GfxPatch patch10 = blankPatch();
  GfxPatch patch11 = blankPatch();
  GfxColor color = {{0}};
  double xx[4][8];
  double yy[4][8];
  double xxm;
  double yym;
  double patchColorDelta = colorDeltas[pdfPatchMeshShading - 1];

  int i;

  for (i = 0; i < nComps; ++i) {
    if (abs(patch->color[0][0].c[i] - patch->color[0][1].c[i])
	  > patchColorDelta ||
	abs(patch->color[0][1].c[i] - patch->color[1][1].c[i])
	  > patchColorDelta ||
	abs(patch->color[1][1].c[i] - patch->color[1][0].c[i])
	  > patchColorDelta ||
	abs(patch->color[1][0].c[i] - patch->color[0][0].c[i])
	  > patchColorDelta) {
      break;
    }
    color.c[i] = GfxColorComp(patch->color[0][0].c[i]);
  }
  if (i == nComps || depth == maxDepths[pdfPatchMeshShading-1]) {
    state->setFillColor(&color);
    state->moveTo(patch->x[0][0], patch->y[0][0]);
    state->curveTo(patch->x[0][1], patch->y[0][1],
		   patch->x[0][2], patch->y[0][2],
		   patch->x[0][3], patch->y[0][3]);
    state->curveTo(patch->x[1][3], patch->y[1][3],
		   patch->x[2][3], patch->y[2][3],
		   patch->x[3][3], patch->y[3][3]);
    state->curveTo(patch->x[3][2], patch->y[3][2],
		   patch->x[3][1], patch->y[3][1],
		   patch->x[3][0], patch->y[3][0]);
    state->curveTo(patch->x[2][0], patch->y[2][0],
		   patch->x[1][0], patch->y[1][0],
		   patch->x[0][0], patch->y[0][0]);
    state->closePath();
    builder->addPath(state, true, false);
    state->clearPath();
  } else {
    for (i = 0; i < 4; ++i) {
      xx[i][0] = patch->x[i][0];
      yy[i][0] = patch->y[i][0];
      xx[i][1] = 0.5 * (patch->x[i][0] + patch->x[i][1]);
      yy[i][1] = 0.5 * (patch->y[i][0] + patch->y[i][1]);
      xxm = 0.5 * (patch->x[i][1] + patch->x[i][2]);
      yym = 0.5 * (patch->y[i][1] + patch->y[i][2]);
      xx[i][6] = 0.5 * (patch->x[i][2] + patch->x[i][3]);
      yy[i][6] = 0.5 * (patch->y[i][2] + patch->y[i][3]);
      xx[i][2] = 0.5 * (xx[i][1] + xxm);
      yy[i][2] = 0.5 * (yy[i][1] + yym);
      xx[i][5] = 0.5 * (xxm + xx[i][6]);
      yy[i][5] = 0.5 * (yym + yy[i][6]);
      xx[i][3] = xx[i][4] = 0.5 * (xx[i][2] + xx[i][5]);
      yy[i][3] = yy[i][4] = 0.5 * (yy[i][2] + yy[i][5]);
      xx[i][7] = patch->x[i][3];
      yy[i][7] = patch->y[i][3];
    }
    for (i = 0; i < 4; ++i) {
      patch00.x[0][i] = xx[0][i];
      patch00.y[0][i] = yy[0][i];
      patch00.x[1][i] = 0.5 * (xx[0][i] + xx[1][i]);
      patch00.y[1][i] = 0.5 * (yy[0][i] + yy[1][i]);
      xxm = 0.5 * (xx[1][i] + xx[2][i]);
      yym = 0.5 * (yy[1][i] + yy[2][i]);
      patch10.x[2][i] = 0.5 * (xx[2][i] + xx[3][i]);
      patch10.y[2][i] = 0.5 * (yy[2][i] + yy[3][i]);
      patch00.x[2][i] = 0.5 * (patch00.x[1][i] + xxm);
      patch00.y[2][i] = 0.5 * (patch00.y[1][i] + yym);
      patch10.x[1][i] = 0.5 * (xxm + patch10.x[2][i]);
      patch10.y[1][i] = 0.5 * (yym + patch10.y[2][i]);
      patch00.x[3][i] = 0.5 * (patch00.x[2][i] + patch10.x[1][i]);
      patch00.y[3][i] = 0.5 * (patch00.y[2][i] + patch10.y[1][i]);
      patch10.x[0][i] = patch00.x[3][i];
      patch10.y[0][i] = patch00.y[3][i];
      patch10.x[3][i] = xx[3][i];
      patch10.y[3][i] = yy[3][i];
    }
    for (i = 4; i < 8; ++i) {
      patch01.x[0][i-4] = xx[0][i];
      patch01.y[0][i-4] = yy[0][i];
      patch01.x[1][i-4] = 0.5 * (xx[0][i] + xx[1][i]);
      patch01.y[1][i-4] = 0.5 * (yy[0][i] + yy[1][i]);
      xxm = 0.5 * (xx[1][i] + xx[2][i]);
      yym = 0.5 * (yy[1][i] + yy[2][i]);
      patch11.x[2][i-4] = 0.5 * (xx[2][i] + xx[3][i]);
      patch11.y[2][i-4] = 0.5 * (yy[2][i] + yy[3][i]);
      patch01.x[2][i-4] = 0.5 * (patch01.x[1][i-4] + xxm);
      patch01.y[2][i-4] = 0.5 * (patch01.y[1][i-4] + yym);
      patch11.x[1][i-4] = 0.5 * (xxm + patch11.x[2][i-4]);
      patch11.y[1][i-4] = 0.5 * (yym + patch11.y[2][i-4]);
      patch01.x[3][i-4] = 0.5 * (patch01.x[2][i-4] + patch11.x[1][i-4]);
      patch01.y[3][i-4] = 0.5 * (patch01.y[2][i-4] + patch11.y[1][i-4]);
      patch11.x[0][i-4] = patch01.x[3][i-4];
      patch11.y[0][i-4] = patch01.y[3][i-4];
      patch11.x[3][i-4] = xx[3][i];
      patch11.y[3][i-4] = yy[3][i];
    }
    //~ if the shading has a Function, this should interpolate on the
    //~ function parameter, not on the color components
    for (i = 0; i < nComps; ++i) {
      patch00.color[0][0].c[i] = patch->color[0][0].c[i];
      patch00.color[0][1].c[i] = (patch->color[0][0].c[i] +
				  patch->color[0][1].c[i]) / 2;
      patch01.color[0][0].c[i] = patch00.color[0][1].c[i];
      patch01.color[0][1].c[i] = patch->color[0][1].c[i];
      patch01.color[1][1].c[i] = (patch->color[0][1].c[i] +
				  patch->color[1][1].c[i]) / 2;
      patch11.color[0][1].c[i] = patch01.color[1][1].c[i];
      patch11.color[1][1].c[i] = patch->color[1][1].c[i];
      patch11.color[1][0].c[i] = (patch->color[1][1].c[i] +
				  patch->color[1][0].c[i]) / 2;
      patch10.color[1][1].c[i] = patch11.color[1][0].c[i];
      patch10.color[1][0].c[i] = patch->color[1][0].c[i];
      patch10.color[0][0].c[i] = (patch->color[1][0].c[i] +
				  patch->color[0][0].c[i]) / 2;
      patch00.color[1][0].c[i] = patch10.color[0][0].c[i];
      patch00.color[1][1].c[i] = (patch00.color[1][0].c[i] +
				  patch01.color[1][1].c[i]) / 2;
      patch01.color[1][0].c[i] = patch00.color[1][1].c[i];
      patch11.color[0][0].c[i] = patch00.color[1][1].c[i];
      patch10.color[0][1].c[i] = patch00.color[1][1].c[i];
    }
    fillPatch(&patch00, nComps, depth + 1);
    fillPatch(&patch10, nComps, depth + 1);
    fillPatch(&patch01, nComps, depth + 1);
    fillPatch(&patch11, nComps, depth + 1);
  }
}

void PdfParser::doEndPath() {
	static double square = 0;

	double cur_square = 0;
	double xMin, yMin, xMax, yMax;
  if (state->isCurPt() && clip != clipNone) {
    state->clip();
    if (clip == clipNormal) {
      clipHistory->setClip((GfxPath*)state->getPath(), clipNormal);
      builder->clip(state);
    } else {
      clipHistory->setClip((GfxPath*)state->getPath(), clipEO);
      builder->clip(state, true);
    }
  }
  state->getClipBBox(&xMin, &yMin, &xMax, &yMax);
  cur_square = (xMax - xMin) * (yMax - yMin);
  if (square < cur_square) {
    square = cur_square;
    backgroundCandidat = builder->getContainer();
  }
  clip = clipNone;
  state->clearPath();
}

//------------------------------------------------------------------------
// path clipping operators
//------------------------------------------------------------------------

void PdfParser::opClip(Object /*args*/[], int /*numArgs*/)
{
  clip = clipNormal;
}

void PdfParser::opEOClip(Object /*args*/[], int /*numArgs*/)
{
  clip = clipEO;
}

//------------------------------------------------------------------------
// text object operators
//------------------------------------------------------------------------

void PdfParser::opBeginText(Object /*args*/[], int /*numArgs*/)
{
  state->setTextMat(1, 0, 0, 1, 0, 0);
  state->textMoveTo(0, 0);
  builder->updateTextPosition(0.0, 0.0);
  fontChanged = gTrue;
  builder->beginTextObject(state);
}

void PdfParser::opEndText(Object /*args*/[], int /*numArgs*/)
{
  builder->endTextObject(state);
}

//------------------------------------------------------------------------
// text state operators
//------------------------------------------------------------------------

// TODO not good that numArgs is ignored but args[] is used:
void PdfParser::opSetCharSpacing(Object args[], int /*numArgs*/)
{
  state->setCharSpace(args[0].getNum());
}

bool checkExcludeListChars(char c) {
	Inkscape::Preferences *prefs = Inkscape::Preferences::get();
	static Glib::ustring charList = prefs->getString("/options/svgoutput/fontnameexcludechars");

	if (charList.size() == 0 ) {
		charList = Glib::ustring(",");
	}
	for(int i = 0; i < charList.size(); i++) {
		if (charList[i] == c) return true;
	}
	return false;
}

bool checkExcludeListCharsStatic(char c, const char* charList = ",") {
	Inkscape::Preferences *prefs = Inkscape::Preferences::get();

	for(int i = 0; i < strlen(charList); i++) {
		if (charList[i] == c) return true;
	}
	return false;
}

char* prepareFamilyName(const char *fontName, bool encode) {
	  if (fontName) {
		CURL *curl = curl_easy_init();
		GooString *fontName2= new GooString(fontName);
		if (sp_font_postfix_sh) {
			//fontName2->append("-");
			fontName2->append(sp_font_postfix_sh);
		}
		// format file name
		for(int strPos = 0; strPos < fontName2->getLength(); strPos++) {
		  while(checkExcludeListChars(fontName2->getChar(strPos))){
			  fontName2->del(strPos, 1);
		  }
		  if (fontName2->getChar(strPos) == '+') {
			  fontName2->del(0, strPos+1);
			  strPos = 0;
		  }
		}
		char *encodeName;
		if (encode)
			encodeName = curl_easy_escape(curl, fontName2->c_str(), 0);
		else
			encodeName = g_strdup(fontName2->c_str());

	    delete(fontName2);
	    return encodeName;
	  } else {
		  return 0;
	  }
}

char* prepareFileFontName(const char *fontName, bool isCIDFont) {
	char *fontExt;
	  if ( isCIDFont ) {
		  fontExt = g_strdup_printf("%s", "cff");
	  } else {
		  fontExt = g_strdup_printf("%s", "ttf");
	  }
	  if (fontName) {
		GooString *gooFileName= new GooString(fontName);

		for(int strPos = 0; strPos < gooFileName->getLength(); strPos++) {
			while(checkExcludeListCharsStatic(gooFileName->getChar(strPos), " ")){
				gooFileName->del(strPos, 1);
			}
		}

		char *preparedFontName = g_strdup(gooFileName->c_str());

		delete(gooFileName);

		char *fname = prepareFamilyName(preparedFontName);
	    char *fnameEx = g_strdup_printf("%s%s.%s", sp_export_svg_path_sh, fname, fontExt);



	    free(preparedFontName);
	    free(fontExt);
	    free(fname);
	    return fnameEx;
	  } else {
		  free(fontExt);
		  return 0;
	  }
}

void* exportFontStatic(void *args)
{
	RecExportFont *argRec = (RecExportFont *)args;
	argRec->parser->exportFont(argRec->font, argRec);
	argRec->status = 0;
	return 0;
}

void PdfParser::exportFontAsync(GfxFont *font, bool async)
{
	  // put font to passed list
	  if (font->getID() && font->getID()->num >= 0) {
		  //If we are doing export for font with same name we must wait.
		  if (async) {
			  for(int fontThredN = 0; fontThredN < exportFontThreads->len; fontThredN++) {
				  void *p;
				  RecExportFont *param = (RecExportFont *) g_ptr_array_index(exportFontThreads, fontThredN);
				  if (strlen(param->fontName) && font->getName() && font->getName()->getLength() > 0) {
					  char *paramFontFile = prepareFileFontName(param->fontName, param->isCIDFont);
					  char *arrFontFile = prepareFileFontName(font->getName()->c_str(), font->isCIDFont());
					  if (strcmp(paramFontFile, arrFontFile) == 0){
						  pthread_join(param->thredID, &p);
					  }
					  free(paramFontFile);
					  free(arrFontFile);
				  }
			  }
		  }
		  g_ptr_array_add(savedFontsList, font);
		  RecExportFont *params = ( RecExportFont *) malloc(sizeof(RecExportFont));
		  g_ptr_array_add(exportFontThreads, params);
		  params->parser = this;
		  params->font = font;
		  params->fontName = g_strdup(font->getName()->getCString());
		  params->isCIDFont = font->isCIDFont();
		  params->buf = font->readEmbFontFile(xref, &params->len);
		  params->ctu = (void*)font->getToUnicode();
		  params->status = 1;

		  if (async) {
			  pthread_create(&params->thredID, NULL, exportFontStatic, params);
		  } else{
			  exportFontStatic(params);
		  }
	  } else return;
}

inline bool isExistFile (const std::string& name) {
  struct stat buffer;
  return (stat (name.c_str(), &buffer) == 0);
}

// THIS PART IS INAPPRIATE FOR HARDCODE - but I can't fund better solution
// Linker can't find this function  CharCodeToUnicode::mapToUnicode in libpoppler
// so i implement it here
struct CharCodeToUnicodeString
{
    CharCode c;
    Unicode *u;
    int len;
};

int CharCodeToUnicode::mapToUnicode(CharCode c, Unicode const **u) const
{
    int i;

    if (isIdentity) {
        map[0] = (Unicode)c;
        *u = map;
        return 1;
    }
    if (c >= mapLen) {
        return 0;
    }
    if (map[c]) {
        *u = &map[c];
        return 1;
    }
    for (i = sMapLen - 1; i >= 0; --i) { // in reverse so CMap takes precedence
        if (sMap[i].c == c) {
            *u = sMap[i].u;
            return sMap[i].len;
        }
    }
    return 0;
}

// xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx

void PdfParser::exportFont(GfxFont *font, RecExportFont *args)
{
	int len;
	const char *fname;
	static int num = 0;
	const char *fontName = args ? args->fontName : font->getName()->getCString();
	bool isCIDFont = args ? args->isCIDFont : font->isCIDFont();
	char *buf = args ? args->buf : font->readEmbFontFile(xref, &len);
	if (args) len = args->len;
	const CharCodeToUnicode *ctu = args ? (CharCodeToUnicode *)args->ctu : font->getToUnicode();

	CURL *curl = curl_easy_init();
	char *fontExt;
	  if ((args && args->isCIDFont) || ((! args) && isCIDFont)) {
		  fontExt = g_strdup_printf("%s", "cff");
	  } else {
		  fontExt = g_strdup_printf("%s", "ttf");
	  }
	  if (fontName) {
	    fname = prepareFileFontName(fontName, args->isCIDFont);
	  }
	  else {
		fname = g_strdup_printf("%s_unnamed%i", builder->getDocName(), num);
		char * encodeName = curl_easy_escape(curl, fname, 0);
	    //free(fname);
		fname = g_strdup_printf("%s%s.%s", sp_export_svg_path_sh, encodeName, fontExt);
		free(encodeName);
	  }
	  num++;


	  if (isExistFile(fname) == false && len > 0) {
		  char exeDir[1024];
		  FILE *fl = fopen(fname, "w");
		  fwrite(buf, 1, len, fl);
		  fclose(fl);
		  if (fontName) {
			  // get path to inkscape
			  readlink("/proc/self/exe", exeDir, 1024);
			  while(exeDir[strlen(exeDir) - 1 ] != '/') {
				  exeDir[strlen(exeDir) - 1 ] = 0;
			  }
			  //static int noname_num = 0; // conter of nemed of noname fonts
			  //GooString *fontName2 = args ? new GooString(args->fontName) : fontName2 = new GooString(font->getName());

			  const char *originalFontName;
			  if (args) {
				  originalFontName = args->fontName;
			  }
			  else {
				  originalFontName = font->getName()->c_str();
			  }
			  GooString *fontName2 = new GooString(prepareFamilyName(originalFontName, false));

		      /*if (sp_font_postfix_sh) {
				fontName2->append("-");
				fontName2->append(sp_font_postfix_sh);
			  }
			  // format font name
			  for(int strPos = 0; strPos < fontName2->getLength(); strPos++) {
				  if (fontName2->getChar(strPos) == '-'){
					  //fontName2->setChar(strPos, ' ');
				  }
				  if (fontName2->getChar(strPos) == '+') {
					  fontName2->del(0, strPos+1);
					  strPos = 0;
				  }
			  }*/


			  // Generate MAP file
			 if (ctu) {

				  int mapLen = ctu->getLength();
				  const Unicode *u;
				  //make JSON map file
				  char * mapFileName = g_strdup_printf("%s.map", fname);
				  FILE *fMap = fopen(mapFileName, "w");
				  free(mapFileName);
				  char buff[256];
#define write_map_file(F, STR)  sprintf(buff, STR);  fwrite(buff, 1, strlen(buff), F);
				  write_map_file(fMap, "{\n");
				  //=============== bbox of font =============
				  FT_Library    ft_lib;
				  FT_Error      error;
				  FT_Face       face;
				  FT_Init_FreeType(&ft_lib);
				  error = FT_New_Memory_Face(ft_lib, (FT_Byte*)buf, len, 0, &face);
				  //error = FT_Set_Char_Size(face, 0, 320 << 6, 0, 300);
				  FT_Set_Pixel_Sizes(face, 0, 10000);
				  write_map_file(fMap, "\"font\" : {\n");

				  write_map_file(fMap, "    \"yMax\" : ");
				  sprintf(buff, "%i,\n", (int)face->bbox.yMax);
				  fwrite(buff, 1, strlen(buff), fMap);

				  write_map_file(fMap, "    \"xMax\" : ");
				  sprintf(buff, "%i\n", (int)face->bbox.yMax);
				  fwrite(buff, 1, strlen(buff), fMap);

				  write_map_file(fMap, "},\n");
				  //=============== write map array  ============
				  sprintf(buff, "\"uniMap\" : [\n");
				  fwrite(buff, 1, strlen(buff), fMap);

				  bool jsonArrayStarted = false;
				  for(int i = 0; i < mapLen; i++) {
					  if (ctu->mapToUnicode((CharCode)i, (Unicode const **)&u)) {
						 if (jsonArrayStarted) {
							buff[0] = ','; buff[1] = 0;
							fwrite(buff, 1, strlen(buff), fMap);
						 }
						 sprintf(buff, "{\"%i\" : { \"uni\" : %u ", i, *u);
						 fwrite(buff, 1, strlen(buff), fMap);
						 jsonArrayStarted = true;

						 if (FT_Load_Glyph(face, (FT_UInt)i, FT_LOAD_NO_BITMAP) == 0) {
					    	 sprintf(buff, ",\n         \"width\" : %u,\n", (uint)face->glyph->metrics.width);
					    	 fwrite(buff, 1, strlen(buff), fMap);
					    	 sprintf(buff, "         \"hAdvance\" : %u,\n", (uint)face->glyph->metrics.horiAdvance);
					    	 fwrite(buff, 1, strlen(buff), fMap);
					    	 sprintf(buff, "         \"xAdvance\" : %u,\n", (uint)face->glyph->advance.x >> 6);
					    	 fwrite(buff, 1, strlen(buff), fMap);
					    	 sprintf(buff, "         \"hBx\" : %i,\n", (uint)face->glyph->metrics.horiBearingX);
					    	 fwrite(buff, 1, strlen(buff), fMap);
					    	 sprintf(buff, "         \"hBy\" : %i", (uint)face->glyph->metrics.horiBearingY);
		                     fwrite(buff, 1, strlen(buff), fMap);
					     }
						 write_map_file(fMap, "} }\n");
					  }
				  };

				  sprintf(buff, "]\n");
				  fwrite(buff, 1, strlen(buff), fMap);
				  sprintf(buff, "}\n");
				  fwrite(buff, 1, strlen(buff), fMap);
				  fclose(fMap);
			  }

			  gchar *fontForgeCmd;
			  if (isCIDFont) {
				  char *cidName = g_strdup(fname);
				  g_ptr_array_add(cidFontList, cidName);
				  fontForgeCmd = g_strdup_printf("fontforge -script %schageFontName.pe %s \"%s\" \"%s\" 2>/dev/null",
				  								exeDir, // path to script, without name
				  								fname,  // name of TTF file for path
												fontName, //postscriptname
				  								fontName2->c_str()); //family
			  } else {
			     // generate command for path names inside TTF file
				  fontForgeCmd = g_strdup_printf("%sfontAdapter.py %s \"%s\" \"%s\" 2>/dev/null",
								exeDir, // path to script, without name
								fname,  // name of TTF file for path
								fontName2->c_str(), //fontName, //postscriptname
								fontName2->c_str()); //family

			  }

			  char *inBuff;
			  FILE *f = popen(fontForgeCmd,"r");
			  static int errcount = 0;

			  while ( f && (inBuff = readLineFromFile(f)) != nullptr ) {
				  if (sp_log_font_sh) {
					  // if it formated message
					  if (strncmp(inBuff, "FFE-", 4) == 0) {
						  char *errcode = strndup(inBuff, 7);
						  char *attrName = g_strdup_printf("data-E%i%s", errcount, errcode);
						  builder->getRoot()->setAttribute(attrName, inBuff);
						  free(attrName);

						  attrName = g_strdup_printf("data-E%i%sex", errcount++, errcode);
						  builder->getRoot()->setAttribute(attrName, fontName2->c_str());
						  free(attrName);
						  free(errcode);
					  }
					  if (inBuff) free(inBuff);
				  }
			  }

			  delete(fontName2);
			  g_free(fontForgeCmd);
		  }
	  }
	  curl_free((void*)fname);
	  free(fontExt);
	  free(buf);
	  free (curl);
#undef write_map_file
}



// TODO not good that numArgs is ignored but args[] is used:
void PdfParser::opSetFont(Object args[], int /*numArgs*/)
{
  int len;
  char *fname;
  static int num = 0;

  GfxFont *font = res->lookupFont(args[0].getName());

  if (!font) {
    // unsetting the font (drawing no text) is better than using the
    // previous one and drawing random glyphs from it
    state->setFont(NULL, args[1].getNum());
    fontChanged = gTrue;
    return;
  }
  if (printCommands) {
    printf("  font: tag=%s name='%s' %g\n",
	   font->getTag().c_str(),
	   font->getName() ? font->getName()->c_str() : "???",
	   args[1].getNum());
    fflush(stdout);
  }

  font->incRefCnt();
  state->setFont(font, args[1].getNum());

  // calculate comfortable space width
  const CharCodeToUnicode *ctu = font->getToUnicode();
  if (ctu) {
	  builder->spaceWidth = 0;
	  for(unsigned char i = 1; i < ctu->getLength() && i != 0 && builder->spaceWidth == 0; i++) {
		  Unicode *u;
		  ctu->mapToUnicode((CharCode)i, (Unicode const **)&u);
		  if (*u == 32) {
			  if (font->isCIDFont()) {
				  builder->spaceWidth = ((GfxCIDFont *)font)->getWidth((char *)&i, 1) * state->getFontSize();
			  } else {
				  builder->spaceWidth = ((Gfx8BitFont *)font)->getWidth(i) *  state->getFontSize();
			  }

		  }
	  }
  } else {
	  builder->spaceWidth = 0.3 * state->getFontSize();
  }

  if (sp_export_fonts_sh  && font->getName() && font->getName()->getLength() > 0)
  {
	  // Save font file
	  // if we have saved this file. We do not do it again
	  bool alreadyDone = false;
	  for(int cntFnt = 0; cntFnt < savedFontsList->len; cntFnt++) {
		  GfxFont *tmpFont = (GfxFont *)g_ptr_array_index(savedFontsList, cntFnt);
		  // if this font stream already extracted
		  if (tmpFont == font) {
			  alreadyDone = true;
			  break;
		  }
	  }

	  if (! alreadyDone) {
			  exportFontAsync(font, false);
			  //exportFont(font);
      }
  }

  fontChanged = gTrue;
}

// TODO not good that numArgs is ignored but args[] is used:
void PdfParser::opSetTextLeading(Object args[], int /*numArgs*/)
{
  state->setLeading(args[0].getNum());
}

// TODO not good that numArgs is ignored but args[] is used:
void PdfParser::opSetTextRender(Object args[], int /*numArgs*/)
{
  state->setRender(args[0].getInt());
  builder->updateStyle(state);
}

// TODO not good that numArgs is ignored but args[] is used:
void PdfParser::opSetTextRise(Object args[], int /*numArgs*/)
{
  state->setRise(args[0].getNum());
}

// TODO not good that numArgs is ignored but args[] is used:
void PdfParser::opSetWordSpacing(Object args[], int /*numArgs*/)
{
  state->setWordSpace(args[0].getNum());
}

// TODO not good that numArgs is ignored but args[] is used:
void PdfParser::opSetHorizScaling(Object args[], int /*numArgs*/)
{
  state->setHorizScaling(args[0].getNum());
  builder->updateTextMatrix(state);
  fontChanged = gTrue;
}

//------------------------------------------------------------------------
// text positioning operators
//------------------------------------------------------------------------

// TODO not good that numArgs is ignored but args[] is used:
void PdfParser::opTextMove(Object args[], int /*numArgs*/)
{
  double tx, ty;

  tx = state->getLineX() + args[0].getNum();
  ty = state->getLineY() + args[1].getNum();
  state->textMoveTo(tx, ty);
  builder->updateTextPosition(tx, ty);
}

// TODO not good that numArgs is ignored but args[] is used:
void PdfParser::opTextMoveSet(Object args[], int /*numArgs*/)
{
  double tx, ty;

  tx = state->getLineX() + args[0].getNum();
  ty = args[1].getNum();
  state->setLeading(-ty);
  ty += state->getLineY();
  state->textMoveTo(tx, ty);
  builder->updateTextPosition(tx, ty);
}

// TODO not good that numArgs is ignored but args[] is used:
void PdfParser::opSetTextMatrix(Object args[], int /*numArgs*/)
{
  state->setTextMat(args[0].getNum(), args[1].getNum(),
		    args[2].getNum(), args[3].getNum(),
		    args[4].getNum(), args[5].getNum());
  state->textMoveTo(0, 0);
  builder->updateTextMatrix(state);
  builder->updateTextPosition(0.0, 0.0);
  fontChanged = gTrue;
}

void PdfParser::opTextNextLine(Object /*args*/[], int /*numArgs*/)
{
  double tx, ty;

  tx = state->getLineX();
  ty = state->getLineY() - state->getLeading();
  state->textMoveTo(tx, ty);
  builder->updateTextPosition(tx, ty);
}

//------------------------------------------------------------------------
// text string operators
//------------------------------------------------------------------------

// TODO not good that numArgs is ignored but args[] is used:
void PdfParser::opShowText(Object args[], int /*numArgs*/)
{
  if (!state->getFont()) {
    error(errSyntaxError, getPos(), "No font in show");
    return;
  }
  if (fontChanged) {
    builder->updateFont(state);
    fontChanged = gFalse;
  }
  doShowText((GooString*)args[0].getString());
}

// TODO not good that numArgs is ignored but args[] is used:
void PdfParser::opMoveShowText(Object args[], int /*numArgs*/)
{
  double tx = 0;
  double ty = 0;

  if (!state->getFont()) {
    error(errSyntaxError, getPos(), "No font in move/show");
    return;
  }
  if (fontChanged) {
    builder->updateFont(state);
    fontChanged = gFalse;
  }
  tx = state->getLineX();
  ty = state->getLineY() - state->getLeading();
  state->textMoveTo(tx, ty);
  builder->updateTextPosition(tx, ty);
  doShowText((GooString*)args[0].getString());
}

// TODO not good that numArgs is ignored but args[] is used:
void PdfParser::opMoveSetShowText(Object args[], int /*numArgs*/)
{
  double tx = 0;
  double ty = 0;

  if (!state->getFont()) {
    error(errSyntaxError, getPos(), "No font in move/set/show");
    return;
  }
  if (fontChanged) {
    builder->updateFont(state);
    fontChanged = gFalse;
  }
  state->setWordSpace(args[0].getNum());
  state->setCharSpace(args[1].getNum());
  tx = state->getLineX();
  ty = state->getLineY() - state->getLeading();
  state->textMoveTo(tx, ty);
  builder->updateTextPosition(tx, ty);
  doShowText((GooString*)args[2].getString());
}

// TODO not good that numArgs is ignored but args[] is used:
void PdfParser::opShowSpaceText(Object args[], int /*numArgs*/)
{
  Array *a = 0;
  Object obj;
  int wMode = 0;

  if (!state->getFont()) {
    error(errSyntaxError, getPos(), "No font in show/space");
    return;
  }
  if (fontChanged) {
    builder->updateFont(state);
    fontChanged = gFalse;
  }
  wMode = state->getFont()->getWMode();
  a = args[0].getArray();
  for (int i = 0; i < a->getLength(); ++i) {
	  obj = a->get(i);
    if (obj.isNum()) {
      // this uses the absolute value of the font size to match
      // Acrobat's behavior
      if (wMode) {
	state->textShift(0, -obj.getNum() * 0.001 *
			    fabs(state->getFontSize()));
      } else {
	state->textShift(-obj.getNum() * 0.001 *
			 fabs(state->getFontSize()), 0);
      }
      builder->updateTextShift(state, obj.getNum());
    } else if (obj.isString()) {
      doShowText(obj.getString());
    } else {
      error(errSyntaxError, getPos(), "Element of show/space array must be number or string");
    }
    //obj.free();
  }
}

static void scaleByFontSize(GfxState *state, double &dx, double  &dy, double  &originX, double  &originY, bool isSpace ) {
	GfxFont *font = state->getFont();
    if (font->getWMode()) {
		dx *= state->getFontSize();
		dy = dy * state->getFontSize() + state->getCharSpace();
		if (isSpace) {
		  dy += state->getWordSpace();
		}
    } else {
		dx = dx * state->getFontSize() + state->getCharSpace();
		if (isSpace) {
		  dx += state->getWordSpace();
		}
		dx *= state->getHorizScaling();
		dy *= state->getFontSize();
    }
    originX *= state->getFontSize();
    originY *= state->getFontSize();
}

static bool isEmptyLine(const GooString *s, const char *p, int charLen) {
  int _len = s->getLength();
  const char *_p = s->c_str();
  bool onlySpase = true;
  uint _currentCode = 0;
  for(int ii = 0; ii < charLen; ii++) {
	  _currentCode = ((_currentCode << 8) & 0xFF) + p[ii];
  }
  while(_len > 0) {
	  uint _code = 0;
	  for(int ii = 0; ii < charLen; ii++) {
		  _code = ((_code << 8) & 0xFF) + _p[ii];
	  }
	  if (_code != _currentCode) onlySpase = false;
	  _p += charLen;
	  _len -= charLen;
  }
  return onlySpase;
}

void PdfParser::replaceFromActulaHidenText(Unicode **u, CharCode &code) {
	static Unicode uActual = 0; // memory must exist after out
	actualMarkerPosition++;
	if (actualtextString->getLength() >= (actualMarkerPosition * 2 + 2)) {
		Unicode uCopy;
		if (*u != nullptr)
			uCopy = **u;
		else
			uCopy = code;//0x20;
		const CharCode codeCopy = code;
		uActual = 0;
		*u = &uActual;
		((char*)*u)[0] = actualtextString->c_str()[actualMarkerPosition * 2 + 1];
		((char*)*u)[1] = actualtextString->c_str()[actualMarkerPosition * 2];
		if (uActual < 32) uActual = 32;
		code = uActual;

		// in-designer some times add invisible bullet point in PDF so after export we have dual bullet
		// we are checking glyph for this symbol and if it is empty change it to space or remove(if tspan keep only one symbol)
		if (code == 0x2022) {
			char* glyph = builder->glyphToPath(state, uCopy, codeCopy);
			if (strlen(glyph) == 0) {
				if (codeCopy == 32 && uCopy == (Unicode)32) {
					**u = uCopy;
					code = codeCopy;
				} else {
					**u = (Unicode)32;
				}
			}
			gfree(glyph);
		}
	}
}

void specialReplacesOfPDFChars(const GooString *s, Unicode *u, int n) {
    // change 0x1F as bullet point. if string consist of only one char - (s->getLength() == n)
	if (u && (*u == 0x2002))
	{
		*u = (Unicode)0x0020;
	}
    if (sp_bullet_point1f_sh && u && (*u == 0x1F) && (s->getLength() == n)) {
  	  *u = (Unicode)0x2022;
    } else {
        // is not printable symbol
        // maybe no the best solution write it directly to map table
  	  if (u && *u < 0x20)
			  *u = (Unicode)0x20;
    }
}

void PdfParser::doShowText(const GooString *s) {
  GfxFont *font;
  int wMode;
  double riseX, riseY;
  CharCode code;
  Unicode *u = nullptr;
  Unicode uActual = 0;
  double x, y, dx, dy, tdx, tdy;
  double originX, originY, tOriginX, tOriginY;
  double oldCTM[6], newCTM[6];
  const double *mat;
  Object charProc;
  Dict *resDict;
  Parser *oldParser;
  const char *p =nullptr;
  int len, n, uLen;

  font = state->getFont();
  wMode = font->getWMode();

  builder->beginString(state);

  // handle a Type 3 char
  if (font->getType() == fontType3 && 0) {//out->interpretType3Chars()) {
    mat = state->getCTM();
    for (int i = 0; i < 6; ++i) {
      oldCTM[i] = mat[i];
    }
    mat = state->getTextMat();
    newCTM[0] = mat[0] * oldCTM[0] + mat[1] * oldCTM[2];
    newCTM[1] = mat[0] * oldCTM[1] + mat[1] * oldCTM[3];
    newCTM[2] = mat[2] * oldCTM[0] + mat[3] * oldCTM[2];
    newCTM[3] = mat[2] * oldCTM[1] + mat[3] * oldCTM[3];
    mat = font->getFontMatrix();
    newCTM[0] = mat[0] * newCTM[0] + mat[1] * newCTM[2];
    newCTM[1] = mat[0] * newCTM[1] + mat[1] * newCTM[3];
    newCTM[2] = mat[2] * newCTM[0] + mat[3] * newCTM[2];
    newCTM[3] = mat[2] * newCTM[1] + mat[3] * newCTM[3];
    newCTM[0] *= state->getFontSize();
    newCTM[1] *= state->getFontSize();
    newCTM[2] *= state->getFontSize();
    newCTM[3] *= state->getFontSize();
    newCTM[0] *= state->getHorizScaling();
    newCTM[2] *= state->getHorizScaling();
    state->textTransformDelta(0, state->getRise(), &riseX, &riseY);
    double curX = state->getCurX();
    double curY = state->getCurY();
    double lineX = state->getLineX();
    double lineY = state->getLineY();
    oldParser = parser;
    p = g_strdup( s->c_str() );
    len = s->getLength();
    while (len > 0) {
      n = font->getNextChar(p, len, &code,
			    (const unsigned int**)&u, &uLen,  /* TODO: This looks like a memory leak for u. */
			    &dx, &dy, &originX, &originY);
      dx = dx * state->getFontSize() + state->getCharSpace();
      if (n == 1 && *p == ' ') {
    	  dx += state->getWordSpace();
      }
      dx *= state->getHorizScaling();
      dy *= state->getFontSize();
      state->textTransformDelta(dx, dy, &tdx, &tdy);
      state->transform(curX + riseX, curY + riseY, &x, &y);
      saveState();
      state->setCTM(newCTM[0], newCTM[1], newCTM[2], newCTM[3], x, y);
      //~ the CTM concat values here are wrong (but never used)
      //out->updateCTM(state, 1, 0, 0, 1, 0, 0);
      if (0){ /*!out->beginType3Char(state, curX + riseX, curY + riseY, tdx, tdy,
			       code, u, uLen)) {*/
    	charProc = ((Gfx8BitFont *)font)->getCharProc(code);
		if ((resDict = ((Gfx8BitFont *)font)->getResources())) {
		  pushResources(resDict);
		}
		if (charProc.isStream()) {
		  //parse(&charProc, gFalse); // TODO: parse into SVG font
		} else {
		  error(errSyntaxError, getPos(), "Missing or bad Type3 CharProc entry");
		}
		//out->endType3Char(state);
		if (resDict) {
		  popResources();
		}
		//charProc.free();
      }
      restoreState();
      // GfxState::restore() does *not* restore the current position,
      // so we deal with it here using (curX, curY) and (lineX, lineY)
      curX += tdx;
      curY += tdy;
      state->moveTo(curX, curY);
      state->textSetPos(lineX, lineY);
      p += n;
      len -= n;
    }
    //if (p != nullptr)
    //	free(p);
    parser = oldParser;

  } else { // is not fontType3
    state->textTransformDelta(0, state->getRise(), &riseX, &riseY);
    p = s->c_str();
    len = s->getLength();
    while (len > 0) {
      //const Unicode *uTmp = nullptr;
      n = font->getNextChar(p, len, &code,
			    (const Unicode **)&u, &uLen,  /* TODO: This looks like a memory leak for u. */
			    &dx, &dy, &originX, &originY);
      // stub for photoshop's PDF - it do not want set widths
      if (dx == 1 && creator && strstr(creator, "Adobe Photoshop"))
      {
    	  FT_GlyphSlot ftGlyph = builder->getFTGlyph(state->getFont(), state->getFontSize(), (uint)code, 10000);
    	  dx = ftGlyph->metrics.horiAdvance;// width + ftGlyph->metrics.horiBearingX;
    	  dx = dx / 10000;
      }

      if (actualMarkerBegin)
    	  replaceFromActulaHidenText(&u, code);
      /* todo: Garbage collector
      if (u && (*u < 256) && sp_mapping_off_sh && p[0] &&
    		  strcmp(font->getTag()->getCString(), "TT3") &&
			  strcmp(font->getTag()->getCString(), "TT5")) {
    	  *u = p[0];
      }*/
      specialReplacesOfPDFChars(s, (unsigned int*)u, n);
      if (u && printCommands) {
		  printf("%04x ", *u);
		  fflush(stdout);
      }
      //try remove span with ZERO WITCH SPACE only
      if (u  && *u == 8203) {
    	  if (isEmptyLine(s, p, n)) break;
      }

      scaleByFontSize(state, dx, dy, originX, originY, /*is space */(n == 1 && *p == ' '));

      state->textTransformDelta(dx, dy, &tdx, &tdy);
      state->textTransformDelta(originX, originY, &tOriginX, &tOriginY);

      if (sp_split_spec_sh && u && *u == 64257) {
    	  Unicode uniCode = 102;
    	  builder->addChar(state, state->getCurX() + riseX, state->getCurY() + riseY,
    	                         dx, dy, tOriginX, tOriginY, /*code*/102, n, &uniCode, uLen);

    	  uniCode = 105;
    	  builder->addChar(state, state->getCurX() + riseX, state->getCurY() + riseY,
    	      	                 dx, dy, tOriginX, tOriginY, /*code*/105, n, &uniCode, uLen);
      } else {
		  builder->addChar(state, state->getCurX() + riseX, state->getCurY() + riseY,
						   dx, dy, tOriginX, tOriginY, code, n, u, uLen);
      }
      state->shift(tdx, tdy);
      builder->glipEndX = state->getCurX();
      builder->glipEndY = state->getCurY();
      p += n;
      len -= n;
    }// END of while
    //	free(p);
  }

  builder->endString(state);
  //builder->updateStyle(state); // do start new text block.
}


//------------------------------------------------------------------------
// XObject operators
//------------------------------------------------------------------------

// TODO not good that numArgs is ignored but args[] is used:
void PdfParser::opXObject(Object args[], int /*numArgs*/)
{
  Object obj1, obj2, obj3, refObj;

  const char *name = args[0].getName();
  obj1 = res->lookupXObject(name);
  if (obj1.isNull()) {
    return;
  }
  if (!obj1.isStream()) {
    error(errSyntaxError, getPos(), "XObject '{0:s}' is wrong type", name);
    //obj1.free();
    return;
  }
  obj2 = obj1.streamGetDict()->lookup(const_cast<char*>("Subtype"));
  if (obj2.isName(const_cast<char*>("Image"))) {
	  refObj = res->lookupXObjectNF(name);
    doImage(&refObj, obj1.getStream(), gFalse);
    //refObj.free();
  } else if (obj2.isName(const_cast<char*>("Form"))) {
    doForm(&obj1);
  } else if (obj2.isName(const_cast<char*>("PS"))) {
	  obj3 = obj1.streamGetDict()->lookup(const_cast<char*>("Level1"));
/*    out->psXObject(obj1.getStream(),
    		   obj3.isStream() ? obj3.getStream() : (Stream *)NULL);*/
  } else if (obj2.isName()) {
    error(errSyntaxError, getPos(), "Unknown XObject subtype '{0:s}'", obj2.getName());
  } else {
    error(errSyntaxError, getPos(), "XObject subtype is missing or wrong type");
  }
  //obj2.free();
  //obj1.free();
}

void PdfParser::doImage(Object * /*ref*/, Stream *str, GBool inlineImg)
{   upTimer(timCREATE_IMAGE);
    Dict *dict;
    int width, height;
    int bits;
    GBool interpolate;
    StreamColorSpaceMode csMode;
    GBool mask;
    GBool invert;
    Object maskObj, smaskObj;
    GBool haveColorKeyMask, haveExplicitMask, haveSoftMask;
    GBool maskInvert;
    GBool maskInterpolate;
    Object obj1, obj2;
    
    // get info from the stream
    bits = 0;
    csMode = streamCSNone;
    str->getImageParams(&bits, &csMode);
    
    // get stream dict
    dict = str->getDict();
    
    // get size
    obj1 = dict->lookup(const_cast<char*>("Width"));
    if (obj1.isNull()) {
        //obj1.free();
        obj1 = dict->lookup(const_cast<char*>("W"));
    }
    if (obj1.isInt()){
        width = obj1.getInt();
    }
    else if (obj1.isReal()) {
        width = (int)obj1.getReal();
    }
    else {
        goto err2;
    }
    //obj1.free();
    obj1 = dict->lookup(const_cast<char*>("Height"));
    if (obj1.isNull()) {
        //obj1.free();
        obj1 = dict->lookup(const_cast<char*>("H"));
    }
    if (obj1.isInt()) {
        height = obj1.getInt();
    }
    else if (obj1.isReal()){
        height = static_cast<int>(obj1.getReal());
    }
    else {
        goto err2;
    }
    //obj1.free();
    
    // image interpolation
    obj1 = dict->lookup("Interpolate");
    if (obj1.isNull()) {
      //obj1.free();
      obj1 = dict->lookup("I");
    }
    if (obj1.isBool())
      interpolate = obj1.getBool();
    else
      interpolate = gFalse;
    //obj1.free();
    maskInterpolate = gFalse;

    // image or mask?
    obj1 = dict->lookup(const_cast<char*>("ImageMask"));
    if (obj1.isNull()) {
        //obj1.free();
        obj1 = dict->lookup(const_cast<char*>("IM"));
    }
    mask = gFalse;
    if (obj1.isBool()) {
        mask = obj1.getBool();
    }
    else if (!obj1.isNull()) {
        goto err2;
    }
    //obj1.free();
    
    // bit depth
    if (bits == 0) {
    	obj1 = dict->lookup(const_cast<char*>("BitsPerComponent"));
        if (obj1.isNull()) {
            //obj1.free();
            obj1 = dict->lookup(const_cast<char*>("BPC"));
        }
        if (obj1.isInt()) {
            bits = obj1.getInt();
        } else if (mask) {
            bits = 1;
        } else {
            goto err2;
        }
        //obj1.free();
    }
    
    // display a mask
    if (mask) {
        // check for inverted mask
        if (bits != 1) {
            goto err1;
        }
        invert = gFalse;
        obj1 = dict->lookup(const_cast<char*>("Decode"));
        if (obj1.isNull()) {
            //obj1.free();
        	obj1 = dict->lookup(const_cast<char*>("D"));
        }
        if (obj1.isArray()) {
        	obj2 = obj1.arrayGet(0);
            if (obj2.isInt() && obj2.getInt() == 1) {
                invert = gTrue;
            }
            //obj2.free();
        } else if (!obj1.isNull()) {
            goto err2;
        }
        //obj1.free();
        
        // draw it
        builder->addImageMask(state, str, width, height, invert, interpolate);
        
    } else {
        // get color space and color map
        GfxColorSpace *colorSpace;
        _POPPLER_CALL_ARGS(obj1, dict->lookup, "ColorSpace");
        if (obj1.isNull()) {
            _POPPLER_FREE(obj1);
            _POPPLER_CALL_ARGS(obj1, dict->lookup, "CS");
        }
        if (!obj1.isNull()) {
            colorSpace = lookupColorSpaceCopy(obj1);
        } else if (csMode == streamCSDeviceGray) {
            colorSpace = new GfxDeviceGrayColorSpace();
        } else if (csMode == streamCSDeviceRGB) {
            colorSpace = new GfxDeviceRGBColorSpace();
        } else if (csMode == streamCSDeviceCMYK) {
            colorSpace = new GfxDeviceCMYKColorSpace();
        } else {
            colorSpace = nullptr;
        }
        _POPPLER_FREE(obj1);
        if (!colorSpace) {
            goto err1;
        }
        obj1 = dict->lookup(const_cast<char*>("Decode"));
        if (obj1.isNull()) {
            //obj1.free();
        	obj1 = dict->lookup(const_cast<char*>("D"));
        }
        GfxImageColorMap *colorMap = new GfxImageColorMap(bits, &obj1, colorSpace);
        //obj1.free();
        if (!colorMap->isOk()) {
            delete colorMap;
            goto err1;
        }
        
        // get the mask
        int maskColors[2*gfxColorMaxComps];
        haveColorKeyMask = haveExplicitMask = haveSoftMask = gFalse;
        Stream *maskStr = NULL;
        int maskWidth = 0;
        int maskHeight = 0;
        maskInvert = gFalse;
        GfxImageColorMap *maskColorMap = NULL;
        maskObj = dict->lookup(const_cast<char*>("Mask"));
        smaskObj = dict->lookup(const_cast<char*>("SMask"));
        Dict* maskDict;
        if (smaskObj.isStream()) {
            // soft mask
            if (inlineImg) {
	            goto err1;
            }
            maskStr = smaskObj.getStream();
            maskDict = smaskObj.streamGetDict();
            obj1 = maskDict->lookup(const_cast<char*>("Width"));
            if (obj1.isNull()) {
        	    //obj1.free();
        	    obj1 = maskDict->lookup(const_cast<char*>("W"));
            }
            if (!obj1.isInt()) {
	            goto err2;
            }
            maskWidth = obj1.getInt();
            //obj1.free();
            obj1 = maskDict->lookup(const_cast<char*>("Height"));
            if (obj1.isNull()) {
	            //obj1.free();
	            obj1 = maskDict->lookup(const_cast<char*>("H"));
            }
            if (!obj1.isInt()) {
	            goto err2;
            }
            maskHeight = obj1.getInt();
            //obj1.free();
            obj1 = maskDict->lookup(const_cast<char*>("BitsPerComponent"));
            if (obj1.isNull()) {
        	    //obj1.free();
            	obj1 = maskDict->lookup(const_cast<char*>("BPC"));
            }
            if (!obj1.isInt()) {
	            goto err2;
            }
            int maskBits = obj1.getInt();
            //obj1.free();
            obj1 = maskDict->lookup("Interpolate");
	    if (obj1.isNull()) {
	      //obj1.free();
	    	obj1 = maskDict->lookup("I");
	    }
	    if (obj1.isBool())
	      maskInterpolate = obj1.getBool();
	    else
	      maskInterpolate = gFalse;
	    //obj1.free();
	    obj1 = maskDict->lookup(const_cast<char*>("ColorSpace"));
            if (obj1.isNull()) {
	            //obj1.free();
	            obj1 = maskDict->lookup(const_cast<char*>("CS"));
            }
            GfxColorSpace *maskColorSpace = lookupColorSpaceCopy(obj1);
            _POPPLER_FREE(obj1);
            if (!maskColorSpace || maskColorSpace->getMode() != csDeviceGray) {
                goto err1;
            }
            _POPPLER_CALL_ARGS(obj1, maskDict->lookup, "Decode");
            if (obj1.isNull()) {
                _POPPLER_FREE(obj1);
                _POPPLER_CALL_ARGS(obj1, maskDict->lookup, "D");
            }
            maskColorMap = new GfxImageColorMap(maskBits, &obj1, maskColorSpace);
            //obj1.free();
            if (!maskColorMap->isOk()) {
                delete maskColorMap;
                goto err1;
            }
            //~ handle the Matte entry
            haveSoftMask = gTrue;
        } else if (maskObj.isArray()) {
            // color key mask
            int i;
            for (i = 0; i < maskObj.arrayGetLength() && i < 2*gfxColorMaxComps; ++i) {
            	obj1 = maskObj.arrayGet(i);
                maskColors[i] = obj1.getInt();
                //obj1.free();
            }
              haveColorKeyMask = gTrue;
        } else if (maskObj.isStream()) {
            // explicit mask
            if (inlineImg) {
                goto err1;
            }
            maskStr = maskObj.getStream();
            maskDict = maskObj.streamGetDict();
            obj1 = maskDict->lookup(const_cast<char*>("Width"));
            if (obj1.isNull()) {
                //obj1.free();
                obj1 = maskDict->lookup(const_cast<char*>("W"));
            }
            if (!obj1.isInt()) {
                goto err2;
            }
            maskWidth = obj1.getInt();
            //obj1.free();
            obj1 = maskDict->lookup(const_cast<char*>("Height"));
            if (obj1.isNull()) {
                //obj1.free();
                obj1 = maskDict->lookup(const_cast<char*>("H"));
            }
            if (!obj1.isInt()) {
                goto err2;
            }
            maskHeight = obj1.getInt();
            //obj1.free();
            obj1 = maskDict->lookup(const_cast<char*>("ImageMask"));
            if (obj1.isNull()) {
                //obj1.free();
                obj1 = maskDict->lookup(const_cast<char*>("IM"));
            }
            if (!obj1.isBool() || !obj1.getBool()) {
                goto err2;
            }
            //obj1.free();
            obj1 = maskDict->lookup("Interpolate");
	    if (obj1.isNull()) {
	      //obj1.free();
	      obj1 = maskDict->lookup("I");
	    }
	    if (obj1.isBool())
	      maskInterpolate = obj1.getBool();
	    else
	      maskInterpolate = gFalse;
	    //obj1.free();
            maskInvert = gFalse;
            obj1 = maskDict->lookup(const_cast<char*>("Decode"));
            if (obj1.isNull()) {
                //obj1.free();
                obj1 = maskDict->lookup(const_cast<char*>("D"));
            }
            if (obj1.isArray()) {
            	obj2 = obj1.arrayGet(0);
                if (obj2.isInt() && obj2.getInt() == 1) {
                    maskInvert = gTrue;
                }
                //obj2.free();
            } else if (!obj1.isNull()) {
                goto err2;
            }
            //obj1.free();
            haveExplicitMask = gTrue;
        }
        
        // draw it
        if (haveSoftMask) {
	    builder->addSoftMaskedImage(state, str, width, height, colorMap, interpolate,
				maskStr, maskWidth, maskHeight, maskColorMap, maskInterpolate);
            delete maskColorMap;
        } else if (haveExplicitMask) {
 	    builder->addMaskedImage(state, str, width, height, colorMap, interpolate,
				maskStr, maskWidth, maskHeight, maskInvert, maskInterpolate);
        } else {
	    builder->addImage(state, str, width, height, colorMap, interpolate,
		        haveColorKeyMask ? maskColors : static_cast<int *>(NULL));
        }
        delete colorMap;
        
        //maskObj.free();
        //smaskObj.free();
    }
    incTimer(timCREATE_IMAGE);
    return;

 err2:
    //obj1.free();
 err1:
    error(errSyntaxError, getPos(), "Bad image parameters");
}

void PdfParser::doForm(Object *str) {
  Dict *dict;
  GBool transpGroup, isolated, knockout;
  GfxColorSpace *blendingColorSpace;
  Object matrixObj, bboxObj;
  double m[6], bbox[4];
  Object resObj;
  Dict *resDict;
  Object obj1, obj2, obj3;
  int i;

  // check for excessive recursion
  if (formDepth > 20) {
    return;
  }

  // get stream dict
  dict = str->streamGetDict();

  // check form type
  obj1 = dict->lookup(const_cast<char*>("FormType"));
  if (!(obj1.isNull() || (obj1.isInt() && obj1.getInt() == 1))) {
    error(errSyntaxError, getPos(), "Unknown form type");
  }
  //obj1.free();

  // get bounding box
  bboxObj = dict->lookup(const_cast<char*>("BBox"));
  if (!bboxObj.isArray()) {
    //bboxObj.free();
    error(errSyntaxError, getPos(), "Bad form bounding box");
    return;
  }
  for (i = 0; i < 4; ++i) {
	obj1 = bboxObj.arrayGet(i);
    bbox[i] = obj1.getNum();
    //obj1.free();
  }
  //bboxObj.free();

  // get matrix
  matrixObj = dict->lookup(const_cast<char*>("Matrix"));
  if (matrixObj.isArray()) {
    for (i = 0; i < 6; ++i) {
    	obj1 = matrixObj.arrayGet(i);
      m[i] = obj1.getNum();
      //obj1.free();
    }
  } else {
    m[0] = 1; m[1] = 0;
    m[2] = 0; m[3] = 1;
    m[4] = 0; m[5] = 0;
  }
  //matrixObj.free();

  // get resources
  resObj = dict->lookup(const_cast<char*>("Resources"));
  resDict = resObj.isDict() ? resObj.getDict() : (Dict *)NULL;

  // check for a transparency group
  transpGroup = isolated = knockout = gFalse;
  blendingColorSpace = NULL;
  obj1 = dict->lookup(const_cast<char*>("Group"));
  if (obj1.isDict()) {
	obj2 = obj1.dictLookup(const_cast<char*>("S"));
    if (obj2.isName(const_cast<char*>("Transparency"))) {
      transpGroup = gTrue;
      if (!_POPPLER_CALL_ARGS_DEREF(obj3, obj1.dictLookup, "CS").isNull()) {
	      blendingColorSpace = GfxColorSpace::parse(nullptr, &obj3, nullptr, state);
      }
      _POPPLER_FREE(obj3);
      if (_POPPLER_CALL_ARGS_DEREF(obj3, obj1.dictLookup, "I").isBool()) {
	      isolated = obj3.getBool();
      }
      _POPPLER_FREE(obj3);
      if (_POPPLER_CALL_ARGS_DEREF(obj3, obj1.dictLookup, "K").isBool()) {
	      knockout = obj3.getBool();
      }
      _POPPLER_FREE(obj3);
    }
    //obj2.free();
  }
  //obj1.free();

  // draw it
  ++formDepth;
  doForm1(str, resDict, m, bbox,
	  transpGroup, gFalse, blendingColorSpace, isolated, knockout);
  --formDepth;

  if (blendingColorSpace) {
    delete blendingColorSpace;
  }
  //resObj.free();
}

void PdfParser::doForm1(Object *str, Dict *resDict, double *matrix, double *bbox,
		  GBool transpGroup, GBool softMask,
		  GfxColorSpace *blendingColorSpace,
		  GBool isolated, GBool knockout,
		  GBool alpha, Function *transferFunc,
		  GfxColor *backdropColor) {
  Parser *oldParser;
  double oldBaseMatrix[6];
  int i;

  // push new resources on stack
  pushResources(resDict);

  // save current graphics state
  saveState();

  // kill any pre-existing path
  state->clearPath();

  if (softMask || transpGroup) {
    builder->clearSoftMask(state);
    builder->pushTransparencyGroup(state, bbox, blendingColorSpace, // state & blendingColorSpace rely do not used
                                   isolated, knockout, softMask);
  }

  // save current parser
  oldParser = parser;

  // set form transformation matrix
  state->concatCTM(matrix[0], matrix[1], matrix[2],
		   matrix[3], matrix[4], matrix[5]);
  builder->setTransform(matrix[0], matrix[1], matrix[2],
                        matrix[3], matrix[4], matrix[5]);

  // set form bounding box
  state->moveTo(bbox[0], bbox[1]);
  state->lineTo(bbox[2], bbox[1]);
  state->lineTo(bbox[2], bbox[3]);
  state->lineTo(bbox[0], bbox[3]);
  state->closePath();
  state->clip();
  clipHistory->setClip((GfxPath*)state->getPath());
  builder->clip(state);
  state->clearPath();

  if (softMask || transpGroup) {
	  // stub for multiplier/not linear filters
    if (state->getBlendMode() != gfxBlendNormal) {
    	if (! sp_map_drop_color_sh)
    		state->setBlendMode(gfxBlendNormal);
    }
    if (state->getFillOpacity() != 1) {
      builder->setGroupOpacity(state->getFillOpacity());
      state->setFillOpacity(1);
    }
    if (state->getStrokeOpacity() != 1) {
      state->setStrokeOpacity(1);
    }
  }

  // set new base matrix
  for (i = 0; i < 6; ++i) {
    oldBaseMatrix[i] = baseMatrix[i];
    baseMatrix[i] = state->getCTM()[i];
  }

  // draw the form
  parse(str, gFalse);

  // restore base matrix
  for (i = 0; i < 6; ++i) {
    baseMatrix[i] = oldBaseMatrix[i];
  }

  // restore parser
  parser = oldParser;

  if (softMask || transpGroup) {
      builder->popTransparencyGroup(state);
  }

  // restore graphics state
  restoreState();

  // pop resource stack
  popResources();

  if (softMask) {
    builder->setSoftMask(state, bbox, alpha, transferFunc, backdropColor);
  } else if (transpGroup) {
    builder->paintTransparencyGroup(state, bbox);
  }
  return;
}

//------------------------------------------------------------------------
// in-line image operators
//------------------------------------------------------------------------

void PdfParser::opBeginImage(Object /*args*/[], int /*numArgs*/)
{
  // build dict/stream
  Stream *str = buildImageStream();

  // display the image
  if (str) {
    doImage(NULL, str, gTrue);
  
    // skip 'EI' tag
    int c1 = str->getUndecodedStream()->getChar();
    int c2 = str->getUndecodedStream()->getChar();
    while (!(c1 == 'E' && c2 == 'I') && c2 != EOF) {
      c1 = c2;
      c2 = str->getUndecodedStream()->getChar();
    }
    delete str;
  }
}

Stream *PdfParser::buildImageStream() {
  Object dict;
  Object obj;
  char *key;
  Stream *str;

  // build dictionary
  //dict.initDict(xref);
  dict = Object(new Dict(xref));
  obj = parser->getObj();
  while (!obj.isCmd(const_cast<char*>("ID")) && !obj.isEOF()) {
    if (!obj.isName()) {
      error(errSyntaxError, getPos(), "Inline image dictionary key must be a name object");
      //obj.free();
    } else {
      Object obj2;
      key = copyString(obj.getName());

      obj2 = parser->getObj();
      if (obj2.isEOF() || obj2.isError()) {
	gfree(key);
	break;
      }
      dict.dictAdd(obj.getName(), std::move(obj));
    }
    obj = parser->getObj();
  }
  if (obj.isEOF()) {
    error(errSyntaxError, getPos(), "End of file in inline image");
    return NULL;
  }

  // make stream
  str = new EmbedStream(parser->getStream(), dict.copy(), gFalse, 0);
  str = str->addFilters(dict.getDict());

  return str;
}

void PdfParser::opImageData(Object /*args*/[], int /*numArgs*/)
{
  error(errInternal, getPos(), "Internal: got 'ID' operator");
}

void PdfParser::opEndImage(Object /*args*/[], int /*numArgs*/)
{
  error(errInternal, getPos(), "Internal: got 'EI' operator");
}

//------------------------------------------------------------------------
// type 3 font operators
//------------------------------------------------------------------------

void PdfParser::opSetCharWidth(Object /*args*/[], int /*numArgs*/)
{
}

void PdfParser::opSetCacheDevice(Object /*args*/[], int /*numArgs*/)
{
}

//------------------------------------------------------------------------
// compatibility operators
//------------------------------------------------------------------------

void PdfParser::opBeginIgnoreUndef(Object /*args*/[], int /*numArgs*/)
{
  ++ignoreUndef;
}

void PdfParser::opEndIgnoreUndef(Object /*args*/[], int /*numArgs*/)
{
  if (ignoreUndef > 0)
    --ignoreUndef;
}

//------------------------------------------------------------------------
// marked content operators
//------------------------------------------------------------------------

void PdfParser::opBeginMarkedContent(Object args[], int numArgs) {
  if (printCommands) {
    printf("  marked content: %s ", args[0].getName());
    if (numArgs == 2)
      args[2].print(stdout);
    printf("\n");
    fflush(stdout);
  }

  if (args[0].isName("Span") && numArgs == 2 && args[1].isDict()) {
      Object obj = args[1].dictLookup("ActualText");
      if (obj.isString()) {
    	actualMarkerBegin = true;
    	actualtextString = new GooString(obj.getString());
    	actualMarkerPosition = 0;
      }
      //obj.free();
  }

  if(numArgs == 2 && args[1].isName()) {
	  Object mcPropertiesObj = res->lookupMarkedContentNF(args[1].getName());

	  if (mcPropertiesObj.isRef()) {
		  Ref mcPropRef = mcPropertiesObj.getRef();
		  const Object &mcPropObj = xref->fetch(mcPropRef.num, mcPropRef.gen);
		  //if (xref->fetch(mcPropRef.num, mcPropRef.gen, &mcPropObj) != nullptr)
		  if ( ! mcPropObj.isNull() )
		  {
			  if (mcPropObj.isDict()) {
				  Object layotNameObj;
				  layoutIsNew = true;
				  layoutProperties = mcPropObj.getDict()->copy(xref);
			  }

		  }

	  }
    //out->beginMarkedContent(args[0].getName(),args[1].getDict());
  }

  if(numArgs == 2) {
    //out->beginMarkedContent(args[0].getName(),args[1].getDict());
  } else {
    //out->beginMarkedContent(args[0].getName());
  }
}

void PdfParser::opEndMarkedContent(Object /*args*/[], int /*numArgs*/)
{
	if (actualMarkerBegin) {
	  actualMarkerBegin = false;
	  delete actualtextString;
	}
  //out->endMarkedContent();
}

void PdfParser::opMarkPoint(Object args[], int numArgs) {
  if (printCommands) {
    printf("  mark point: %s ", args[0].getName());
    if (numArgs == 2)
      args[2].print(stdout);
    printf("\n");
    fflush(stdout);
  }

  if(numArgs == 2) {
    //out->markPoint(args[0].getName(),args[1].getDict());
  } else {
    //out->markPoint(args[0].getName());
  }

}

//------------------------------------------------------------------------
// misc
//------------------------------------------------------------------------

void PdfParser::saveState() {
  bool is_radial = false;

  GfxPattern *pattern = state->getFillPattern();
  if (pattern != NULL)
    if (pattern->getType() == 2 ) {
        GfxShadingPattern *shading_pattern = static_cast<GfxShadingPattern *>(pattern);
        GfxShading *shading = shading_pattern->getShading();
        if (shading->getType() == 3)
          is_radial = true;
    }

  builder->saveState();
  if (layoutIsNew && layoutProperties)
  {
	  const Object &objName = layoutProperties->lookupNF("Name");
	  if (objName.isString())
	  {
		  const GooString* layoutName = objName.getString();
		  int len = layoutName->getLength();
		  char* cLayoutName = g_strdup(layoutName->c_str());
		  // wrong unicode string. Right marker is 0xFEFF instead 0xFFFE
		  if ((len > 3) && (cLayoutName[0] == (char)0xFF) && (cLayoutName[1] == (char)0xFE))
		  {
			  int idx;
			  for(idx=2; idx < (len - 1); idx+=2)
			  {
				  cLayoutName[idx/2 - 1] = cLayoutName[idx];
			  }
			  cLayoutName[idx] = 0;
		  }
		  builder->setLayoutName(cLayoutName);
		  free(cLayoutName);
		  layoutIsNew = false;
	  }
  }
  if (is_radial)
    state->save();          // nasty hack to prevent GfxRadialShading from getting corrupted during copy operation
  else
    state = state->save();  // see LP Bug 919176 comment 8
  clipHistory = clipHistory->save();
}

void PdfParser::restoreState() {
  clipHistory = clipHistory->restore();
  state = state->restore();
  builder->restoreState();
}

void PdfParser::pushResources(Dict *resDict) {
  res = new GfxResources(xref, resDict, res);
}

void PdfParser::popResources() {
  GfxResources *resPtr;

  resPtr = res->getNext();
  delete res;
  res = resPtr;
}

void PdfParser::setDefaultApproximationPrecision() {
  for (int i = 1; i <= pdfNumShadingTypes; ++i) {
    setApproximationPrecision(i, defaultShadingColorDelta, defaultShadingMaxDepth);
  }
}

void PdfParser::setApproximationPrecision(int shadingType, double colorDelta,
                                          int maxDepth) {

  if (shadingType > pdfNumShadingTypes || shadingType < 1) {
    return;
  }
  colorDeltas[shadingType-1] = dblToCol(colorDelta);
  maxDepths[shadingType-1] = maxDepth;
}

//------------------------------------------------------------------------
// ClipHistoryEntry
//------------------------------------------------------------------------

ClipHistoryEntry::ClipHistoryEntry(GfxPath *clipPathA, GfxClipType clipTypeA) :
  saved(NULL),
  clipPath((clipPathA) ? clipPathA->copy() : NULL),
  clipType(clipTypeA)
{
}

ClipHistoryEntry::~ClipHistoryEntry()
{
    if (clipPath) {
        delete clipPath;
	clipPath = NULL;
    }
}

void ClipHistoryEntry::setClip(GfxPath *clipPathA, GfxClipType clipTypeA) {
    // Free previous clip path
    if (clipPath) {
        delete clipPath;
    }
    if (clipPathA) {
        clipPath = clipPathA->copy();
        clipType = clipTypeA;
    } else {
        clipPath = NULL;
	clipType = clipNormal;
    }
}

ClipHistoryEntry *ClipHistoryEntry::save() {
    ClipHistoryEntry *newEntry = new ClipHistoryEntry(this);
    newEntry->saved = this;

    return newEntry;
}

ClipHistoryEntry *ClipHistoryEntry::restore() {
    ClipHistoryEntry *oldEntry;

    if (saved) {
        oldEntry = saved;
        saved = NULL;
        delete this; // TODO really should avoid deleting from inside.
    } else {
        oldEntry = this;
    }

    return oldEntry;
}

ClipHistoryEntry::ClipHistoryEntry(ClipHistoryEntry *other) {
    if (other->clipPath) {
        this->clipPath = other->clipPath->copy();
        this->clipType = other->clipType;
    } else {
        this->clipPath = NULL;
	this->clipType = clipNormal;
    }
    saved = NULL;
}

#endif /* HAVE_POPPLER */
