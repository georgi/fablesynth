#include "Engine.h"
#include <algorithm>
#include <cmath>

namespace fable {

static constexpr double PI = 3.14159265358979323846;
static constexpr double LN2 = 0.6931471805599453;
static const double DC_R = 0.9998; // ~3.5 Hz highpass

// Vowel formants (A-E-I-O-U) for the VOWEL filter morph.
static const double VOWELS[5][3] = {
    {730, 1090, 2440}, {530, 1840, 2480}, {390, 1990, 2550}, {570, 840, 2410}, {440, 1020, 2240}};
static const double F_AMPS[3] = {1, 0.55, 0.32};

// Numerically stable ln(cosh(z)) — antiderivative of tanh, used by the ADAA saturator.
static inline double lcosh(double z) {
    double a = std::abs(z);
    return a + std::log1p(std::exp(-2 * a)) - LN2;
}

// ---------------- Env ----------------
void Env::set(double a, double d, double sus, double r, double sr) {
    s = sus;
    if (a != a_ || d != d_ || r != r_) {
        a_ = a; d_ = d; r_ = r;
        ca = 1 - std::exp(-1 / (std::max(0.0008, a) * sr));
        cd = 1 - std::exp(-1 / (std::max(0.002, d / 4.5) * sr));
        cr = 1 - std::exp(-1 / (std::max(0.002, r / 4.5) * sr));
    }
}
double Env::process() {
    switch (state) {
        case 1:
            level += (1.45 - level) * ca;
            if (level >= 1) { level = 1; state = 2; }
            break;
        case 2:
            level += (s - level) * cd;
            if (level - s < 0.0005) state = 3;
            break;
        case 3: level = s; break;
        case 4:
            level -= level * cr;
            if (level < 1e-4) { level = 0; state = 0; }
            break;
    }
    return level;
}

// ---------------- Lfo ----------------
double Lfo::value(int shape) const {
    double p = phase;
    switch (shape) {
        case 0: return std::sin(2 * PI * p);
        case 1: return 1 - 4 * std::abs(p - 0.5);
        case 2: return 1 - 2 * p;
        case 3: return p < 0.5 ? 1 : -1;
        default: return hold;
    }
}
void Lfo::advance(double rate, int n, double sr) {
    phase += (rate * n) / sr;
    if (phase >= 1) { phase -= std::floor(phase); hold = rng->next() * 2 - 1; }
}

// ---------------- FilterState ----------------
void FilterState::reset() {
    for (auto& x : svf) x = 0;
    for (auto& x : fmt) x = 0;
    combL.fill(0); combR.fill(0); combW = 0;
    cutSm = 0; satXL = 0; satXR = 0;
}

// ---------------- Voice ----------------
void Voice::noteOn(int n, double v, double startPitch, long a, Rng& rng) {
    note = n; vel = v; gate = true; age = a; pitch = startPitch;
    velGain = 0.25 + 0.75 * v * v;
    ampEnv.trigger(); modEnv.trigger();
    lfo1.rng = &rng; lfo2.rng = &rng;
    lfo1.reset(); lfo2.reset();
    for (int i = 0; i < MAXUNI; i++) { oA.phases[i] = rng.next(); oB.phases[i] = rng.next(); }
    oA.posSm = -1; oB.posSm = -1;
    subPhase = 0;
    f1.reset(); f2.reset();
    dcxL = dcxR = dcyL = dcyR = 0;
}

// ---------------- Engine ----------------
void Engine::prepare(double sampleRate) {
    sr_ = sampleRate;
    for (auto& v : voices_) { v.lfo1.rng = &rng_; v.lfo2.rng = &rng_; }
}

void Engine::setTables(const std::vector<GeneratedTable>& tables) {
    tables_.clear();
    for (const auto& t : tables) {
        EngineTable e;
        e.frames = t.frames; e.mips = t.mips; e.size = t.size; e.mask = t.size - 1;
        e.data = t.data;
        tables_.push_back(std::move(e));
    }
}

void Engine::noteOn(int n, double vel) {
    Voice* voice = nullptr;
    for (auto& v : voices_) if (v.gate && v.note == n) { voice = &v; break; }
    if (!voice) for (auto& v : voices_) if (!v.active()) { voice = &v; break; }
    if (!voice) {
        Voice* best = nullptr;
        for (auto& v : voices_) {
            if (!best) { best = &v; continue; }
            int vRel = v.gate ? 1 : 0, bRel = best->gate ? 1 : 0;
            if (vRel < bRel || (vRel == bRel && v.age < best->age)) best = &v;
        }
        voice = best;
    }
    double glide = p_[MASTER_GLIDE];
    double start = glide > 0.001 ? lastPitch_ : n;
    voice->noteOn(n, vel, start, clock_++, rng_);
    lastPitch_ = n;
}

void Engine::noteOff(int n) {
    for (auto& v : voices_) if (v.gate && v.note == n) v.noteOff();
}

// Configure one oscillator's per-block render state. Returns true if audible.
bool Engine::setupOsc(OscState& o, int base, Voice& v, double mPos, double mPitch, double mLvl, double mPan) {
    if (p_[base + OSC_ON] < 0.5) return false;
    int ti = (int)p_[base + OSC_TABLE];
    if (ti < 0 || ti >= (int)tables_.size()) return false;
    const EngineTable& table = tables_[ti];

    double basePitch = v.pitch + bend_ + p_[base + OSC_OCT] * 12 + p_[base + OSC_SEMI]
                     + p_[base + OSC_FINE] / 100.0 + mPitch * 12;
    double freq = 440 * std::pow(2.0, (basePitch - 69) / 12);
    if (freq <= 0 || freq > sr_ * 0.45) return false;

    double level = std::min(1.2, std::max(0.0, (double)p_[base + OSC_LEVEL] + mLvl));
    level *= level;
    if (level < 1e-5) return false;

    int uni = std::max(1, std::min(MAXUNI, (int)p_[base + OSC_UNISON]));
    double det = p_[base + OSC_DETUNE];
    double spr = p_[base + OSC_SPREAD];
    double basePan = std::max(-1.0, std::min(1.0, (double)p_[base + OSC_PAN] + mPan));

    double pos = std::min(1.0, std::max(0.0, (double)p_[base + OSC_POS] + mPos));
    if (o.posSm < 0) o.posSm = pos;
    o.posSm += (pos - o.posSm) * 0.35;
    double posF = o.posSm * (table.frames - 1);
    int f0 = (int)posF;
    int f1 = std::min(table.frames - 1, f0 + 1);
    o.ft = posF - f0;

    double cps = freq / sr_;
    double maxRatio = std::pow(2.0, (det * 50) / 1200);

    const double W = 0.07;
    double mipF = std::log2((cps * maxRatio * 1024) / 0.475);
    int mip = 0; double mipBlend = 0;
    if (mipF > 0) {
        mip = std::min(table.mips - 1, (int)std::ceil(mipF));
        double over = mipF - (mip - 1);
        if (over < W) mipBlend = 1 - over / W;
    }
    int fineMip = mip > 0 ? mip - 1 : 0;

    o.off0  = (f0 * table.mips + mip) * table.size;
    o.off1  = (f1 * table.mips + mip) * table.size;
    o.off0b = (f0 * table.mips + fineMip) * table.size;
    o.off1b = (f1 * table.mips + fineMip) * table.size;
    o.mipBlend = mipBlend;
    o.data = table.data.data();
    o.mask = table.mask;
    o.size = table.size;
    o.uni = uni;
    o.gain = (level * 0.32) / std::sqrt((double)uni);

    for (int u = 0; u < uni; u++) {
        double sprd = uni > 1 ? (double)u / (uni - 1) * 2 - 1 : 0;
        double cents = sprd * det * 50;
        double ratio = std::pow(2.0, cents / 1200);
        o.incs[u] = cps * ratio * table.size;
        double pan = std::max(-1.0, std::min(1.0, sprd * spr + basePan));
        double a = ((pan + 1) * PI) / 4;
        o.gl[u] = (float)std::cos(a);
        o.gr[u] = (float)std::sin(a);
    }
    return true;
}

void Engine::renderOsc(OscState& o, float* tmpL, float* tmpR, int n) {
    const float* data = o.data; int mask = o.mask, size = o.size;
    double ft = o.ft, g = o.gain;
    int off0 = o.off0, off1 = o.off1;
    double blend = o.mipBlend;
    if (blend < 0.001) {
        for (int u = 0; u < o.uni; u++) {
            double ph = o.phases[u], inc = o.incs[u];
            double gl = o.gl[u] * g, gr = o.gr[u] * g;
            for (int i = 0; i < n; i++) {
                int idx = (int)ph;
                double frac = ph - idx;
                int i2 = (idx + 1) & mask;
                double s0 = data[off0 + idx] + frac * (data[off0 + i2] - data[off0 + idx]);
                double s1 = data[off1 + idx] + frac * (data[off1 + i2] - data[off1 + idx]);
                double s = s0 + ft * (s1 - s0);
                tmpL[i] += (float)(s * gl);
                tmpR[i] += (float)(s * gr);
                ph += inc; if (ph >= size) ph -= size;
            }
            o.phases[u] = ph;
        }
    } else {
        int off0b = o.off0b, off1b = o.off1b;
        for (int u = 0; u < o.uni; u++) {
            double ph = o.phases[u], inc = o.incs[u];
            double gl = o.gl[u] * g, gr = o.gr[u] * g;
            for (int i = 0; i < n; i++) {
                int idx = (int)ph;
                double frac = ph - idx;
                int i2 = (idx + 1) & mask;
                double sc0 = data[off0 + idx] + frac * (data[off0 + i2] - data[off0 + idx]);
                double sc1 = data[off1 + idx] + frac * (data[off1 + i2] - data[off1 + idx]);
                double sc = sc0 + ft * (sc1 - sc0);
                double sf0 = data[off0b + idx] + frac * (data[off0b + i2] - data[off0b + idx]);
                double sf1 = data[off1b + idx] + frac * (data[off1b + i2] - data[off1b + idx]);
                double sf = sf0 + ft * (sf1 - sf0);
                double s = sc + blend * (sf - sc);
                tmpL[i] += (float)(s * gl);
                tmpR[i] += (float)(s * gr);
                ph += inc; if (ph >= size) ph -= size;
            }
            o.phases[u] = ph;
        }
    }
}

void Engine::setupFilter(FilterState& fs, int base, Voice& v, double e2, double mCut, double mRes) {
    int ftype = (int)p_[base + FLT_TYPE];
    fs.ftype = ftype;

    double fc = p_[base + FLT_CUTOFF] *
        std::pow(2.0, p_[base + FLT_ENV] * 4 * e2 + (p_[base + FLT_KEY] * (v.note - 60)) / 12.0 + mCut * 5);
    fc = std::min(sr_ * 0.45, std::max(20.0, fc));
    if (fs.cutSm <= 0) fs.cutSm = fc;
    fs.cutSm += (fc - fs.cutSm) * 0.5;
    double cut = fs.cutSm;
    double res = std::min(0.999, std::max(0.0, (double)p_[base + FLT_RES] + mRes));

    if (ftype <= 4) {
        fs.twoPole = ftype == 1;
        double g = std::tan((PI * cut) / sr_);
        double k = 2 - 1.93 * res;
        fs.k1 = k;
        fs.a1 = 1 / (1 + g * (g + k));
        fs.a2 = g * fs.a1;
        fs.a3 = g * fs.a2;
    } else if (ftype == 5) {
        double len = sr_ / cut;
        len = std::min((double)COMB_MAX - 2, std::max(1.0, len));
        fs.combLen = len;
        fs.combFb = res * 0.97;
    } else {
        double norm = std::min(0.999, std::max(0.0, std::log(cut / 20) / std::log(1000.0)));
        double pos = norm * 4;
        int vi = std::min(3, (int)pos);
        double fr = pos - vi;
        double q = 2 + res * 22;
        for (int j = 0; j < 3; j++) {
            double f0 = std::min(sr_ * 0.45, VOWELS[vi][j] + (VOWELS[vi + 1][j] - VOWELS[vi][j]) * fr);
            double w0 = (2 * PI * f0) / sr_;
            double alpha = std::sin(w0) / (2 * q);
            double a0 = 1 + alpha;
            fs.fc[j * 3]     = alpha / a0;
            fs.fc[j * 3 + 1] = (-2 * std::cos(w0)) / a0;
            fs.fc[j * 3 + 2] = (1 - alpha) / a0;
            fs.famp[j] = F_AMPS[j];
        }
    }
}

void Engine::runFilter(FilterState& fs, const float* inL, const float* inR,
                       float* outL, float* outR, double drive, int n) {
    if (drive > 0.005) {
        double dg = 1 + drive * 7;
        double dcomp = 1 / std::pow(dg, 0.55);
        double kF = dcomp / dg;
        double xpL = fs.satXL, xpR = fs.satXR;
        double FpL = kF * lcosh(dg * xpL), FpR = kF * lcosh(dg * xpR);
        for (int i = 0; i < n; i++) {
            double aL = inL[i], aR = inR[i];
            double dxL = aL - xpL;
            double FL = kF * lcosh(dg * aL);
            outL[i] = (float)((dxL > 1e-5 || dxL < -1e-5) ? (FL - FpL) / dxL : dcomp * std::tanh(dg * 0.5 * (aL + xpL)));
            xpL = aL; FpL = FL;
            double dxR = aR - xpR;
            double FR = kF * lcosh(dg * aR);
            outR[i] = (float)((dxR > 1e-5 || dxR < -1e-5) ? (FR - FpR) / dxR : dcomp * std::tanh(dg * 0.5 * (aR + xpR)));
            xpR = aR; FpR = FR;
        }
        fs.satXL = xpL; fs.satXR = xpR;
    } else {
        for (int i = 0; i < n; i++) { outL[i] = inL[i]; outR[i] = inR[i]; }
        if (n > 0) { fs.satXL = inL[n - 1]; fs.satXR = inR[n - 1]; }
    }

    int ftype = fs.ftype;
    if (ftype <= 4) {
        double a1 = fs.a1, a2 = fs.a2, a3 = fs.a3, k1 = fs.k1;
        double* F = fs.svf;
        for (int ch = 0; ch < 2; ch++) {
            float* buf = ch == 0 ? outL : outR;
            int o1 = ch * 2;
            double ic1 = F[o1], ic2 = F[o1 + 1];
            for (int i = 0; i < n; i++) {
                double x = buf[i];
                double v3 = x - ic2;
                double v1 = a1 * ic1 + a2 * v3;
                double v2 = ic2 + a2 * ic1 + a3 * v3;
                ic1 = 2 * v1 - ic1;
                ic2 = 2 * v2 - ic2;
                switch (ftype) {
                    case 0: case 1: buf[i] = (float)v2; break;
                    case 2: buf[i] = (float)(k1 * v1); break;
                    case 3: buf[i] = (float)(x - k1 * v1 - v2); break;
                    default: buf[i] = (float)(x - k1 * v1); break;
                }
            }
            F[o1] = ic1; F[o1 + 1] = ic2;
        }
        if (fs.twoPole) {
            for (int ch = 0; ch < 2; ch++) {
                float* buf = ch == 0 ? outL : outR;
                int o1 = 4 + ch * 2;
                double ic1 = F[o1], ic2 = F[o1 + 1];
                for (int i = 0; i < n; i++) {
                    double x = buf[i];
                    double v3 = x - ic2;
                    double v1 = a1 * ic1 + a2 * v3;
                    double v2 = ic2 + a2 * ic1 + a3 * v3;
                    ic1 = 2 * v1 - ic1;
                    ic2 = 2 * v2 - ic2;
                    buf[i] = (float)v2;
                }
                F[o1] = ic1; F[o1 + 1] = ic2;
            }
        }
    } else if (ftype == 5) {
        double len = fs.combLen, fb = fs.combFb, g0 = 1 - fb;
        float* cl = fs.combL.data(); float* cr = fs.combR.data();
        int w = fs.combW;
        for (int i = 0; i < n; i++) {
            double rd = w - len;
            rd = std::fmod(std::fmod(rd, (double)COMB_MAX) + COMB_MAX, (double)COMB_MAX);
            int i0 = (int)rd;
            double frac = rd - i0;
            int i1 = i0 + 1 < COMB_MAX ? i0 + 1 : 0;
            double yL = g0 * outL[i] + fb * (cl[i0] + frac * (cl[i1] - cl[i0]));
            double yR = g0 * outR[i] + fb * (cr[i0] + frac * (cr[i1] - cr[i0]));
            cl[w] = (float)yL; cr[w] = (float)yR;
            outL[i] = (float)yL; outR[i] = (float)yR;
            w = w + 1 < COMB_MAX ? w + 1 : 0;
        }
        fs.combW = w;
    } else {
        const double* fc = fs.fc; const double* fa = fs.famp; double* z = fs.fmt;
        for (int ch = 0; ch < 2; ch++) {
            float* buf = ch == 0 ? outL : outR;
            int zb = ch * 6;
            for (int i = 0; i < n; i++) {
                double x = buf[i];
                double acc = 0.04 * x;
                for (int j = 0; j < 3; j++) {
                    double b0 = fc[j * 3], ca1 = fc[j * 3 + 1], ca2 = fc[j * 3 + 2];
                    int zi = zb + j * 2;
                    double y = b0 * x + z[zi];
                    z[zi] = z[zi + 1] - ca1 * y;
                    z[zi + 1] = -b0 * x - ca2 * y;
                    acc += fa[j] * y;
                }
                buf[i] = (float)(acc * 0.8);
            }
        }
    }
}

void Engine::renderVoice(Voice& v, float* L, float* R, int n) {
    v.ampEnv.set(p_[ENV1_BASE + 0], p_[ENV1_BASE + 1], p_[ENV1_BASE + 2], p_[ENV1_BASE + 3], sr_);
    v.modEnv.set(p_[ENV2_BASE + 0], p_[ENV2_BASE + 1], p_[ENV2_BASE + 2], p_[ENV2_BASE + 3], sr_);

    double gl = p_[MASTER_GLIDE];
    if (gl > 0.001) {
        double c = 1 - std::exp(-(double)n / (gl * 0.3 * sr_ + 1));
        v.pitch += (v.note - v.pitch) * c;
    } else v.pitch = v.note;

    double l1 = v.lfo1.value((int)p_[LFO1_BASE + 0]);
    double l2 = v.lfo2.value((int)p_[LFO2_BASE + 0]);
    double e2 = v.modEnv.level;
    double srcs[6] = {0, l1, l2, e2, v.vel, (v.note - 60) / 24.0};

    double mPosA = 0, mPosB = 0, mCut = 0, mPitch = 0, mAmp = 0, mPan = 0, mLvlA = 0, mLvlB = 0;
    double mCut2 = 0, mRes2 = 0;
    for (int s = 1; s <= 4; s++) {
        int b = matBase(s);
        int src = (int)p_[b + MAT_SRC];
        int dst = (int)p_[b + MAT_DST];
        if (!src || !dst) continue;
        double x = srcs[src] * p_[b + MAT_AMT];
        switch (dst) {
            case 1: mPosA += x; break;
            case 2: mPosB += x; break;
            case 3: mCut += x; break;
            case 4: mPitch += x; break;
            case 5: mAmp += x; break;
            case 6: mPan += x; break;
            case 7: mLvlA += x; break;
            case 8: mLvlB += x; break;
            case 9: mCut2 += x; break;
            case 10: mRes2 += x; break;
        }
    }

    int route = (int)p_[FILTER_ROUTE];
    bool split = route == 2;

    std::fill(tmpL_, tmpL_ + n, 0.0f);
    std::fill(tmpR_, tmpR_ + n, 0.0f);
    if (split) { std::fill(bL_, bL_ + n, 0.0f); std::fill(bR_, bR_ + n, 0.0f); }

    bool aOn = setupOsc(v.oA, OSCA_BASE, v, mPosA, mPitch, mLvlA, mPan);
    bool bOn = setupOsc(v.oB, OSCB_BASE, v, mPosB, mPitch, mLvlB, mPan);
    if (aOn) renderOsc(v.oA, tmpL_, tmpR_, n);
    if (bOn) renderOsc(v.oB, split ? bL_ : tmpL_, split ? bR_ : tmpR_, n);

    // sub oscillator
    if (p_[SUB_ON] > 0.5) {
        double lvl = p_[SUB_LEVEL] * p_[SUB_LEVEL] * 0.3;
        if (lvl > 1e-6) {
            double sf = 440 * std::pow(2.0, (v.pitch + bend_ + p_[SUB_OCT] * 12 + mPitch * 12 - 69) / 12.0);
            double inc = sf / sr_;
            if (inc > 0 && inc < 0.45) {
                double ph = v.subPhase;
                bool square = (int)p_[SUB_SHAPE] == 1;
                for (int i = 0; i < n; i++) {
                    double s;
                    if (square) {
                        s = ph < 0.5 ? 1 : -1;
                        if (ph < inc) { double t = ph / inc; s += -(t * t) + 2 * t - 1; }
                        else if (ph > 1 - inc) { double t = (ph - 1) / inc; s += t * t + 2 * t + 1; }
                        double h = ph - 0.5;
                        if (h >= 0 && h < inc) { double t = h / inc; s -= -(t * t) + 2 * t - 1; }
                        else if (h < 0 && h > -inc) { double t = h / inc; s -= t * t + 2 * t + 1; }
                        s *= 0.7;
                    } else {
                        s = std::sin(2 * PI * ph);
                    }
                    float o = (float)(s * lvl);
                    tmpL_[i] += o; tmpR_[i] += o;
                    ph += inc; if (ph >= 1) ph -= 1;
                }
                v.subPhase = ph;
            }
        }
    }

    // noise
    if (p_[NOISE_ON] > 0.5) {
        double lvl = p_[NOISE_LEVEL] * p_[NOISE_LEVEL] * 0.35;
        if (lvl > 1e-6) {
            if ((int)p_[NOISE_TYPE] == 1) {
                double* b = v.pb;
                for (int i = 0; i < n; i++) {
                    double w = rng_.next() * 2 - 1;
                    b[0] = 0.99886 * b[0] + w * 0.0555179;
                    b[1] = 0.99332 * b[1] + w * 0.0750759;
                    b[2] = 0.969 * b[2] + w * 0.153852;
                    b[3] = 0.8665 * b[3] + w * 0.3104856;
                    b[4] = 0.55 * b[4] + w * 0.5329522;
                    b[5] = -0.7616 * b[5] - w * 0.016898;
                    double pink = (b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + b[6] + w * 0.5362) * 0.11;
                    b[6] = w * 0.115926;
                    float o = (float)(pink * lvl);
                    tmpL_[i] += o; tmpR_[i] += o;
                }
            } else {
                for (int i = 0; i < n; i++) {
                    float o = (float)((rng_.next() * 2 - 1) * lvl);
                    tmpL_[i] += o; tmpR_[i] += o;
                }
            }
        }
    }

    // ---- per-voice filters with routing ----
    bool f1on = p_[FILTER1_BASE + FLT_ON] > 0.5;
    bool f2on = p_[FILTER2_BASE + FLT_ON] > 0.5;
    if (f1on) setupFilter(v.f1, FILTER1_BASE, v, e2, mCut, 0);
    if (f2on) setupFilter(v.f2, FILTER2_BASE, v, e2, mCut2, mRes2);

    double dr1 = p_[FILTER1_BASE + FLT_DRIVE], dr2 = p_[FILTER2_BASE + FLT_DRIVE];
    float* oL; float* oR;

    if (split) {
        if (f1on) runFilter(v.f1, tmpL_, tmpR_, f1L_, f1R_, dr1, n);
        if (f2on) runFilter(v.f2, bL_, bR_, f2L_, f2R_, dr2, n);
        float* aL = f1on ? f1L_ : tmpL_; float* aR = f1on ? f1R_ : tmpR_;
        float* sL = f2on ? f2L_ : bL_;   float* sR = f2on ? f2R_ : bR_;
        for (int i = 0; i < n; i++) { f1L_[i] = aL[i] + sL[i]; f1R_[i] = aR[i] + sR[i]; }
        oL = f1L_; oR = f1R_;
    } else if (route == 1) {
        if (f1on) runFilter(v.f1, tmpL_, tmpR_, f1L_, f1R_, dr1, n);
        if (f2on) runFilter(v.f2, tmpL_, tmpR_, f2L_, f2R_, dr2, n);
        if (f1on && f2on) {
            for (int i = 0; i < n; i++) { f1L_[i] += f2L_[i]; f1R_[i] += f2R_[i]; }
            oL = f1L_; oR = f1R_;
        } else if (f1on) { oL = f1L_; oR = f1R_; }
        else if (f2on) { oL = f2L_; oR = f2R_; }
        else { oL = tmpL_; oR = tmpR_; }
    } else {
        float* cL = tmpL_; float* cR = tmpR_;
        if (f1on) { runFilter(v.f1, cL, cR, f1L_, f1R_, dr1, n); cL = f1L_; cR = f1R_; }
        if (f2on) { runFilter(v.f2, cL, cR, f2L_, f2R_, dr2, n); cL = f2L_; cR = f2R_; }
        oL = cL; oR = cR;
    }

    double ampFactor = std::min(2.0, std::max(0.0, 1 + mAmp));
    for (int i = 0; i < n; i++) {
        double sl = oL[i], sr = oR[i];
        double yL = sl - v.dcxL + DC_R * v.dcyL;
        double yR = sr - v.dcxR + DC_R * v.dcyR;
        v.dcxL = sl; v.dcyL = yL;
        v.dcxR = sr; v.dcyR = yR;
        double amp = v.ampEnv.process() * v.velGain * ampFactor;
        L[i] += (float)(yL * amp);
        R[i] += (float)(yR * amp);
    }

    v.lfo1.advance(p_[LFO1_BASE + 1], n, sr_);
    v.lfo2.advance(p_[LFO2_BASE + 1], n, sr_);
    v.modEnv.processBlock(n);

    vizA = v.oA.posSm; vizB = v.oB.posSm;
}

void Engine::renderBlock(float* L, float* R, int n) {
    std::fill(L, L + n, 0.0f);
    std::fill(R, R + n, 0.0f);
    int act = 0;
    for (auto& v : voices_) {
        if (!v.active()) continue;
        renderVoice(v, L, R, n);   // updates vizA/vizB to this voice's wt positions
        act++;
    }
    vizActive = act;
    if (act == 0) { vizA = -1; vizB = -1; } // idle -> let the UI fall back to the knob
}

void Engine::render(float* L, float* R, int n) {
    int off = 0;
    while (off < n) {
        int chunk = std::min(128, n - off);
        renderBlock(L + off, R + off, chunk);
        off += chunk;
    }
}

} // namespace fable
