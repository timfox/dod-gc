"""Writes a small DoD-style test room (stock entities, stock caen textures)."""
import sys, gsmap
def room(path, pillar_h=96):
    T="m_wall11"; F="m_floor1"; C="m_whitewash2"
    wb=[]
    wb.append((gsmap.box((-512,-512,-16),(512,512,0)),F))
    wb.append((gsmap.box((-512,-512,192),(512,512,208)),C))
    for mins,maxs in [((-528,-512,-16),(-512,512,208)),((512,-512,-16),(528,512,208)),
                      ((-512,-528,-16),(512,-512,208)),((-512,512,-16),(512,528,208))]:
        wb.append((gsmap.box(mins,maxs),T))
    wb.append((gsmap.box((-32,-32,0),(32,32,pillar_h)),"c2_wood"))   # the visible edit
    ents=[
     {"keys":{"classname":"info_player_allies","origin":"-384 0 24","angles":"0 0 0"}},
     {"keys":{"classname":"info_player_axis","origin":"384 0 24","angles":"0 180 0"}},
     {"keys":{"classname":"info_doddetect"}},
     {"keys":{"classname":"light","origin":"0 0 160","_light":"255 240 200 300"}},
    ]
    gsmap.write_map(path,wb,ents,wad="dod_caen.wad")
if __name__=="__main__":
    room(sys.argv[1], int(sys.argv[2]) if len(sys.argv)>2 else 96)
