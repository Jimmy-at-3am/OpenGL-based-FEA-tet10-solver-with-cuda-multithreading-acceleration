// mesh_diag.cpp  -- Standalone diagnostic for revenge spindex mesh issues
// Compile & run:  build.bat diag
// Outputs edge-sharing stats, manifold check, boundary face counts

#include "tetgen.h"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <iostream>
#include <fstream>
#include <vector>
#include <unordered_map>
#include <tuple>
#include <cmath>
#include <algorithm>
#include <map>
#include <set>
#include <string>

using namespace std;

// ---------- Vertex welding (same as model_loading.cpp) ----------
struct RawTri { glm::vec3 n, v0, v1, v2; };

using IKey = tuple<int64_t,int64_t,int64_t>;
struct IKeyHash {
    size_t operator()(const IKey& k) const {
        size_t h = 0;
        auto mix = [&](int64_t v){ h ^= hash<int64_t>{}(v)+0x9e3779b97f4a7c15ULL+(h<<6)+(h>>2); };
        mix(get<0>(k)); mix(get<1>(k)); mix(get<2>(k));
        return h;
    }
};

struct WeldedMesh {
    vector<glm::vec3> positions;
    vector<unsigned int> indices;   // triangles
};

WeldedMesh weldMesh(const vector<RawTri>& tris, float tol=1e-6f)
{
    WeldedMesh m;
    unordered_map<IKey,unsigned int,IKeyHash> vmap;
    auto snap = [&](float v)->int64_t{ return (int64_t)round(v/tol); };
    auto getOrAdd = [&](glm::vec3 p)->unsigned int {
        IKey k={snap(p.x),snap(p.y),snap(p.z)};
        auto it=vmap.find(k);
        if(it!=vmap.end()) return it->second;
        unsigned int id=(unsigned int)m.positions.size();
        vmap[k]=id; m.positions.push_back(p); return id;
    };
    for(auto& t:tris){
        unsigned i0=getOrAdd(t.v0), i1=getOrAdd(t.v1), i2=getOrAdd(t.v2);
        if(i0==i1||i1==i2||i0==i2) continue;
        m.indices.push_back(i0); m.indices.push_back(i1); m.indices.push_back(i2);
    }
    return m;
}

// ----- Check manifoldness: every edge should appear exactly twice -----
void checkManifold(const WeldedMesh& m)
{
    map<pair<unsigned,unsigned>,int> edgeCount;
    auto addEdge = [&](unsigned a, unsigned b){
        auto key = make_pair(min(a,b),max(a,b));
        edgeCount[key]++;
    };
    for(size_t i=0;i<m.indices.size();i+=3){
        unsigned a=m.indices[i],b=m.indices[i+1],c=m.indices[i+2];
        addEdge(a,b); addEdge(b,c); addEdge(c,a);
    }
    int boundary=0, nonManifold=0;
    for(auto& kv:edgeCount){
        if(kv.second==1) boundary++;
        else if(kv.second>2) nonManifold++;
    }
    cout << "\n=== Manifold Check ===" << endl;
    cout << "  Unique edges    : " << edgeCount.size() << endl;
    cout << "  Manifold (x2)   : " << (edgeCount.size()-boundary-nonManifold) << endl;
    cout << "  Boundary (x1)   : " << boundary << " (HOLES in surface!)" << endl;
    cout << "  Non-manifold(>2): " << nonManifold << endl;
    if(boundary==0 && nonManifold==0)
        cout << "  >> Surface is a valid closed 2-manifold. Good for TetGen!" << endl;
    else
        cout << "  >> WARNING: Surface has topology problems - TetGen may produce bad results!" << endl;

    // --- Valence distribution ---
    map<unsigned,int> valenceCount;
    for(size_t i=0;i<m.indices.size();i+=3){
        unsigned a=m.indices[i],b=m.indices[i+1],c=m.indices[i+2];
        valenceCount[a]++; valenceCount[b]++; valenceCount[c]++;
    }
    cout << "\n=== Vertex Valence Distribution ===" << endl;
    map<int,int> valDist;
    int maxVal=0; unsigned maxVert=0;
    for(auto& kv:valenceCount){ valDist[kv.second]++; if(kv.second>maxVal){maxVal=kv.second;maxVert=kv.first;} }
    for(auto& kv:valDist)
        if(kv.first>8) cout<<"  Valence "<<kv.first<<": "<<kv.second<<" vertices"<<endl;
    cout<<"  Max valence: "<<maxVal<<" (vertex "<<maxVert<<")"<<endl;
}

// ----- Measure max edge aspect ratio across all tets -----
void analyzeTetQuality(const tetgenio& out)
{
    cout << "\n=== Tetrahedron Quality ===" << endl;
    double worstAR=0, sumAR=0;
    int slivers=0;
    for(int i=0;i<out.numberoftetrahedra;++i){
        int p[4];
        for(int j=0;j<4;j++) p[j]=out.tetrahedronlist[i*4+j];
        glm::dvec3 v[4];
        for(int j=0;j<4;j++){
            v[j]={out.pointlist[p[j]*3+0],out.pointlist[p[j]*3+1],out.pointlist[p[j]*3+2]};
        }
        // Compute shortest and longest edge
        double eMin=1e18, eMax=0;
        int pairs[6][2]={{0,1},{0,2},{0,3},{1,2},{1,3},{2,3}};
        for(auto& pr:pairs){
            double len=glm::length(v[pr[1]]-v[pr[0]]);
            eMin=min(eMin,len); eMax=max(eMax,len);
        }
        double ar = (eMin>1e-15) ? eMax/eMin : 1e15;
        worstAR=max(worstAR,ar);
        sumAR+=ar;
        if(ar>100) slivers++;
    }
    cout << "  Total tets      : " << out.numberoftetrahedra << endl;
    cout << "  Sliver tets(>100): " << slivers << endl;
    cout << "  Avg aspect ratio: " << sumAR/max(1,out.numberoftetrahedra) << endl;
    cout << "  Worst AR        : " << worstAR << endl;
}

