"""Build shared marker artwork and an explicit sample layer document."""
import json
import math
import sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'work/geospatial_deps'))
from PIL import Image, ImageDraw

def main():
    folder=ROOT/'assets/landscape';folder.mkdir(parents=True,exist_ok=True)
    image=Image.new('RGBA',(320,64));draw=ImageDraw.Draw(image)
    white=(255,255,255,255); dark=(10,26,38,255)
    draw.polygon([(32,61),(10,28),(54,28)],fill=white)
    draw.ellipse((8,3,56,51),fill=white);draw.ellipse((21,16,43,38),fill=dark)
    draw.ellipse((71,7,121,57),fill=white);draw.ellipse((85,21,107,43),fill=dark)
    draw.rounded_rectangle((136,8,184,56),radius=6,fill=white);draw.rectangle((150,22,170,42),fill=dark)
    star=[]
    for i in range(10):
        a=-math.pi/2+i*math.pi/5;r=29 if i%2==0 else 13
        star.append((224+math.cos(a)*r,32+math.sin(a)*r))
    draw.polygon(star,fill=white)
    draw.polygon([(288,4),(260,57),(316,57)],fill=white);draw.rectangle((285,19,291,39),fill=dark);draw.ellipse((285,44,291,50),fill=dark)
    image.save(folder/'icons.png')
    style={'icon':'pin','sizePx':32,'pattern':'solid','widthPx':4,'color':'#22D3EE','opacity':1.,'fillColor':'#22D3EE','fillOpacity':.25}
    def feature(id,name,type,positions,**overrides):
        return dict(id=id,name=name,type=type,positions=positions,style=style|overrides)
    doc={'schema':'hongtu.landscape.layers.v1','scene':'zhumadian','coordinates':{'srs':'EPSG:32650','origin':[225319.5,3648440.1,30.9],'axes':'east,north,up','unit':'metre'},'layers':[
        {'id':'sample-points','name':'测试 · 关注位置','visible':True,'locked':False,'features':[
            feature('sample-a','办公区位置示例','point',[[350,-1850,90]],icon='pin',color='#FBBF24',sizePx=40),
            feature('sample-b','通道位置示例','point',[[500,-1800,55]],icon='warning',color='#FB7185',sizePx=36)]},
        {'id':'sample-lines','name':'测试 · 路线和区域','visible':True,'locked':False,'features':[
            feature('sample-route','虚线路线示例','line',[[200,-1900,55],[350,-1900,55],[550,-1750,55]],pattern='dash',widthPx=5),
            feature('sample-area','凹多边形区域示例','region',[[180,-2100,55],[580,-2100,55],[580,-1980,55],[350,-1980,55],[350,-1880,55],[180,-1880,55]],color='#34D399',fillColor='#34D399',fillOpacity=.25)]}]}
    projects=ROOT/'projects';projects.mkdir(exist_ok=True)
    (projects/'驻马店图层示例.json').write_text(json.dumps(doc,ensure_ascii=False,indent=2),encoding='utf-8')
    (ROOT/'data/showcase/zhumadian.scene.json').write_text(json.dumps({'schema':'hongtu.scene.v1','name':'驻马店地景','asset_root':'assets/showcase/runtime','presets':['zhumadian']}),encoding='utf-8')
    print('LANDSCAPE ICONS AND SAMPLE CREATED')
if __name__=='__main__':main()
