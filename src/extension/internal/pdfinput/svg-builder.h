#ifndef SEEN_EXTENSION_INTERNAL_PDFINPUT_SVGBUILDER_H
#define SEEN_EXTENSION_INTERNAL_PDFINPUT_SVGBUILDER_H

/*
 * Authors:
 *   miklos erdelyi
 *
 * Copyright (C) 2007 Authors
 *
 * Released under GNU GPL, read the file 'COPYING' for more information
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#ifdef HAVE_POPPLER

class SPDocument;
namespace Inkscape {
    namespace XML {
        struct Document;
        class Node;
    }
}

#include <2geom/point.h>
#include <2geom/affine.h>
#include <glibmm/ustring.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#include "CharTypes.h"
class GooString;
class Function;
class GfxState;
struct GfxColor;
class GfxColorSpace;
struct GfxRGB;
class GfxPath;
class GfxPattern;
class GfxTilingPattern;
class GfxShading;
class GfxFont;
class GfxImageColorMap;
class Stream;
class XRef;

class SPCSSAttr;

#include <vector>
#include <glib.h>

typedef std::vector<Inkscape::XML::Node*> NodeList;

namespace Inkscape {
namespace Extension {
namespace Internal {

struct SvgTransparencyGroup;

/**
 * Holds information about the current softmask and group depth for use of libpoppler.
 * Could be later used to store other graphics state parameters so that we could
 * emit only the differences in style settings from the parent state.
 */
struct SvgGraphicsState {
    Inkscape::XML::Node *softmask; // Points to current softmask node
    int group_depth;    // Depth of nesting groups at this level
};
bool inList(std::vector<std::string> &tags, const char* tag);

/**
 * Holds information about glyphs added by PdfParser which haven't been added
 * to the document yet.
 */
struct SvgGlyph {
    Geom::Point position;    // Absolute glyph coords
    Geom::Point text_position; // Absolute glyph coords in text space
    double dx;  // X advance value
    double dy;  // Y advance value
    double spaceWidth;
    double rise;    // Text rise parameter
    Glib::ustring code;   // UTF-8 coded character
    bool is_space;

    bool style_changed;  // Set to true if style has to be reset
    SPCSSAttr *style;
    int render_mode;    // Text render mode
    char *font_specification;   // Pointer to current font specification
    GfxFont *font; // reference to PDF font
    double fontSize;
    uint gidCode; // code of glyph in font stream
    double charSpace; // used for dx calculate in _flushText (gap of text)
    double wordSpace;
};

void mergeTwoTspan(Inkscape::XML::Node *first, Inkscape::XML::Node *second);
void mergeTspanList(NodeList &tspanArray);
/**
 * Builds the inner SVG representation using libpoppler from the calls of PdfParser.
 */

struct SvgTextPosition {
    Inkscape::XML::Node* ptextNode;
    gchar* text;
    double x = 0;
    double y = 0;
    Geom::Rect* sqTextBBox;
};

class SvgBuilder {
public:
    SvgBuilder(SPDocument *document, gchar *docname, XRef *xref);
    SvgBuilder(SvgBuilder *parent, Inkscape::XML::Node *root);
    virtual ~SvgBuilder();
    // tools
    void mergeTextNodesToFirst(NodeList &listNodes);

    // Property setting
    void setDocumentSize(double width, double height);  // Document size in px
    void setAsLayer(char *layer_name=NULL);
    void setLayoutName(char *layout_name=NULL);
    void setGroupOpacity(double opacity);
    NodeList* getNodeListByTags(std::vector<std::string> &tags, NodeList* list, Inkscape::XML::Node* startNode);
    NodeList* getNodeListByTag(const char* tag, NodeList* list, Inkscape::XML::Node* startNode = nullptr);
    Inkscape::XML::Node* getMainNode();

    Inkscape::XML::Node *getPreferences() {
        return _preferences;
    }

    std::vector<NodeList>* getRegions(std::vector<std::string> &tags);


    // Handling the node stack
    Inkscape::XML::Node *pushGroup();
    Inkscape::XML::Node *popGroup();
    Inkscape::XML::Node *getContainer();    // Returns current group node
    Inkscape::XML::Node *createElement(char const *name);
    Inkscape::XML::Node *createTextNode(char const *content);

    char *getGlyph(SvgGlyph * svgGlyph, FT_Face face);
    FT_GlyphSlot getFTGlyph(GfxFont *font, double fontSize, uint gidCode, unsigned long int zoom);
    const char *generateClipsFormLetters(Inkscape::XML::Node *container);
    void adjustEndX();
    enum todoRemoveClip {
    	CLIP_NOTFOUND,
    	REMOVE_CLIP,
    	KEEP_CLIP,
    };
    todoRemoveClip checkClipAroundText(Inkscape::XML::Node *gNode);
    double fetchAverageColor(Inkscape::XML::Node *container, Inkscape::XML::Node *image_node);

    // Path adding
    void addPath(GfxState *state, bool fill, bool stroke, bool even_odd=false);
    void addShadedFill(GfxShading *shading, double *matrix, GfxPath *path, bool even_odd=false);

    // Image handling
    void addImage(GfxState *state, Stream *str, int width, int height,
                  GfxImageColorMap *color_map, bool interpolate, int *mask_colors);
    void addImageMask(GfxState *state, Stream *str, int width, int height,
                      bool invert, bool interpolate);
    void addMaskedImage(GfxState *state, Stream *str, int width, int height,
                        GfxImageColorMap *color_map, bool interpolate,
                        Stream *mask_str, int mask_width, int mask_height,
                        bool invert_mask, bool mask_interpolate);
    void addSoftMaskedImage(GfxState *state, Stream *str, int width, int height,
                            GfxImageColorMap *color_map, bool interpolate,
                            Stream *mask_str, int mask_width, int mask_height,
                            GfxImageColorMap *mask_color_map, bool mask_interpolate);

    // Transparency group and soft mask handling
    void pushTransparencyGroup(GfxState *state, double *bbox,
                               GfxColorSpace *blending_color_space,
                               bool isolated, bool knockout,
                               bool for_softmask);
    void popTransparencyGroup(GfxState *state);
    void paintTransparencyGroup(GfxState *state, double *bbox);
    void setSoftMask(GfxState *state, double *bbox, bool alpha,
                     Function *transfer_func, GfxColor *backdrop_color);
    Inkscape::XML::Node *getRoot() {
    	return _root;
    }
    void clearSoftMask(GfxState *state);

    // Text handling
    void beginString(GfxState *state, GooString *s);
    void endString(GfxState *state);
    void addChar(GfxState *state, double x, double y,
                 double dx, double dy,
                 double originX, double originY,
                 CharCode code, int nBytes, Unicode *u, int uLen);
    char* glyphToPath(GfxState *state, CharCode codeCopy, Unicode uCopy);
    void beginTextObject(GfxState *state);
    void endTextObject(GfxState *state);

    bool isPatternTypeSupported(GfxPattern *pattern);

    // State manipulation
    void saveState();
    void restoreState();
    void updateStyle(GfxState *state);
    void updateFont(GfxState *state);
    void updateTextPosition(double tx, double ty);
    void updateTextShift(GfxState *state, double shift);
    void updateTextMatrix(GfxState *state);

    // Clipping
    void clip(GfxState *state, bool even_odd=false);
    void setClipPath(GfxState *state, bool even_odd=false);

    // Transforming
    void setTransform(double c0, double c1, double c2, double c3, double c4,
                      double c5);
    void setTransform(double const *transform);
    bool getTransform(double *transform);
    gchar *getDocName();
    gint getCountOfImages(void) { return _countOfImages;};
    gint getCountOfPath(void) { return _countOfPath;};
    SPDocument *getSpDocument(void){ return _doc;};
    double glipEndX, glipEndY;
    double spaceWidth;

