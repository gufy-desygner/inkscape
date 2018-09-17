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

def main(font_file):
    
    # load and parse matched MAP file
    mapJSON = loadJson("%s.map" % font_file)
    chars = []
    filled = []
    
    for obj in mapJSON["uniMap"]:
        for (cid, params) in obj.items():
            chars.append({"gid" : cid,
                          "uni" : params["uni"]})
    
    # create new font object
    font2 = fontforge.open(font_file)
    font2.clear()
    #font2.generate("tmp.ttf")
    
    font2.save("tmp.ttf")    
    font2.close()  
    	
    font2 = fontforge.open("tmp.ttf")  
    
    # load font from file
    font = fontforge.open(font_file) #current font 
    
    idToNameMap = makeGlyphMap(font);
    
    for cell in chars :
        ex = 1
        exist = 0;
        for ob in filled :
            if ob == cell['uni'] :
                exist = 1;
                break
            
        try:
            try:
                gg = font[int(cell['gid'])]
            except TypeError as ex:
                name = idToNameMap[int(cell["gid"])]
                gg = font[name]
            # sometimes do not have exception but glyph is empty
            if gg.originalgid == 0 :
                name = idToNameMap[int(cell["gid"])]
                if name == "" :
                    font2.createChar(cell['uni'])
                    continue
                gg = font[name]  
            #print "%i %i %s %i" % (gg.originalgid, gg.unicode, gg.glyphname, cell['uni'])  
            continue
            gg.unicode = cell['uni']
            gg.glyphname = "glyph%s" % cell['uni']
            font.selection.select(gg)
            font.copy()
            font.selection.none()
            if exist  and (not isEmptyGlyph(gg)) :
                # FontForge error
                print "FFE-001: duble of code %s replaced" % cell['uni']
            if exist  and isEmptyGlyph(gg) :
                print "FFE-002: emty duble for code %s skiped" % cell['uni']
                continue
            gg2 = font2.createChar(cell['uni'])
            if not isEmptyGlyph(gg) :
                filled.append(cell['uni'])
            font2.selection.select(gg2)
            font2.paste();         
            font2.selection.none() 
            gg2.glyphname = "glyph%s" % cell['uni']       
            ex = 0
        except Exception as exception:
            #print type(exception).__name__
            if ex == 1 :
                font2.createChar(cell['uni'])
           
    font2.familyname = sys.argv[3]
    font2.fontname = sys.argv[2]   
    font2.generate("%s" % font_file)
    font2.save("%s.sfd" % font_file)
    font2.close

if __name__ == '__main__':
    if len(sys.argv) > 1:
        main(sys.argv[1])
    else:
        sys.stderr.write("\nUsage: %s CIDkeyFont.cff\n" % sys.argv[0] )

# vim: set filetype=python:
