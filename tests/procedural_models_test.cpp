// Tests for src/ProceduralModels.{h,cpp}: the built-in pine and rock. No Direct3D, no framework.
//   Every part has unit normals; every surface is consistently wound (each directed edge appears once, with its reverse once)
//   and closed - except the trunk, which is an open tube (its base is buried, its top is under the first tier of needles);
//   the signed volume is positive, which for the engine's left-handed clockwise convention means the faces point OUTWARD.
#include "ProceduralModels.h"
#include <cstdio>
#include <map>
#include <set>
#include <tuple>
#include <vector>
#include <cmath>
static int fails=0;
#define CHECK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n",__LINE__,#c); ++fails;} }while(0)
static void Check(const char* id, int expectParts)
{
    ImportedModel m; CHECK(MakeBuiltinModel(id, m)); CHECK((int)m.parts.size()==expectParts);
    double volume=0; size_t tris=0;
    for(auto& p: m.parts){
        CHECK(!p.vertices.empty()); CHECK(p.indices.size()%3==0);
        // weld by position and check every edge is used by exactly two triangles (closed surface)
        std::map<std::tuple<int,int,int>,int> weld; std::vector<int> id(p.vertices.size());
        for(size_t i=0;i<p.vertices.size();++i){ auto&v=p.vertices[i].position; auto k=std::make_tuple((int)std::lround(v.x*1000),(int)std::lround(v.y*1000),(int)std::lround(v.z*1000)); auto f=weld.find(k); if(f==weld.end()){ f=weld.emplace(k,(int)weld.size()).first;} id[i]=f->second; }
        std::map<std::pair<int,int>,int> edges;
        for(size_t t=0;t<p.indices.size();t+=3){ int a=id[p.indices[t]],b=id[p.indices[t+1]],c=id[p.indices[t+2]]; CHECK(a!=b&&b!=c&&a!=c); edges[{a,b}]++; edges[{b,c}]++; edges[{c,a}]++;
            auto&A=p.vertices[p.indices[t]].position,&B=p.vertices[p.indices[t+1]].position,&C=p.vertices[p.indices[t+2]].position;
            volume += (A.x*(B.y*C.z-B.z*C.y) - A.y*(B.x*C.z-B.z*C.x) + A.z*(B.x*C.y-B.y*C.x))/6.0; }
        // every directed edge must appear once, and its reverse once (consistent winding, closed)
        int bad=0; for(auto&e: edges){ if(e.second!=1) ++bad; auto r=edges.find({e.first.second,e.first.first}); if(r==edges.end()||r->second!=1) ++bad; }
        printf("  part %s: %zu verts %zu tris, bad edges %d\n", p.name.c_str(), p.vertices.size(), p.indices.size()/3, bad);
        CHECK(bad==(p.name=="Trunk"?28:0)); tris+=p.indices.size()/3;
        for(auto&v:p.vertices){ float l=std::sqrt(v.normal.x*v.normal.x+v.normal.y*v.normal.y+v.normal.z*v.normal.z); CHECK(std::fabs(l-1)<1e-3); }
    }
    printf("%s: %zu triangles, signed volume %.2f, bounds y %.2f..%.2f x %.2f..%.2f\n", id, tris, volume, m.boundsMin.y, m.boundsMax.y, m.boundsMin.x, m.boundsMax.x);
    CHECK(volume > 0);
    CHECK(m.triangleCount==tris);
}
int main(){ Check("builtin:pine",2); Check("builtin:rock",1); ImportedModel m; CHECK(!MakeBuiltinModel("builtin:nope",m)); printf(fails?"FAILED\n":"ok\n"); return fails; }
