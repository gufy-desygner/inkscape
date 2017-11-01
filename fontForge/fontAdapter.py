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
    newViewBox = "viewBox=\"0 0 %i %i\"" % (int(width * vwidth/height), vwidth)
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

    for cell in chars :
        try:
            gg = font[int(cell['gid'])]
            gg.unicode = cell['uni']
            gg.glyphname = "glyph%s" % cell['uni']
            font.selection.select(gg)
            font.copy()
            font.selection.none()
            gg2 = font2.createChar(cell['uni'])
            font2.selection.select(gg2)
            font2.paste();         
            font2.selection.none()            
        except:
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