    std::vector<SvgTextPosition> getTextPositionList() { return textPositionList; }
    void setTextPositionList(std::vector<SvgTextPosition> _textPositionList) { textPositionList = _textPositionList; }
    std::vector<SvgTextPosition> getTextInArea(double x1, double y1, double x2, double y2);

private:
    void _init();

    // Pattern creation
    gchar *_createPattern(GfxPattern *pattern, GfxState *state, bool is_stroke=false);
    gchar *_createGradient(GfxShading *shading, double *matrix, bool for_shading=false);
    void _addStopToGradient(Inkscape::XML::Node *gradient, double offset,
                            GfxRGB *color, double opacity);
    bool _addGradientStops(Inkscape::XML::Node *gradient, GfxShading *shading,
                           Function *func);
    gchar *_createTilingPattern(GfxTilingPattern *tiling_pattern, GfxState *state,
                                bool is_stroke=false);

    static int getImageIngex() { static int counter = 0; return counter++; };
    // generate alpha channel for masked image
    unsigned char* _encodeImageAlphaMask(Stream *str, int width, int height,
                                      GfxImageColorMap *color_map, bool interpolate);

    Inkscape::XML::Node *_createMaskedImage(Stream *str, int width, int height,
                                      GfxImageColorMap *color_map, bool interpolate,
									  unsigned char* alphaChanel, int mask_width, int mask_height);
    // Image/mask creation
    Inkscape::XML::Node *_createImage(Stream *str, int width, int height,
                                      GfxImageColorMap *color_map, bool interpolate,
                                      int *mask_colors, bool alpha_only=false,
                                      bool invert_alpha=false);
    Inkscape::XML::Node *_createMask(double width, double height);
    // Style setting
    SPCSSAttr *_setStyle(GfxState *state, bool fill, bool stroke, bool even_odd=false);
    void _setStrokeStyle(SPCSSAttr *css, GfxState *state);
    void _setFillStyle(SPCSSAttr *css, GfxState *state, bool even_odd);

    void _flushText();    // Write buffered text into doc

    std::string _BestMatchingFont(std::string PDFname);

    // Handling of node stack
    Inkscape::XML::Node *pushNode(const char* name);
    Inkscape::XML::Node *popNode();
    std::vector<Inkscape::XML::Node *> _node_stack;
    std::vector<int> _group_depth;    // Depth of nesting groups
    SvgTransparencyGroup *_transp_group_stack;  // Transparency group stack
    std::vector<SvgGraphicsState> _state_stack;

    SPCSSAttr *_font_style;          // Current font style
    GfxFont *_current_font;
    char *_font_specification;
    double _font_scaling;
    bool _need_font_update;
    Geom::Affine _text_matrix;
    Geom::Point _text_position;
    GPtrArray *glyphs_for_clips;
    std::vector<SvgGlyph> _glyphs;   // Added characters
    bool _in_text_object;   // Whether we are inside a text object
    bool _invalidated_style;
    GfxState *_current_state;
    std::vector<std::string> _availableFontNames; // Full names, used for matching font names (Bug LP #179589).

    bool _is_top_level;  // Whether this SvgBuilder is the top-level one
    SPDocument *_doc;
    gchar *_docname;    // Basename of the URI from which this document is created
    XRef *_xref;    // Cross-reference table from the PDF doc we're converting from
    Inkscape::XML::Document *_xml_doc;
    Inkscape::XML::Node *_root;  // Root node from the point of view of this SvgBuilder
    Inkscape::XML::Node *_container; // Current container (group/pattern/mask)
    Inkscape::XML::Node *_preferences;  // Preferences container node
    double _width;       // Document size in px
    double _height;       // Document size in px
    double _ttm[6]; ///< temporary transform matrix
    int _countOfImages;
    int _countOfPath;
    bool _ttm_is_set;

    std::vector<SvgTextPosition> textPositionList;
};


} // namespace Internal
} // namespace Extension
} // namespace Inkscape

#endif // HAVE_POPPLER

#endif // SEEN_EXTENSION_INTERNAL_PDFINPUT_SVGBUILDER_H

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4 :
