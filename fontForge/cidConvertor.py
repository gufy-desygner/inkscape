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
    for line in open(svgFileName, 'r'):
        content = "%s%s" % (content, line)
 #       if m == None :
#            m = re.match(r'.+transform="matrix\([\d-]+ [\d-]+ [\d-]+ [\d-]+ [\d-]+ ([\d-]+).+', line)
    #hh = int(m.group(1))
    regExp = re.compile(r'^.*\n.*\n.*(viewBox="[\d-]+ [\d-]+ [\d-]+ [\d-]+").*', re.MULTILINE)
    viewBoxContent = regExp.match(content).group(1)
    vwidth = int(re.match(r'.+ ([\d-]+)\"$',viewBoxContent).group(1))
    #diff = hh - int(height)
    newViewBox = "viewBox=\"0 %i %i %i\"" % (0, int(width * vwidth/height), vwidth)
    content2 = re.sub(viewBoxContent, newViewBox, content)
    f = open(svgFileName, 'w')
    f.write(content2)
    f.close()

def loadJson(filename='font.json'):
    with open(filename) as f:
        return json.load(f)

def main(font_file):
    # parse unicode map
    mapJSON = loadJson("%s.map" % font_file)
    mapSize = 0
    cids = [0] * 1000
    cds = [0] * 1000
    for pair in mapJSON:
        for cid, code in pair.items():
            cids[mapSize] = cid
            cds[mapSize] = code
        mapSize = mapSize + 1

    # build new font
    (path, cff_name) = os.path.split(font_file)
    if len(path) > 0 :
       path = '%s%s' % (path, '/')
    ttf_name = re.sub(r".[^\.]+$", r".ttf", cff_name)
    # if we have ttf already - try merge it
    try:
       fontTTF = fontforge.open("%s%s" % (path, ttf_name))
       isNewTTF = 0
    except:
       fontTTF = fontforge.font() # new font
       isNewTTF = 1
   
    mapPos = 0; # cursor for map file
    font = fontforge.open(font_file) #current font 
    #sys.stdout.write(font.sfnt_names)

    # serch current glyph in CID font
    glyphFileName = '%s/%s_font_file.svg' % (tempfile.gettempdir(), ttf_name)

    for glyph in font.glyphs():
        if (glyph.glyphname == '.notdef'):
            continue
#        sys.stdout.write('===%s  %s\n' % (glyph.originalgid, glyph.glyphname))
        if re.search(r'uniF', glyph.glyphname) == None :
           gId = int(re.match(r"[^\d]*(\d+)$", glyph.glyphname).group(1))
        else:
           gId = int(re.match(r"[^\dA-F]*F([\dA-F]+)$", glyph.glyphname).group(1), 16)

        if (gId == int(cids[mapPos])):
            glyph.export(glyphFileName)
            glyph.unicode = int(cds[mapPos])
            fixWidth(glyph.width, font.capHeight, glyphFileName)
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
    fontTTF.generate("%s%s" % (path, ttf_name))

if __name__ == '__main__':
    if len(sys.argv) > 1:
        main(sys.argv[1])
    else:
        sys.stderr.write("\nUsage: %s CIDkeyFont.cff\n" % sys.argv[0] )

# vim: set filetype=python:
