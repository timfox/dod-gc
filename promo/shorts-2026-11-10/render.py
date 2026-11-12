#!/usr/bin/env python3
"""Bounded vertical-short renderer (Nov 10 batch). Reads footage read-only; writes only under work dir + out path."""
import json, os, subprocess, sys, hashlib
from PIL import Image, ImageDraw, ImageFont
W,H,FPS=1080,1920,30
WORK=os.path.expanduser('~/sv-work-2026-11-10')
B='/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'; R='/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'
def font(p,s): return ImageFont.truetype(p,s)
def wrap(d,text,f,maxw):
    out=[]
    for para in text.split('\n'):
        words=para.split(); line=''
        for w in words:
            t=(line+' '+w).strip()
            if d.textlength(t,font=f)<=maxw: line=t
            else: out.append(line); line=w
        out.append(line)
    return out
def overlay(seg,fg_box,path):
    im=Image.new('RGBA',(W,H),(0,0,0,0)); d=ImageDraw.Draw(im)
    # top dark band for legibility
    band=Image.new('RGBA',(W,fg_box[1]),(0,0,0,0)); bd=ImageDraw.Draw(band)
    for y in range(fg_box[1]): bd.line([(0,y),(W,y)],fill=(0,0,0,int(120*min(1,y/120))))
    im.alpha_composite(band)
    y=165
    if seg.get('chip'):
        f=font(R,27); tw=d.textlength(seg['chip'],font=f)
        d.rounded_rectangle([60,y,60+tw+32,y+46],radius=10,fill=(15,20,18,215)); d.text((76,y+8),seg['chip'],font=f,fill=(225,225,225,255)); y+=68
    if seg.get('kicker'):
        d.text((62,y),seg['kicker'],font=font(B,36),fill=(242,190,40,255)); y+=54
    if seg.get('head'):
        f=font(B,66)
        for ln in wrap(d,seg['head'],f,W-124):
            d.text((60+3,y+3),ln,font=f,fill=(0,0,0,170)); d.text((60,y),ln,font=f,fill=(255,255,255,255)); y+=78
    if seg.get('pill'):
        f=font(B,40); lines=wrap(d,seg['pill'],f,W-160); py=fg_box[3]+28
        hgt=26+52*len(lines); tw=max(d.textlength(l,font=f) for l in lines)
        d.rounded_rectangle([48,py,48+tw+48,py+hgt],radius=14,fill=(12,18,16,225))
        for i,l in enumerate(lines): d.text((72,py+12+52*i),l,font=f,fill=(255,255,255,255))
    if seg.get('label'):
        f=font(B,34); l=seg['label']; tw=d.textlength(l,font=f)
        x=fg_box[2]-tw-36; yy=fg_box[1]+18
        d.rounded_rectangle([x-14,yy-6,x+tw+14,yy+46],radius=8,fill=(0,0,0,190)); d.text((x,yy),l,font=f,fill=(242,190,40,255))
    if seg.get('endcard'):
        f1=font(B,64); f2=font(R,38); cy=fg_box[3]+40
        d.rounded_rectangle([60,cy,W-60,cy+190],radius=18,fill=(12,18,16,235))
        t1='gamecubehalf.life'; t2='Project updates + source-code link'
        d.text(((W-d.textlength(t1,font=f1))/2,cy+30),t1,font=f1,fill=(242,190,40,255))
        d.text(((W-d.textlength(t2,font=f2))/2,cy+118),t2,font=f2,fill=(230,230,230,255))
    im.save(path)
def run(cmd):
    r=subprocess.run(['nice','-n','19','ionice','-c3']+cmd,capture_output=True,text=True)
    if r.returncode: sys.stderr.write(r.stderr[-3000:]); raise SystemExit('ffmpeg failed')
def render_seg(seg,i,name):
    sw,sh=seg.get('size',(640,480))
    fw=1000; fh=int(round(fw*sh/sw/2)*2); fx=(W-fw)//2; fy=seg.get('fy',600)
    ov=f"{WORK}/seg/{name}_{i:03d}_ov.png"; overlay(seg,(fx,fy,fx+fw,fy+fh),ov)
    out=f"{WORK}/seg/{name}_{i:03d}.mp4"; dur=seg['dur']
    nfr=max(1,int(round(dur*FPS)))
    if seg['kind']=='img':
        inp=['-loop','1','-framerate',str(FPS),'-t',f"{nfr/FPS:.4f}",'-i',seg['src']]
    else:
        inp=['-ss',str(seg.get('ss',0)),'-t',f"{nfr/FPS+0.2:.4f}",'-i',seg['src']]
    crop=seg.get('crop')  # optional crop of source x:y:w:h
    pre=(f"crop={crop}," if crop else '')+f"scale={sw}:{sh},setsar=1,fps={FPS},format=yuv420p"
    z=seg.get('zoom',0.0)
    if z and nfr>1:
        fgz=f"scale={fw*2}:{fh*2}:flags=bicubic,zoompan=z='1+{z}*on/{nfr}':x='iw/2-(iw/zoom/2)':y='ih/2-(ih/zoom/2)':d=1:s={fw}x{fh}:fps={FPS}"
    else:
        fgz=f"scale={fw}:{fh}:flags=lanczos"
    fc=(f"[0:v]{pre},split[a][b];"
        f"[a]scale={W}:{H}:force_original_aspect_ratio=increase,crop={W}:{H},boxblur=28:2,eq=brightness=-0.18:saturation=0.8[bg];"
        f"[b]{fgz}[fg];[bg][fg]overlay={fx}:{fy}[c];[c][1:v]overlay=0:0,format=yuv420p[v]")
    run(['ffmpeg','-v','error','-y','-threads','4']+inp+['-loop','1','-i',ov,'-filter_complex',fc,'-map','[v]','-frames:v',str(nfr),
         '-r',str(FPS),'-c:v','libx264','-preset','veryfast','-crf','20','-threads','4','-pix_fmt','yuv420p','-an',out])
    return out
def main(spec_path):
    spec=json.load(open(spec_path)); name=spec['name']
    segs=[render_seg(s,i,name) for i,s in enumerate(spec['segments'])]
    lst=f"{WORK}/seg/{name}_list.txt"; open(lst,'w').write(''.join(f"file '{s}'\n" for s in segs))
    silent=f"{WORK}/seg/{name}_silent.mp4"
    run(['ffmpeg','-v','error','-y','-f','concat','-safe','0','-i',lst,'-c','copy',silent])
    dur=float(subprocess.check_output(['ffprobe','-v','error','-show_entries','format=duration','-of','csv=p=0',silent]))
    m=spec['music']; mss=spec.get('music_ss',0)
    af=f"atrim=0:{dur:.3f},asetpts=N/SR/TB,afade=t=in:d=1.0,afade=t=out:st={max(0,dur-2.0):.3f}:d=2.0,loudnorm=I=-16:TP=-1.5:LRA=11,aresample=48000"
    os.makedirs(os.path.dirname(spec['out']),exist_ok=True)
    run(['ffmpeg','-v','error','-y','-i',silent,'-ss',str(mss),'-i',m,'-filter_complex',f"[1:a]{af}[a]",'-map','0:v','-map','[a]',
         '-c:v','copy','-c:a','aac','-b:a','192k','-ar','48000','-movflags','+faststart','-shortest',spec['out']])
    print('OK',spec['out'],round(dur,2))
if __name__=='__main__': main(sys.argv[1])
