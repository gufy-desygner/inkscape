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

def findGlyph(font, inGid) :
    glname = ""
    for gl in font.glyphs() :
        if gl.unicode == inGid :
            return gl.glyphname 
    for gl in font.glyphs() :
        uni = 0 # unecode for puting glyph
        gId = 0 # try receive CID of glyph
        
        try:
            if re.search(r'uniF', gl.glyphname) == None :
               if re.search(r'uni', gl.glyphname) != None :
                   gId = int(re.match(r"^uni([0-9A-F]+)$", gl.glyphname).group(1), 16)
                   if gl.glyphname != "" and gId == inGid :
                       return gl.glyphname
               else:
                   gId = int(re.match(r"[^\d]*(\d+)$", gl.glyphname).group(1))
                   if gl.glyphname != "" and gId == inGid :
                       glname = gl.glyphname
                       
            else:
               gId = int(re.match(r"[^\dA-F]*F([\dA-F]+)$", gl.glyphname).group(1), 16)
               if gl.glyphname != "" and gId == inGid :
                   glname = gl.glyphname
                   
        except:
            gId = gId
    return glname

def isEmptyGlyph(glyph):
    n = 0
    while n < glyph.layer_cnt :
        if not glyph.layers[n].isEmpty() :
            return 0
        n = n + 1
    return 1

def loadJson(filename='font.json'):
    with open(filename) as f:
        return json.load(f)

def main(font_file):
    
    # load and parse matched MAP file
    mapJSON = loadJson("%s.map" % font_file)
    chars = []
    for obj in mapJSON["uniMap"]:
        for (cid, params) in obj.items():
            chars.append({"gid" : cid,
                          "uni" : params["uni"]})

    # create new font object
    font2 = fontforge.open(font_file)
    font2.generate("tmp.ttf")  
    font2.close()  
    font2 = fontforge.open("tmp.ttf")  

    # load font from file
    font = fontforge.open(font_file) #current font 

    #for gl in font.glyphs():
    #    if gl.unicode > 0 :
    #        print "%i %i %s \n" % (gl.originalgid, gl.unicode, gl.glyphname)

    for cell in chars :
        ex = 1
        try:
            gg = font[int(cell['gid'])]
            if gg.originalgid == 0 :
                name = findGlyph(font, int(cell["gid"]))
                if name == "" :
                    font2.createChar(cell['uni'])
                    continue
                gg = font[name]               
            gg.unicode = cell['uni']
            gg.glyphname = "glyph%s" % cell['uni']
            font.selection.select(gg)
            font.copy()
            font.selection.none()
            gg2 = font2.createChar(cell['uni'])
            #if isEmptyGlyph(gg) and (not isEmptyGlyph(gg2)):
            #    print "skiped"
            #    ex = 0
            #    continue
            font2.selection.select(gg2)
            font2.paste();         
            font2.selection.none() 
            gg2.glyphname = "glyph%s" % cell['uni']       
            ex = 0
        except Exception as exception:
            if ex == 1 :
                font2.createChar(cell['uni'])
           
    font2.familyname = sys.argv[3]
    font2.fontname = sys.argv[2]   
    font2.generate("%s" % font_file)

if __name__ == '__main__':
    if len(sys.argv) > 1:
        main(sys.argv[1])
    else:
        sys.stderr.write("\nUsage: %s CIDkeyFont.cff\n" % sys.argv[0] )

# vim: set filetype=python:
