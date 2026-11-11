import json,os,glob
from PIL import Image
H=os.path.expanduser('~'); W=H+'/sv-work-2026-11-10'
DV=H+'/Desktop/dod-gc/playtests/videos/'; VM=W+'/vmfull/'
DM=H+'/Desktop/dod-gc/social-media/music/'; LM=H+'/Desktop/lostcoast-gc/promo/music/'
DODOUT=H+'/Desktop/dod-gc/promo/shorts-2026-11-10/'; LCOUT=H+'/Desktop/lostcoast-gc/promo/shorts-2026-11-10/'
CHIP='Dolphin emulator capture · Nov 10, 2026'; KD='DAY OF DEFEAT · GAMECUBE PORT'; KL='HL2: LOST COAST · GAMECUBE PORT'
def img(src,dur,**k): d=dict(kind='img',src=src,dur=dur,chip=CHIP); d.update(k); return d
def vid(src,ss,dur,**k): d=dict(kind='vid',src=src,ss=ss,dur=dur,chip=CHIP); d.update(k); return d
F=1/30
vm15=DV+'20261110-182519-ava-vm15-599494ad0d.mp4'
v1=dict(name='dod01',music=DM+'Mournful March.mp3',music_ss=0,out=DODOUT+'01-dod-garand-viewmodel-bringup.mp4',segments=[
 img(VM+'182519-vm15.png',3.2,kicker=KD,head='Getting the rifle right, one run at a time',pill='dod_avalanche · solo listen server',zoom=0.06),
 img(VM+'171957-playable.png',2.2,kicker=KD,head='17:19: spawned in, no rifle yet',label='17:19',zoom=0.03),
 img(VM+'172449-playable2.png',F,label='crash'),
 img(VM+'172915-playable4.png',F,label='crash'),
 img(VM+'173509-vm1.png',2.4,kicker=KD,head='17:35: the rifle draws',pill='untextured, and it fills most of the screen',label='17:35',zoom=0.03),
 img(VM+'174425-vm4.png',1.6,kicker=KD,head='17:44: still oversized',label='17:44',zoom=0.03),
 img(VM+'175245-vm5.png',F,label='crash'),
 img(VM+'175902-vm6.png',1.5,kicker=KD,head='17:59: still oversized',label='17:59',zoom=0.03),
 img(VM+'180507-vm8.png',2.0,kicker=KD,head='18:05: textures start to arrive',pill='not always on the right surfaces',label='18:05',zoom=0.03),
 img(VM+'180901-vm9.png',1.5,kicker=KD,head='18:09',pill='not always on the right surfaces',label='18:09',zoom=0.03),
 img(VM+'181124-vm10.png',1.4,kicker=KD,head='18:11',pill='not always on the right surfaces',label='18:11',zoom=0.03),
 img(VM+'181403-vm11.png',1.5,kicker=KD,head='18:14',pill='not always on the right surfaces',label='18:14',zoom=0.03),
 img(VM+'181612-vm12.png',2.0,kicker=KD,head='18:16: hands textured',pill='rifle still too big',label='18:16',zoom=0.03),
 img(VM+'182015-vm13.png',2.6,kicker=KD,head='18:20: sized and textured',label='18:20',zoom=0.04),
 vid(vm15,41,5.5,kicker=KD,head='Rifle in hand on dod_avalanche',pill='18:25 run · HUD shows HP 100',label='18:25'),
 img(VM+'182519-vm15.png',3.6,kicker=KD,head='Team menu and spawn work. A full match isn\'t verified yet.',endcard=True),
])
tg1=DV+'20261110-143649-flash-teamgui-7e44ea2556.mp4'; tg2=DV+'20261110-144412-flash-teamgui2-7e44ea2556.mp4'; tg3=DV+'20261110-145246-flash-teamgui3-7e44ea2556.mp4'
v2=dict(name='dod02',music=DM+'Menace of War.mp3',music_ss=0,out=DODOUT+'02-dod-team-menu-three-builds.mp4',segments=[
 vid(tg3,6,3.2,kicker=KD,head='The team menu, three builds in 16 minutes',pill='dod_flash',label='14:52'),
 vid(tg1,8,2.8,kicker=KD,head='14:36: the panel draws',pill='but no text',label='14:36'),
 vid(tg1,37,2.0,kicker=KD,head='14:36: the panel draws',pill='then the map at sunset',label='14:36'),
 vid(tg2,13,2.0,kicker=KD,head='14:44: still no text',label='14:44'),
 vid(tg2,32,2.0,kicker=KD,head='14:44: still no text',pill='and the map draws broken',label='14:44'),
 vid(tg3,8,3.2,kicker=KD,head='14:52: SELECT A TEAM',pill='Allies, Axis, Spectate · D-pad + A',label='14:52'),
 vid(tg3,22,2.6,kicker=KD,head='14:52: Allies picked',pill='the map shows through',label='14:52'),
 vid(tg3,36,4.0,kicker=KD,head='Spawn view, Allies, on dod_flash',label='14:52'),
 vid(tg3,50,3.6,kicker=KD,head='Menus and spawn work. A full match isn\'t verified yet.',endcard=True),
])
# Lost Coast day timelapse
L=W+'/lcfull/'
def lcimg(t,dur,**k):
    p=L+t+'.png'; sz=Image.open(p).size
    d=img(p,dur,size=(640,480),**k)
    if sz==(640,528): d['crop']='640:480:0:24'
    return d
