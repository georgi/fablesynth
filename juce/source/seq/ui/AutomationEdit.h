// Editing geometry and point operations, matching src/seq/clipAutomation.ts.
#pragma once
#include "../../dsp/ClipAutomation.h"
namespace fable::autoedit {
constexpr double tick = .25;
inline double quant(double t) { return std::round(t / tick) * tick; }
inline std::vector<AutoPoint> tidy(std::vector<AutoPoint> points) {
    for (auto& p : points) { p.t = quant(std::max(0.0,p.t)); p.v = std::clamp(p.v,0.0,1.0); }
    std::stable_sort(points.begin(),points.end(),[](const auto& a,const auto& b){ return a.t < b.t; });
    std::vector<AutoPoint> out;
    for (size_t i = 0; i < points.size(); ++i) {
        size_t first = i, last = i;
        while (first && points[first-1].t == points[i].t) --first;
        while (last+1 < points.size() && points[last+1].t == points[i].t) ++last;
        if (last-first >= 2 && i != first && i != last) continue;
        const auto& p = points[i];
        if (!out.empty() && out.back().t == p.t && (out.back().v == p.v
            || (out.size() > 1 && out[out.size()-2].hold && out[out.size()-2].t < p.t))) out.back() = p;
        else out.push_back(p);
    }
    if (out.size() > AUTO_MAX_POINTS) out.resize(AUTO_MAX_POINTS);
    return out;
}
inline bool audible(const std::vector<AutoPoint>& points,int len) { return !points.empty() && points.front().t < len; }
inline std::vector<AutoPoint> splice(const std::vector<AutoPoint>& points,int len,double t0,double t1,const std::vector<AutoPoint>& insert) {
    std::vector<AutoPoint> out;
    for (const auto& p : points) if (p.t < t0) out.push_back(p);
    if (audible(points,len)) out.push_back({t0,evalLane(points,len,t0)});
    out.insert(out.end(),insert.begin(),insert.end());
    bool edge = false; for (const auto& p : points) edge |= p.t == t1;
    if (audible(points,len) && t1 < len && !edge) out.push_back({t1,evalLane(points,len,t1)});
    for (const auto& p : points) if (p.t >= t1) out.push_back(p);
    return tidy(std::move(out));
}
inline std::vector<AutoPoint> paintCell(const std::vector<AutoPoint>& points,int len,double t,double v,double grid) {
    double t0 = std::floor(std::clamp(t,0.0,len-tick)/grid)*grid;
    return splice(points,len,t0,std::min((double)len,t0+grid),{{t0,std::clamp(v,0.0,1.0),0,true}});
}
inline std::vector<AutoPoint> drawLine(const std::vector<AutoPoint>& points,int len,double ta,double va,double tb,double vb) {
    if (tb < ta) { std::swap(ta,tb); std::swap(va,vb); }
    ta = quant(std::clamp(ta,0.0,len-tick)); tb = quant(std::clamp(tb,0.0,len-tick));
    if (tb-ta < tick) return paintCell(points,len,ta,va,tick);
    return splice(points,len,ta,tb,{{ta,std::clamp(va,0.0,1.0)},{tb,std::clamp(vb,0.0,1.0)}});
}
inline std::vector<AutoPoint> movePoint(std::vector<AutoPoint> points,int len,int i,double t,double v) {
    if (i < 0 || i >= (int)points.size()) return points;
    points[(size_t)i].t = std::min(i+1 < (int)points.size() ? points[(size_t)i+1].t : len-tick,
        std::max(i > 0 ? points[(size_t)i-1].t : 0.0,quant(t)));
    points[(size_t)i].v = std::clamp(v,0.0,1.0); return points;
}
inline std::pair<std::vector<AutoPoint>,int> addPoint(std::vector<AutoPoint> points,double t,double v) {
    t = quant(t); points.erase(std::remove_if(points.begin(),points.end(),[&](const auto& p){ return p.t == t; }),points.end());
    points.push_back({t,std::clamp(v,0.0,1.0)}); points = tidy(std::move(points));
    for (int i = 0; i < (int)points.size(); ++i) if (points[(size_t)i].t == t) return {points,i};
    return {points,-1};
}
inline std::vector<AutoPoint> removePoint(std::vector<AutoPoint> points,int i) {
    if (i >= 0 && i < (int)points.size()) points.erase(points.begin()+i);
    return tidy(std::move(points));
}
inline std::vector<AutoPoint> bendSegment(std::vector<AutoPoint> points,int i,double c) {
    if (i >= 0 && i < (int)points.size()) { points[(size_t)i].c = std::clamp(c,-1.0,1.0); points[(size_t)i].hold = false; }
    return points;
}
struct Geometry {
    AutoCycle cycle; double unit,span,axis; int clipSteps;
    Geometry(AutoCycle c,int steps) : cycle(c),unit(c.fit ? c.fit*4.0/c.len : 1),span(c.len*unit),axis(std::max((double)steps,span)),clipSteps(steps) {}
    double toClip(double t) const { return autoMod(t+cycle.rot,cycle.len)*unit; }
    double toLane(double s) const { return autoMod(autoMod(s,span)/unit-cycle.rot,cycle.len); }
    double phase(double steps) const { return autoMod((cycle.fit ? std::fmod(steps/4,cycle.fit)/cycle.fit*cycle.len : steps)-cycle.rot,cycle.len); }
};
}
