"""Exact LCS fine water grid -> merged SA water.dat rectangles."""
import json
import math


def export_water(game, output):
    c=game.chunk
    p=c.u32(32+132)
    n,zs,rs=c.read('<3I',p)
    if n<1 or n>256:raise ValueError('Invalid water level count')
    heights=c.read('<'+str(n)+'f',zs)
    if not all(math.isfinite(z) for z in heights):raise ValueError('Invalid water height')
    grid=c.span(p+4108,128*128)
    rects=[];active={}
    for y in range(128):
        runs=[];x=0
        def height(x,y):
            value=grid[x*128+y]
            if value>=128:return None
            if value>=n:raise ValueError('Water cell refers to invalid level')
            return heights[value]
        while x<128:
            start=x;z=height(x,y);x+=1
            while x<128 and height(x,y)==z:x+=1
            if z is not None:runs.append((start,x,z))
        next_active={}
        for run in runs:
            if run in active:r=active[run];r[3]=y+1
            else:r=[run[0],y,run[1],y+1,run[2]];rects.append(r)
            next_active[run]=r
        active=next_active
    coverage={}
    for x1,y1,x2,y2,z in rects:
        for x in range(x1,x2):
            for y in range(y1,y2):
                if (x,y) in coverage:raise ValueError('Overlapping water rectangles')
                coverage[x,y]=z
    for x in range(128):
        for y in range(128):
            value=grid[x*128+y]
            expected=None if value>=128 else heights[value]
            if coverage.get((x,y))!=expected:raise ValueError('Converted coastline differs from source')
    rects=[[x1*32-2048,y1*32-2048,x2*32-2048,y2*32-2048,z]
           for x1,y1,x2,y2,z in rects]
    # Original renderer also draws the exterior ocean beyond its finite grid.
    rects.extend([[-3000,-3000,-2048,3000,0], [2048,-3000,3000,3000,0],
                  [-2048,-3000,2048,-2048,0],[-2048,2048,2048,3000,0]])
    vertices={(x,y,z) for x1,y1,x2,y2,z in rects for x in (x1,x2) for y in (y1,y2)}
    combos=0
    for by in range(12):
        for bx in range(12):
            count=sum(x1<(-3000+(bx+1)*500) and x2>(-3000+bx*500)
                      and y1<(-3000+(by+1)*500) and y2>(-3000+by*500)
                      for x1,y1,x2,y2,z in rects)
            if count>1:combos+=count+1
    # Native SA 1.0 has 301 quads, 1021 vertices and 700 poly combo entries.
    if len(rects)>301 or len(vertices)>1021 or combos>700:
        raise ValueError('Exact source water exceeds native SA capacities')
    lines=['processed']
    for x1,y1,x2,y2,z in rects:
        big,small=(1.,.1) if z==0 else (.1,.01)
        verts=[(x1,y1),(x2,y1),(x1,y2),(x2,y2)]
        lines.append('    '.join(f'{x} {y} {z:.8g} 0 0 {big:.3f} {small:.3f}' for x,y in verts)+' 1')
    output.write_text('\n'.join(lines)+'\n',encoding='ascii')
    return dict(sourceLevels=n,fineCells=16384,occupiedCells=len(coverage),
                quads=len(rects),vertices=len(vertices),polyCombos=combos,
                allSourceCellsVerified=True,sourceGridOrigin=[-2048,-2048],
                sourceCellSize=32,gameExecuted=False)