times=sorted(os.path.basename(p)[:-4] for p in glob.glob(L+'*.png'))
skip={'173506','125917'}
def phase(t):
    h=int(t[:4])
    if h<45: return ('Just after midnight','shore, water and title-menu checks')
    if h<300: return ('Early morning','shore geometry runs')
    if h<1530: return ('Afternoon','more geometry runs')
    if h<1600: return ('15:34','a gun appears, untextured')
    if h<1700: return ('16:03','the gun gets textures')
    if h<1716: return ('17:05','bloom + TEV detail, too hot on the sky at first')
    return ('17:16 on','sun disc + hardware fog')
segs=[lcimg('000004',2.6,kicker=KL,head='One day of Lost Coast builds',pill='every run from midnight to 18:19',zoom=0.05,label='00:00')]
for t in times:
    if t in skip: continue
    a,b=phase(t); segs.append(lcimg(t,0.36,kicker=KL,head=a,pill=b,label=f"{t[:2]}:{t[2:4]}"))
segs.append(lcimg('181915',3.0,kicker=KL,head='18:19: latest run',pill='Dolphin framedumps · unfinished port',zoom=0.05,label='18:19'))
segs.append(lcimg('181915',3.4,kicker=KL,head='Still in development, not on hardware yet',endcard=True))
v3=dict(name='lc01',music=LM+'Mechanical Rhythm.mp3',music_ss=0,out=LCOUT+'01-lostcoast-one-day-of-builds.mp4',segments=segs)
N=H+'/Desktop/lostcoast-gc/playtests/notes/dolphin-probe-20261110-'
v4=dict(name='lc02',music=LM+'Mechanical Rhythm(1).mp3',music_ss=0,out=LCOUT+'02-lostcoast-bloom-sun-fog.mp4',segments=[
 lcimg('164010',2.6,kicker=KL,head='Bloom, sun and fog: three runs, 17:05 to 17:16',pill='16:40 · before',label='16:40',zoom=0.04),
 vid(N+'170509/clip.mp4',0,5.6,size=(640,528),kicker=KL,head='17:05: bloom on',pill='gun textured · bloom too hot on the sky',label='17:05'),
 vid(N+'171240/clip.mp4',0,4.6,size=(640,528),kicker=KL,head='17:12: bloom turned down',pill='bloom threshold and strength retuned',label='17:12'),
 vid(N+'171655/clip.mp4',0,4.6,size=(640,528),kicker=KL,head='17:16: sun disc + fog',pill='env_sun as a disc with bloom glow · env_fog_controller as hardware fog',label='17:16'),
 lcimg('181915',3.4,kicker=KL,head='Still in development, not on hardware yet',endcard=True),
])
os.makedirs(W+'/specs',exist_ok=True)
for v in (v1,v2,v3,v4): json.dump(v,open(f"{W}/specs/{v['name']}.json",'w'),indent=1); print(v['name'],len(v['segments']),round(sum(s['dur'] for s in v['segments']),2))
