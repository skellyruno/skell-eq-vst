from PIL import Image, ImageDraw, ImageFilter
import random, math, os
random.seed(112)
root=os.path.join(os.path.dirname(__file__),'Assets')
os.makedirs(root,exist_ok=True)
W,H=1784,882
f=Image.new('RGBA',(W,H));d=ImageDraw.Draw(f,'RGBA')
for band in range(7):
    for x,y,w,h in [(4+band*2,5+band*2,W-8-band*4,106-band*4),(4+band*2,726+band*2,W-8-band*4,149-band*4),(4+band*2,114,34-band*2,606),(1746+band*2,114,34-band*2,606)]:
        if w>0 and h>0:d.rounded_rectangle((x,y,x+w,y+h),radius=6,outline=(25,75+band*17,27,180),width=2)
for x,y,w,h in [(14,12,1738,94),(9,730,1765,135),(10,113,42,606),(1724,113,51,608)]:
    d.rounded_rectangle((x,y,x+w,y+h),radius=12,fill=(15,20,17,190),outline=(65,85,69,255),width=3)
for x1,y1,x2,y2 in [(45,110,1710,110),(34,730,1736,730),(43,16,1715,16),(15,163,15,689),(1768,163,1768,689),(82,803,474,803),(1200,803,1670,803)]:
    d.line((x1,y1,x2,y2),fill=(30,255,50,160),width=3)
for k in range(100):
    x=random.choice([random.randint(0,60),random.randint(1715,1783),random.randint(0,W)])
    y=random.choice([random.randint(0,110),random.randint(730,881)])
    r=random.randint(1,4)
    d.ellipse((x-r,y-r,x+r,y+r),fill=(15,20,15,220),outline=(105,121,99,200))
for x,y in [(24,26),(1760,24),(22,862),(1762,862),(666,47),(1114,47),(550,820),(1170,820)]:
    d.ellipse((x-9,y-9,x+9,y+9),fill=(20,25,22,255),outline=(135,155,125,255),width=2)
    d.line((x-4,y-4,x+4,y+4),fill=(130,151,130,200),width=2)
f.save(os.path.join(root,'frame.png'))
# Original vector skull illustration, transparent background
s=Image.new('RGBA',(250,215));d=ImageDraw.Draw(s,'RGBA')
d.ellipse((55,21,195,153),fill=(35,44,37,255),outline=(110,130,109,255),width=5)
d.polygon([(75,116),(105,172),(146,176),(180,116)],fill=(29,35,29,255),outline=(117,137,106,255))
for cx in [87,161]:d.polygon([(cx-25,87),(cx-15,62),(cx+15,66),(cx+23,89),(cx,107)],fill=(2,8,3,255),outline=(46,255,65,255))
d.polygon([(125,101),(107,126),(142,126)],fill=(5,12,5,255),outline=(71,255,65,240))
for x in range(103,150,10):d.line((x,143,x,163),fill=(172,179,159,240),width=2)
for a in range(10):
    ang=a*math.tau/10;cx,cy=125+math.cos(ang)*98,95+math.sin(ang)*83
    dx,dy=math.cos(ang),math.sin(ang)
    d.polygon([(cx-dy*10,cy+dx*10),(cx+dx*35,cy+dy*35),(cx+dy*10,cy-dx*10)],fill=(120,135,115,255),outline=(190,207,165,245))
for x in [48,75,174,196]:
    y=random.randint(110,153);d.polygon([(x,y),(x+12,y+4),(x+2,y+random.randint(22,49))],fill=(13,245,40,220))
s.save(os.path.join(root,'skull.png'))
back=Image.new('RGBA',(1450,545),(2,11,5,255));d=ImageDraw.Draw(back,'RGBA')
for k in range(1000):
    x=random.randrange(1450);y=random.randrange(545)
    r=random.randrange(2,37)
    d.arc((x-r,y-r,x+r,y+r),random.randrange(360),random.randrange(360,700),fill=(15,52,15,random.randrange(3,22)),width=random.randrange(1,4))
back.save(os.path.join(root,'graph_bg.png'))
knob=Image.new('RGBA',(96,96));d=ImageDraw.Draw(knob,'RGBA')
d.ellipse((7,7,89,89),fill=(11,19,15,255),outline=(71,105,71,255),width=4)
for k in range(8):d.arc((10+k,10+k,86-k,86-k),205,335,fill=(30+k*5,45+k*6,30+k*5,170),width=2)
knob.save(os.path.join(root,'knob_cap.png'))
print('Assets generated')