// ----- Count boundary faces (faces owned by only 1 tet) -----
void analyzeBoundaryFaces(const tetgenio& out)
{
    cout << "\n=== Boundary Face Analysis ===" << endl;
    cout << "  Total tri-faces : " << out.numberoftrifaces << endl;
    int boundaryFaces=0, internalFaces=0;
    for(int i=0;i<out.numberoftrifaces;++i){
        if(out.trifacemarkerlist && out.trifacemarkerlist[i]!=0)
            boundaryFaces++;
        else
            internalFaces++;
    }
    cout << "  Boundary faces  : " << boundaryFaces << endl;
    cout << "  Internal faces  : " << internalFaces << endl;
    cout << "  Tets x4 faces   : " << out.numberoftetrahedra*4
         << " (current code draws ALL of these)" << endl;
    cout << "  >> Drawing all tet-faces = " << out.numberoftetrahedra*4
         << " vs boundary-only = " << boundaryFaces << endl;
}

int main(int argc, char* argv[])
{
    string filepath = (argc>1) ? argv[1] : "revenge spindex - Part 33 (6).stl";
    cout << "Loading: " << filepath << endl;

    // --- Load STL ---
    ifstream file(filepath, ios::binary|ios::ate);
    if(!file){ cerr << "Cannot open file!" << endl; return 1; }
    streamsize sz=file.tellg(); file.seekg(0,ios::beg);
    if(sz<84){ cerr << "Too small!" << endl; return 1; }
    char header[80]; file.read(header,80);
    uint32_t numTris; file.read(reinterpret_cast<char*>(&numTris),4);
    cout << "Raw triangles: " << numTris << endl;

    vector<RawTri> rawTris;
    rawTris.reserve(numTris);
    for(uint32_t i=0;i<numTris;++i){
        RawTri t; float n[3],v0[3],v1[3],v2[3]; uint16_t attr;
        file.read((char*)n,12); file.read((char*)v0,12);
        file.read((char*)v1,12); file.read((char*)v2,12);
        file.read((char*)&attr,2);
        t.n={n[0],n[1],n[2]}; t.v0={v0[0],v0[1],v0[2]};
        t.v1={v1[0],v1[1],v1[2]}; t.v2={v2[0],v2[1],v2[2]};
        rawTris.push_back(t);
    }

    // --- Weld ---
    WeldedMesh m = weldMesh(rawTris);
    cout << "After weld: " << m.positions.size() << " verts, "
         << m.indices.size()/3 << " tris" << endl;

    // --- Manifold check ---
    checkManifold(m);

    // --- TetGen ---
    tetgenio in, out; in.firstnumber=0;
    in.numberofpoints=(int)m.positions.size();
    in.pointlist=new REAL[in.numberofpoints*3];
    // Auto-center/scale
    glm::vec3 mn=m.positions[0],mx=m.positions[0];
    for(auto& p:m.positions){mn=glm::min(mn,p);mx=glm::max(mx,p);}
    glm::vec3 ctr=(mn+mx)*0.5f;
    float maxD=max({mx.x-mn.x,mx.y-mn.y,mx.z-mn.z}); if(maxD<1e-6f)maxD=1;
    float sc=3.0f/maxD;
    for(int i=0;i<in.numberofpoints;++i){
        glm::vec3 p=(m.positions[i]-ctr)*sc;
        in.pointlist[i*3+0]=p.x;in.pointlist[i*3+1]=p.y;in.pointlist[i*3+2]=p.z;
    }
    int nFacets=(int)(m.indices.size()/3);
    in.numberoffacets=nFacets;
    in.facetlist=new tetgenio::facet[nFacets];
    in.facetmarkerlist=new int[nFacets];
    for(int i=0;i<nFacets;++i){
        tetgenio::facet* f=&in.facetlist[i];
        f->polygonlist=new tetgenio::polygon[1];
        f->numberofpolygons=1;f->holelist=NULL;f->numberofholes=0;
        tetgenio::polygon* p=&f->polygonlist[0];
        p->numberofvertices=3; p->vertexlist=new int[3];
        p->vertexlist[0]=m.indices[i*3];p->vertexlist[1]=m.indices[i*3+1];p->vertexlist[2]=m.indices[i*3+2];
        in.facetmarkerlist[i]=1;
    }

    cout << "\nRunning TetGen (pq1.4a0.5)..." << endl;
    try{
        tetgenbehavior b; b.parse_commandline((char*)"pq1.4a0.5");
        tetrahedralize(&b,&in,&out);
    } catch(int e){ cerr<<"TetGen error: "<<e<<endl; return 1; }

    analyzeTetQuality(out);
    analyzeBoundaryFaces(out);

    cout << "\n=== DIAGNOSIS SUMMARY ===" << endl;
    cout << "Problem 1 (visualization): The code pushes ALL 4 faces per tet." << endl;
    cout << "  Internal shared faces are drawn TWICE, creating false line crossings." << endl;
    cout << "  Fix: use out.trifacelist (boundary faces only) for rendering." << endl;
    cout << "Problem 2 (sliver tets): Check Worst AR above." << endl;
    cout << "  If AR > 1000, surface manifold issues are causing TetGen bad elements." << endl;

    return 0;
}
