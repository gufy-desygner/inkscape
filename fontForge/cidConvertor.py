#!/usr/bin/fontforge -script

import sys
import os.path
import json
import re
import tempfile

IMPORT_OPTIONS = ('removeoverlap', 'correctdir')

try:
    unicode
except NameError:
    unicode = str

def loadJson(filename='font.json'):
    with open(filename) as f:
        return json.load(f)

def makeGlyphMap(font) :
    glyphMap = {}
    for gl in font.glyphs() :
        uni = 0 # unecode for puting glyph
        gId = 0 # try receive CID of glyph
        try:
            if re.search(r'uniF', gl.glyphname) == None :
               if re.search(r'uni', gl.glyphname) != None :
                 gId = int(re.match(r"^uni([0-9A-F]+)$", gl.glyphname).group(1), 16)
               else:
                 gId = int(re.match(r"[^\d]*(\d+)$", gl.glyphname).group(1))
            else:
               gId = int(re.match(r"[^\dA-F]*F([\dA-F]+)$", gl.glyphname).group(1), 16)
        except:
            gId = gId
            #print gId, gl.glyphname
            #print "add"
        if gId in glyphMap :
            continue

        glyphMap[gId] = gl.glyphname
    return glyphMap

def findGlyph(font, inGid) :
    for gl in font.glyphs() :
        uni = 0 # unecode for puting glyph
        gId = 0 # try receive CID of glyph
        try:
            if re.search(r'uniF', gl.glyphname) == None :
               if re.search(r'uni', gl.glyphname) != None :
                 gId = int(re.match(r"^uni([0-9A-F]+)$", gl.glyphname).group(1), 16)
               else:
                 gId = int(re.match(r"[^\d]*(\d+)$", gl.glyphname).group(1))
            else:
               gId = int(re.match(r"[^\dA-F]*F([\dA-F]+)$", gl.glyphname).group(1), 16)
        except:
            gId = gId
        if gId != 0 and gId == inGid :
            return gl.glyphname
    return ""

def main(font_file):
    # load and parse matched MAP file
    mapJSON = loadJson("%s.map" % font_file)
    chars = []
    for obj in mapJSON["uniMap"]:
        for (cid, params) in obj.items():
            chars.append({"gid" : cid,
                          "uni" : params["uni"],
                          "xAdvance" : params["xAdvance"],
                          "size" : 1000 })
    # generate name for TTF file
    (path, cff_name) = os.path.split(font_file)
    if len(path) > 0 :
       path = '%s%s' % (path, '/')
    ttf_name = re.sub(r".[^\.]+$", r".ttf", cff_name)

    # if we have ttf already - try merge it
    isNewTTF = 1
    try:
       fontTTF = fontforge.open("%s%s" % (path, ttf_name))
       fontTTF.mergeFont("%s%s.sfd" % (path, ttf_name))
       isNewTTF = 0
    except:
       if isNewTTF == 1 :
            fontTTF = fontforge.open(font_file)
            fontTTF.generate("%s%s" % (path, ttf_name))  
            fontTTF.close()  
            fontTTF = fontforge.open("%s%s" % (path, ttf_name))  
    font = fontforge.open(font_file) #current font 
    needGenerate = 0 # if we do not add any glyph we do not need generate new font

    idToNameMap = makeGlyphMap(font);

    for cell in chars :
        # serch current glyph in CID font
        # name = findGlyph(font, int(cell["gid"]))
        if int(cell["gid"]) in idToNameMap :
            name = idToNameMap[int(cell["gid"])]
        else :
            #print cell["gid"], "not in the map"
            continue

        try:
            glyph = font[name]
            uni = cell["uni"] # unecode for puting glyph
            gId = cell["gid"] # try receive CID of glyph
        except:
            #print "exeption"
            continue

        # processing convert one glyph
        font.selection.none
        font.selection.select(glyph)
        font.copy()

        needGenerate = 1; # if add any plyph we must generate font
        g = fontTTF.createChar(uni)
    
        #g.importOutlines(glyphFileName, IMPORT_OPTIONS)
        #g.removeOverlap()
        fontTTF.selection.none
        fontTTF.selection.select(g)
        fontTTF.paste()
        g.width = cell["xAdvance"]/10 * (fontTTF.em/1000)
        

        g.glyphname = fontforge.nameFromUnicode(g.unicode)
        if g.unicode == 64256:
            g.glyphname = "LATIN SMALL LIGATURE FF"
        if g.unicode == 64257:
            g.glyphname = "LATIN SMALL LIGATURE FI" 
        if g.unicode == 64258:
            g.glyphname = "LATIN SMALL LIGATURE FL"
        if g.unicode == 64259:
            g.glyphname = "LATIN SMALL LIGATURE FFI"
        if g.unicode == 64260:
            g.glyphname = "LATIN SMALL LIGATURE FFL"
        if g.unicode == 64261:
            g.glyphname = "LATIN SMALL LIGATURE LONG S T"
        if g.unicode == 64262:
            g.glyphname = "LATIN SMALL LIGATURE ST"

    if (isNewTTF == 1):
        fontTTF.familyname = font.familyname
        fontTTF.fontname = font.fontname
    if needGenerate != 0 :
        fontTTF.generate("%s%s" % (path, ttf_name))

if __name__ == '__main__':
    if len(sys.argv) > 1:
        main(sys.argv[1])
    else:
        sys.stderr.write("\nUsage: %s CIDkeyFont.cff\n" % sys.argv[0] )

# vim: set filetype=python:
