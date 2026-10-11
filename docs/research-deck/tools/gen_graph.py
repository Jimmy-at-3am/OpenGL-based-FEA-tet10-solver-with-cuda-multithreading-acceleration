import math, json
W,H=1150,860   # svg placed at slide (40,200)
CX,CY=560,430
def pol(R,deg):
    t=math.radians(deg); return (CX+R*math.sin(t), CY-R*math.cos(t))
groups={'G1':(-25,'Surface for drawing'),'G2':(-75,'Real size'),'G5':(35,'Sources'),'G3':(90,'Volume mesh'),
        'G4':(140,'Printed structure'),'G6':(185,'Results'),'G7':(232,'Display state')}
mods={'M1':(400,-50,'processRawGeometry'),'M2':(400,18,'SlicedPackage'),'M5':(400,58,'LoadTransfer'),
      'M4':(400,100,'generateMidEdgeNodes'),'M3':(390,128,'SlabMesher'),'M6':(370,166,'FEASolver'),
      'M7':(390,206,'StressVisualization'),'M8':(400,248,'selectDefaultResult'),'M9':(400,-90,'SimulationRecord')}
P={}
for k,(a,n) in groups.items(): P[k]=pol(235,a)
for k,(r,a,n) in mods.items(): P[k]=pol(r,a)
P['C']=(CX,CY)
edges=[('M1','G1'),('M1','G2'),('M2','G5'),('M3','G3'),('M3','G4'),('M4','G3'),('M5','G3'),('M5','G5'),
       ('M6','G3'),('M6','G4'),('M6','G6'),('M7','G6'),('M7','G7'),('M8','G7'),('M9','C')]
out=[f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">',
     '<defs><radialGradient id="gc" cx="0.5" cy="0.5" r="0.5"><stop offset="0" stop-color="#0078D4" stop-opacity="0.35"/><stop offset="1" stop-color="#0078D4" stop-opacity="0"/></radialGradient>',
     '<radialGradient id="gm" cx="0.5" cy="0.5" r="0.5"><stop offset="0" stop-color="#E3008C" stop-opacity="0.30"/><stop offset="1" stop-color="#E3008C" stop-opacity="0"/></radialGradient></defs>']
for g in groups:
    x,y=P[g]; out.append(f'<line x1="{CX}" y1="{CY}" x2="{x:.1f}" y2="{y:.1f}" stroke="#0078D4" stroke-width="3" opacity="0.45"/>')
for a,b in edges:
    (x1,y1),(x2,y2)=P[a],P[b]
    dash=' stroke-dasharray="8 7"' if a=='M9' else ''
    out.append(f'<line x1="{x1:.1f}" y1="{y1:.1f}" x2="{x2:.1f}" y2="{y2:.1f}" stroke="#9AA3AD" stroke-width="2.2"{dash}/>')
out.append(f'<circle cx="{CX}" cy="{CY}" r="120" fill="url(#gc)"/><circle cx="{CX}" cy="{CY}" r="64" fill="#0078D4" stroke="#FFFFFF" stroke-width="4"/>')
for g in groups:
    x,y=P[g]; out.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="52" fill="url(#gm)"/><circle cx="{x:.1f}" cy="{y:.1f}" r="24" fill="#E3008C" stroke="#FFFFFF" stroke-width="3"/>')
for m in mods:
    x,y=P[m]; out.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="15" fill="#5C6370" stroke="#FFFFFF" stroke-width="3"/>')
out.append('</svg>')
open('deck/art/model_graph.svg','w').write(''.join(out))
print(json.dumps({k:(round(v[0]),round(v[1])) for k,v in P.items()}))
