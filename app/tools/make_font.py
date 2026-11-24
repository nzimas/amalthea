from pathlib import Path
from PIL import Image,ImageFont,ImageDraw
r=Path(__file__).resolve().parents[1]
im=Image.new('L',(24*16,44*6))
d=ImageDraw.Draw(im)
f=ImageFont.truetype(str(r/'assets/Oxanium.ttf'),32)
f.set_variation_by_axes([600])
for i in range(96):
 ch=chr(i+32); box=d.textbbox((0,0),ch,font=f); x=(i%16)*24+(24-(box[2]-box[0]))//2-box[0]; y=(i//16)*44
 d.text((x,y),ch,font=f,fill=255)
b=im.tobytes()
(r/'src/font.hpp').write_text('#pragma once\nstatic const unsigned char fontMask[]={\n'+','.join(str(x) for x in b)+'\n};\n')
