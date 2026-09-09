# Rasterise LECO glyphs from text_render.c's own point tables.
LECO_X=[-7,+7,+7,-7,-7,-3,-3,+3,+3,-3,
        -7,+2,+2,+7,+7,-7,-7,-2,-2,-7,
        -7,+7,+7,-3,-3,+7,+7,-7,-7,+3,+3,-3,-3,-7,
        -7,+7,+7,-7,-7,+3,+3,-5,-5,+3,+3,-7,
        -7,-3,-3,+3,+3,+7,+7,+3,+3,-7,
        -7,+7,+7,-3,-3,+7,+7,-7,-7,-3,-3,+3,+3,-7,
        +7,-3,-3,+3,+3,-5,-5,+7,+7,-7,-7,+7,
        -7,+7,+7,+3,+3,-3,-3,-7,
        -7,-7,+7,+7,-7,-7,+5,+5,-3,-3,+3,+3,-3,-3,
        -7,+3,+3,-3,-3,+5,+5,-7,-7,+7,+7,-7,
        -2,+2,+2,-2,
        -7,+7,+7,-7]
LECO_Y=[-10,-10,+10,+10,-10,-10,+6,+6,-6,-6,
        -10,-10,+6,+6,+10,+10,+6,+6,-6,-6,
        -10,-10,+2,+2,+6,+6,+10,+10,-2,-2,-6,-6,-4,-4,
        -10,-10,+10,+10,+6,+6,+2,+2,-2,-2,-6,-6,
        -10,-10,-2,-2,-10,-10,+10,+10,+2,+2,
        -10,-10,-6,-6,-2,-2,+10,+10,+4,+4,+6,+6,+2,+2,
        -6,-6,+6,+6,+2,+2,-2,-2,+10,+10,-10,-10,
        -10,-10,+10,+10,-6,-6,-4,-4,
        +2,-10,-10,+10,+10,-2,-2,+2,+2,+6,+6,-6,-6,+2,
        +6,+6,-6,-6,-2,-2,+2,+2,-10,-10,+10,+10,
        -2,-2,+2,+2,
        +6,+6,+10,+10]
OFF=[0,10,20,34,46,56,70,82,90,104,116,120,124]
WID=[14,14,14,14,14,14,14,14,14,14,4,14]
COLON=[-4,8]; H=20; KERN=2   # LECO_KERNING checked below

def idx_of(ch):
    return 11 if ch=='_' else ord(ch)-ord('0')
def sc(v,S):
    return v*S//H if v>=0 else -((-v*S)//H)

def poly(ch,S):
    i=idx_of(ch)
    return [(sc(LECO_X[k],S), sc(LECO_Y[k],S)) for k in range(OFF[i],OFF[i+1])], sc(WID[i],S)

def raster(pts, rule='nz'):
    xs=[p[0] for p in pts]; ys=[p[1] for p in pts]; n=len(pts)
    x0,x1,y0,y1=min(xs),max(xs),min(ys),max(ys)
    g=[[0]*(x1-x0+1) for _ in range(y1-y0+1)]
    for y in range(y0,y1+1):
        for x in range(x0,x1+1):
            px,py=x+0.5,y+0.5
            if rule=='eo':
                ins=False
                for k in range(n):
                    ax,ay=pts[k]; bx,by=pts[(k+1)%n]
                    if (ay>py)!=(by>py) and px<ax+(py-ay)*(bx-ax)/(by-ay): ins=not ins
                v=1 if ins else 0
            else:
                w=0
                for k in range(n):
                    ax,ay=pts[k]; bx,by=pts[(k+1)%n]
                    if ay<=py<by and px<ax+(py-ay)*(bx-ax)/(by-ay): w+=1
                    elif by<=py<ay and px<ax+(py-ay)*(bx-ax)/(by-ay): w-=1
                v=1 if w!=0 else 0
            g[y-y0][x-x0]=v
    return g,(x0,y0)
