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

def fixWidth(width, height, svgFileName):
    content = ""
    m = None
    # load svg file
    for line in open(svgFileName, 'r'):
        content = "%s%s" % (content, line)
    # get viewBox="N N N N"
    regExp = re.compile(r'^.*\n.*\n.*(viewBox="[\d-]+ [\d-]+ [\d-]+ [\d-]+").*', re.MULTILINE)
    viewBoxContent = regExp.match(content).group(1)
    # get hight from viewBox
    vwidth = int(re.match(r'.+ ([\d-]+)\"$',viewBoxContent).group(1))
    # generate new value for viewBox
    newViewBox = "viewBox=\"0 %i %i %i\"" % (0, int(width * vwidth/height), vwidth)
    # replace old values of viewBox
    content2 = re.sub(viewBoxContent, newViewBox, content)
    # save file back
    f = open(svgFileName, 'w')
    f.write(content2)
    f.close()

def loadJson(filename='font.json'):
    with open(filename) as f:
        return json.load(f)

def main(font_file):
    # load and parse matched MAP file
    mapJSON = loadJson("%s.map" % font_file)
    mapSize = 0
    cids = [0] * 1000
    cds = [0] * 1000
    for pair in mapJSON:
        for cid, code in pair.items():
            cids[mapSize] = cid
            cds[mapSize] = code
        mapSize = mapSize + 1

    # generate name for TTF file
    (path, cff_name) = os.path.split(font_file)
    if len(path) > 0 :
       path = '%s%s' % (path, '/')
    ttf_name = re.sub(r".[^\.]+$", r".ttf", cff_name)

    # if we have ttf already - try merge it
    isNewTTF = 1
    try:
       fontTTF = fontforge.open("%s%s" % (path, ttf_name))
       isNewTTF = 0
    except:
       if isNewTTF == 1 :
           fontTTF = fontforge.font() # new font
    # DEBUG block
    if isNewTTF == 10 :
       i = 0
       while i < 255 :
           try:
             print("%i %s" % ( i, fontTTF[i].glyphname))
           except TypeError :
             print "%i do not provided" % i
           except Exception as ex :
             template = "An exception of type {0} occurred. Arguments:\n{1!r}"
             message = template.format(type(ex).__name__, ex.args)
             print message
           i = i+1
    mapPos = 0; # cursor for map file
    font = fontforge.open(font_file) #current font 

    # generate name for temp SVG file
    glyphFileName = '%s/%s_font_file.svg' % (tempfile.gettempdir(), ttf_name)

    # serch current glyph in CID font
    needGenerate = 0 # if we do not add any glyph we do not need generate new font
    for glyph in font.glyphs():
        if (glyph.glyphname == '.notdef'):
            continue
        # convert name of glyph to GID of glyph
        # original GID no always matchet to MAP file
        # two style of glyph name
        if re.search(r'uniF', glyph.glyphname) == None :
           gId = int(re.match(r"[^\d]*(\d+)$", glyph.glyphname).group(1))
        else:
           gId = int(re.match(r"[^\dA-F]*F([\dA-F]+)$", glyph.glyphname).group(1), 16)

        if (gId == int(cids[mapPos])):
            glyph.export(glyphFileName)
            glyph.unicode = int(cds[mapPos])
            fixWidth(glyph.width, font.capHeight, glyphFileName)
            hasUni = 0;
            try : # if glyph is not to be it skeep line "hasUni = 1"
                name = fontTTF[cds[mapPos]].glyphname
                hasUni = 1;
                #print "%i skeeped" % cds[mapPos]
                mapPos = mapPos + 1
            except TypeError :
                hasUni = 0
                #print "%i putted" % cds[mapPos]

            # if font has not current code
            if hasUni == 0 :
                needGenerate = 1;
                g = fontTTF.createChar(int(cds[mapPos]))
                g.importOutlines(glyphFileName, IMPORT_OPTIONS)
                g.removeOverlap()
                wNew = int(glyph.width * fontTTF.capHeight/font.capHeight)
                g.width = wNew
                #print("-w%i  %i  h%i %i" % (g.width, glyph.width, fontTTF.capHeight, font.capHeight))
                mapPos = mapPos + 1
                if mapSize <= mapPos:
                    break
    try:
        os.remove(glyphFileName)
    except:
        print('Can not remove temp file')
    if (isNewTTF == 1):
        fontTTF.familyname = font.familyname
        fontTTF.fontname = font.fontname
    #fontTTF.mergeFonts(font_file)
    if needGenerate != 0 :
        fontTTF.generate("%s%s" % (path, ttf_name))

if __name__ == '__main__':
    if len(sys.argv) > 1:
        main(sys.argv[1])
    else:
        sys.stderr.write("\nUsage: %s CIDkeyFont.cff\n" % sys.argv[0] )

# vim: set filetype=python:
