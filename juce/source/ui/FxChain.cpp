#include "FxChain.h"
#include "../dsp/Fx.h"
#include <complex>

namespace fui {
namespace {
using M = fable::FxTelemetry;
float clamp(float v, float lo, float hi) { return juce::jlimit(lo, hi, v); }
float level(float v) { return clamp((v + 72) / 72, 0, 1); }
juce::String db(float v) { return v <= -89 ? juce::String::fromUTF8("−∞") : juce::String(v, 1); }
juce::String signedDb(float v) {
    return (v > 0.05f ? "+" : "") + juce::String(std::abs(v) < 0.05f ? 0 : v, 1);
}
void text(juce::Graphics &g, const juce::String &s, float x, float y, juce::Colour c = col::acN,
          juce::Justification align = juce::Justification::left, float size = 8) {
    g.setFont(monoFont(size));
    g.setColour(c);
    const float offset = align == juce::Justification::right     ? 220.0f
                         : align == juce::Justification::centred ? 110.0f
                                                                 : 0.0f;
    g.drawText(s, juce::Rectangle<float>(x - offset, y - 10, 220, 14), align, false);
}
void line(juce::Graphics &g, float x, float y, float xx, float yy, float alpha = 0.15f,
          juce::Colour c = col::acN) {
    g.setColour(c.withAlpha(alpha));
    g.drawLine(x, y, xx, yy, 0.8f);
}
void stroke(juce::Graphics &g, const juce::Path &p, juce::Colour c, float width = 1) {
    g.setColour(c);
    g.strokePath(p, juce::PathStrokeType(width));
}
const char *effects[] = {"eq", "ott", "comp", "drive", "chorus", "delay", "reverb"};
const char *titles[] = {"EQ", "OTT", "COMP", "DRIVE", "CHORUS", "DELAY", "REVERB"};
const char *eqKeys[4][5] = {{"lfreq", "low", "lq", "ltype", "lon"},
                            {"mfreq", "mid", "mq", "mtype", "mon"},
                            {"m2freq", "mid2", "m2q", "m2type", "m2on"},
                            {"hfreq", "high", "hq", "htype", "hon"}};
} // namespace

FxModuleView::FxModuleView(DeviceUiModel &model, Kind kind, bool tape, juce::String prefix, int pad)
    : model_(model), source_(model.parameters()), kind_(kind), tape_(tape), prefix_(std::move(prefix)),
      effect_(effects[kind]), pad_(pad), power_(source_, prefix_ + "fx." + effect_ + ".on", Accent::N) {
    setName(juce::String(titles[kind]) + " visual FX");
    addAndMakeVisible(power_);
    juce::StringArray keys;
    switch (kind_) {
    case Eq:
        break;
    case Ott:
        keys = {"depth", "time", "up", "down"};
        break;
    case Comp:
        keys = {"thr", "att", "rel", "ratio"};
        break;
    case Drive:
        keys = {"amt", "tone", "mix"};
        break;
    case Chorus:
        keys = {"rate", "depth", "mix"};
        break;
    case Echo:
        keys = tape ? juce::StringArray{"time", "fb", "mix", "tone", "sat", "wow", "flutter", "width"}
                    : juce::StringArray{"time", "fb", "mix"};
        break;
    case Reverb:
        keys = {"size", "mix"};
        break;
    }
    for (const auto &key : keys)
        addAndMakeVisible(
            knobs_.add(new Knob(source_, prefix_ + "fx." + effect_ + "." + key, Knob::Sm, Accent::N)));
    if (kind == Eq) {
        setWantsKeyboardFocus(true);
        for (int i = 0; i < 4; ++i) {
            auto &b = bands_[(size_t)i];
            b.setButtonText(juce::String(i + 1));
            b.setTitle("Select EQ band " + juce::String(i + 1));
            b.onClick = [this, i] { selectBand(i); };
            addAndMakeVisible(b);
        }
        shape_.addItemList({"LOW SHELF", "BELL", "HIGH SHELF"}, 1);
        shape_.onChange = [this] { write(bandKey("type"), (float)(shape_.getSelectedId() - 1)); };
        bandOn_.onClick = [this] { write(bandKey("on"), value(bandKey("on")) > 0.5f ? 0 : 1); };
        reset_.onClick = [this] { resetBand(); };
        addAndMakeVisible(shape_);
        addAndMakeVisible(bandOn_);
        addAndMakeVisible(reset_);
        selectBand(1);
    }
    if (kind == Drive) {
        const char* names[] = {"SOFT", "TAPE", "HARD"};
        for (int i = 0; i < 3; ++i) {
            auto& button = driveTypes_[(size_t)i];
            button.setButtonText(names[i]);
            button.setName(names[i]);
            button.setToggleState((int)value(prefix_ + "fx.drive.type") == i, juce::dontSendNotification);
            button.setTitle(juce::String(names[i]) + " saturation");
            button.setClickingTogglesState(true);
            button.setColour(juce::TextButton::buttonOnColourId, col::acN.withAlpha(.18f));
            button.onClick = [this, i] {
                write(prefix_ + "fx.drive.type", (float)i);
                for (int j = 0; j < 3; ++j)
                    driveTypes_[(size_t)j].setToggleState(i == j, juce::dontSendNotification);
            };
            addAndMakeVisible(button);
        }
    }
    if (kind == Echo && tape) {
        mode_.addItemList({"PING PONG", "STEREO"}, 1);
        mode_.onChange = [this] { write("fx.delay.mode", (float)(mode_.getSelectedId() - 1)); };
        division_.addItemList({"1/4", "1/4 D", "1/8", "1/8 D", "1/8 T", "1/16"}, 1);
        division_.onChange = [this] { write("fx.delay.div", (float)(division_.getSelectedId() - 1)); };
        sync_.onClick = [this] { write("fx.delay.sync", value("fx.delay.sync") > 0.5f ? 0 : 1); };
        addAndMakeVisible(mode_);
        addAndMakeVisible(division_);
        addAndMakeVisible(sync_);
    }
    startTimerHz(30);
}
FxModuleView::~FxModuleView() {
    stopTimer();
    finishDrag();
}
float FxModuleView::value(const juce::String &id) const {
    if (auto *p = source_.parameter(id))
        return p->convertFrom0to1(p->getValue());
    return 0;
}
void FxModuleView::write(const juce::String &id, float v, bool gesture) {
    if (auto *p = source_.parameter(id)) {
        if (gesture)
            p->beginChangeGesture();
        p->setValueNotifyingHost(clamp(p->convertTo0to1(v), 0, 1));
        if (gesture)
            p->endChangeGesture();
    }
    repaint();
}
juce::String FxModuleView::bandKey(const char *field) const {
    const juce::StringArray fields{"freq", "gain", "q", "type", "on"};
    return prefix_ + "fx.eq." + juce::String(eqKeys[selected_][fields.indexOf(field)]);
}
void FxModuleView::selectBand(int band) {
    finishDrag();
    selected_ = band;
    knobs_.clear();
    for (const char *f : {"freq", "gain", "q"}) {
        auto* knob = knobs_.add(new Knob(source_, bandKey(f), Knob::Sm, Accent::N));
        knob->setLabelText(juce::String(f).toUpperCase());
        addAndMakeVisible(knob);
    }
    for (int i = 0; i < 4; ++i)
        bands_[(size_t)i].setToggleState(i == selected_, juce::dontSendNotification);
    bandOn_.setButtonText("BAND " + juce::String(band + 1));
    resized();
    repaint();
}
void FxModuleView::resetBand() {
    for (const char *f : {"freq", "gain", "q", "type", "on"}) {
        auto id = bandKey(f);
        if (auto *p = source_.parameter(id))
            write(id, p->convertFrom0to1(p->getDefaultValue()));
    }
}
float FxModuleView::echoTime() const {
    if (data_[M::time] > 0 && lastReceived_ > 0)
        return data_[M::time];
    if (tape_ && value("fx.delay.sync") > 0.5f) {
        static const float divisions[] = {1, 1.5f, .5f, .75f, 1.f / 3, .25f};
        return clamp(60.0f / juce::jmax(1.0f, value("seq.bpm")) *
                         divisions[juce::jlimit(0, 5, (int)value("fx.delay.div"))],
                     .02f, 1.5f);
    }
    return value(prefix_ + "fx.delay.time");
}
void FxModuleView::timerCallback() {
    // Checking ancestor visibility also permits offscreen software snapshots;
    // isShowing() requires an OS peer even when every component is visible.
    for (auto *c = static_cast<juce::Component *>(this); c != nullptr; c = c->getParentComponent())
        if (!c->isVisible()) {
            history_.clear();
            lastReceived_ = 0;
            return;
        }
    int bus = prefix_.isEmpty() ? 0 : juce::jlimit(0, 4, (int)value(prefix_ + "out"));
    if (bus != bus_) {
        bus_ = bus;
        history_.clear();
        serial_ = 0;
        lastReceived_ = 0;
    }
    auto next = model_.fxTelemetry(pad_, bus_);
    const auto serial = kind_ == Reverb ? next.reverbSerial : next.serial;
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (serial != serial_) {
        data_ = next;
        serial_ = serial;
        lastReceived_ = now;
        if (kind_ == Reverb)
            data_.seconds = next.reverbSeconds;
        if (!history_.empty() && data_.seconds < history_.back().seconds)
            history_.clear();
        history_.push_back(data_);
        while (history_.size() > 90)
            history_.pop_front();
    } else if (lastReceived_ > 0 && now - lastReceived_ > 350) {
        data_ = {};
        data_.sampleRate = next.sampleRate;
        history_.clear();
        lastReceived_ = 0;
    }
    if (kind_ == Eq) {
        shape_.setSelectedId((int)value(bandKey("type")) + 1, juce::dontSendNotification);
        bandOn_.setToggleState(value(bandKey("on")) > 0.5f, juce::dontSendNotification);
    }
    if (kind_ == Drive)
        for (int i = 0; i < 3; ++i)
            driveTypes_[(size_t)i].setToggleState((int)value(prefix_ + "fx.drive.type") == i, juce::dontSendNotification);
    if (kind_ == Echo && tape_) {
        const bool sync = value("fx.delay.sync") > 0.5f;
        mode_.setSelectedId((int)value("fx.delay.mode") + 1, juce::dontSendNotification);
        division_.setSelectedId((int)value("fx.delay.div") + 1, juce::dontSendNotification);
        sync_.setToggleState(sync, juce::dontSendNotification);
        division_.setVisible(sync);
        knobs_[0]->setEnabled(!sync);
        knobs_[0]->setAlpha(sync ? .3f : power_.isOn() ? 1.f : .55f);
    }
    for (int i = 0; i < knobs_.size(); ++i)
        if (!(kind_ == Echo && tape_ && i == 0))
            knobs_[i]->setAlpha(power_.isOn() ? 1.f : .55f);
    repaint();
}

void FxModuleView::resized() {
    auto r = getLocalBounds().reduced(12, 10);
    auto head = r.removeFromTop(24);
    power_.setBounds(head.removeFromLeft(18).withSizeKeepingCentre(14, 14));
    caption_ = head.withTrimmedLeft(kind_ == Echo && tape_ ? 125 : 80);
    if (kind_ == Eq) {
        auto tabs = head.removeFromRight(108);
        for (auto &b : bands_) {
            b.setBounds(tabs.removeFromLeft(24));
            tabs.removeFromLeft(4);
        }
    }
    if (kind_ == Echo && tape_) {
        division_.setBounds(head.removeFromRight(72));
        head.removeFromRight(5);
        sync_.setBounds(head.removeFromRight(46));
        head.removeFromRight(5);
        mode_.setBounds(head.removeFromRight(98));
    }
    r.removeFromTop(6);
    if (kind_ == Chorus) {
        plot_ = {};
        controls_ = r;
    } else {
        const int bottom = kind_ == Eq ? 112 : kind_ == Ott || kind_ == Comp ? 106 : kind_ == Drive ? 98 : 92;
        plot_ = r.removeFromTop(juce::jmax(72, r.getHeight() - bottom));
        readouts_ = r.removeFromTop(kind_ == Eq ? 28 : 24);
        if (kind_ == Drive) {
            auto types = readouts_.reduced(0, 2);
            const int width = types.getWidth() / 3;
            for (auto& button : driveTypes_) button.setBounds(types.removeFromLeft(width));
        }
        if (kind_ == Eq) {
            auto tools = readouts_.reduced(0, 3);
            bandOn_.setBounds(tools.removeFromLeft(75));
            reset_.setBounds(tools.removeFromRight(52));
            tools.removeFromLeft(4);
            shape_.setBounds(tools.removeFromLeft(115));
        }
        controls_ = r.removeFromTop(64);
        footer_ = r;
    }
    auto controls = controls_;
    if (kind_ == Reverb)
        controls = controls.withTrimmedRight(controls.getWidth() / 3);
    const int n = knobs_.size();
    for (int i = 0; i < n; ++i)
        knobs_[i]->setBounds(controls.getX() + i * controls.getWidth() / n, controls.getY(),
                             controls.getWidth() / n, juce::jmin(64, controls.getHeight()));
}

void FxModuleView::paint(juce::Graphics &g) {
    drawPanel(g, getLocalBounds().toFloat(), 9);
    g.setFont(dispFont(12));
    g.setColour(col::text);
    g.drawText(kind_ == Echo && tape_ ? "TAPE ECHO" : titles[kind_], 34, 10, 140, 24,
               juce::Justification::centredLeft);
    juce::String caption;
    if (kind_ == Ott)
        caption = power_.isOn() ? juce::String::fromUTF8("3 BAND         LEVEL / ± GAIN") : "BYPASS";
    if (kind_ == Comp)
        caption = power_.isOn() ? juce::String(value(prefix_ + "fx.comp.ratio"), 1) + juce::String::fromUTF8(":1 · SOFT KNEE") : "BYPASS";
    if (kind_ == Drive)
        caption = power_.isOn() ? "4x SATURATION" : "BYPASS";
    if (kind_ == Reverb)
        caption = prefix_.isNotEmpty() ? (power_.isOn() ? "PAD SEND" : "SEND OFF")
                  : power_.isOn()      ? "STEREO"
                                       : "BYPASS";
    g.setFont(monoFont(8));
    g.setColour(col::acN);
    g.drawText(caption, caption_, juce::Justification::centredRight);
    if (!plot_.isEmpty()) {
        drawDisplayBox(g, plot_.toFloat());
        juce::Graphics::ScopedSaveState save(g);
        g.reduceClipRegion(plot_.reduced(1));
        g.addTransform(juce::AffineTransform::translation((float)plot_.getX(), (float)plot_.getY()));
        if (kind_ == Eq)
            drawEq(g, (float)plot_.getWidth(), (float)plot_.getHeight());
        if (kind_ == Ott || kind_ == Comp)
            drawDynamics(g, (float)plot_.getWidth(), (float)plot_.getHeight());
        if (kind_ == Drive)
            drawDrive(g, (float)plot_.getWidth(), (float)plot_.getHeight());
        if (kind_ == Echo)
            drawEcho(g, (float)plot_.getWidth(), (float)plot_.getHeight());
        if (kind_ == Reverb)
            drawReverb(g, (float)plot_.getWidth(), (float)plot_.getHeight());
    }
    juce::StringArray readings;
    if (kind_ == Ott || kind_ == Comp) {
        const bool ott = kind_ == Ott;
        readings.add("IN  " + db(data_[ott ? M::ottIn : M::compIn]) + " dB");
        readings.add("OUT  " + db(data_[ott ? M::ottOut : M::compOut]) + " dB");
        readings.add("AUTO  " + signedDb(power_.isOn() ? data_[ott ? M::ottAuto : M::compAuto] : 0) + " dB");
        if (!ott)
            readings.add("GR  " + juce::String(power_.isOn() ? data_[M::reduction] : 0, 1) + " dB");
    }
    if (kind_ == Echo) {
        readings = {"TIME  " + juce::String((int)std::round(echoTime() * 1000)) + " ms",
                    "L  " + db(data_[M::echoL]) + " dB", "R  " + db(data_[M::echoR]) + " dB"};
        if (tape_)
            readings.add(
                "DRIFT  " +
                juce::String(1000 * juce::jmax(std::abs(data_[M::driftL]), std::abs(data_[M::driftR])), 2) +
                " ms");
    }
    if (kind_ == Reverb)
        readings = {"L RETURN  " + db(data_[M::verbL]) + " dB", "R RETURN  " + db(data_[M::verbR]) + " dB"};
    g.setFont(monoFont(8));
    g.setColour(col::text);
    for (int i = 0; i < readings.size(); ++i)
        g.drawText(readings[i],
                   readouts_.withX(readouts_.getX() + i * readouts_.getWidth() / readings.size())
                       .withWidth(readouts_.getWidth() / readings.size()),
                   juce::Justification::centred);
    if (kind_ == Comp) {
        g.setColour(col::acN);
        g.drawText(juce::String::fromUTF8("AUTO GAIN · GR BEFORE AUTO · ~3 s HISTORY"), footer_,
                   juce::Justification::centred);
    }
    if (kind_ == Ott) {
        g.setColour(col::acN);
        g.drawText(juce::String::fromUTF8("120 Hz / 2.5 kHz · WET GAIN BEFORE AUTO · ±24 dB VIEW"), footer_,
                   juce::Justification::centred);
    }
    if (kind_ == Eq) {
        juce::StringArray values{juce::String((int)value(bandKey("freq"))) + " Hz",
                                 signedDb(value(bandKey("gain"))) + " dB",
                                 "Q " + juce::String(value(bandKey("q")), 2)};
        for (int i = 0; i < 3; ++i)
            g.drawText(
                values[i],
                footer_.withX(footer_.getX() + i * footer_.getWidth() / 3).withWidth(footer_.getWidth() / 3),
                juce::Justification::centred);
    }
    if (kind_ == Reverb) {
        auto c = controls_.withTrimmedLeft(controls_.getWidth() * 2 / 3).reduced(8, 0);
        g.setColour(col::acN);
        g.drawText("CORRELATION", c.removeFromTop(15), juce::Justification::centred);
        const bool silent = data_[M::verbL] <= -89 && data_[M::verbR] <= -89;
        g.setColour(col::text);
        g.drawText(silent ? juce::String::fromUTF8("—") : juce::String(data_[M::correlation], 2),
                   c.removeFromTop(18), juce::Justification::centred);
        const float y = (float)c.getY() + 4;
        line(g, (float)c.getX(), y, (float)c.getRight(), y, .3f);
        line(g, (float)c.getCentreX(), y - 3, (float)c.getCentreX(), y + 3, .4f);
        if (!silent) {
            const float x = (float)c.getX() + (data_[M::correlation] + 1) * .5f * (float)c.getWidth();
            line(g, x, y - 3, x, y + 3, 1, col::text);
        }
        g.setColour(col::acN);
        g.drawText(juce::String::fromUTF8("−1          0          +1"), c.withTrimmedTop(10),
                   juce::Justification::centred);
        if (prefix_.isNotEmpty())
            g.drawText(juce::String::fromUTF8("SHARED BUS RETURN · CONTROLS APPLY TO THIS PAD"), footer_,
                       juce::Justification::centred);
    }
}

void FxModuleView::drawDynamics(juce::Graphics &g, float w, float h) {
    const bool on = power_.isOn() && (kind_ != Ott || value(prefix_ + "fx.ott.depth") > 0);
    if (kind_ == Ott) {
        const float top = 28, bottom = h - 24, middle = (top + bottom) / 2, col = (w - 20) / 3;
        const char *names[] = {"LOW", "MID", "HIGH"};
        for (int i = 0; i < 3; ++i) {
            const float x = 10 + (float)i * col, center = x + col / 2, bx = x + col * .18f,
                        bw = juce::jmax(6.f, col * .12f);
            text(g, names[i], center, 14, col::text, juce::Justification::centred);
            if (i)
                line(g, x, 8, x, h - 8, .12f);
            g.setColour(col::acN.withAlpha(.08f));
            g.fillRect(bx, top, bw, bottom - top);
            float v = on ? clamp((data_.values[(size_t)M::ottLow + (size_t)i] + 60) / 60, 0, 1) : 0;
            g.setGradientFill(juce::ColourGradient(col::text.withAlpha(.8f), 0, top, col::acN.withAlpha(.8f),
                                                   0, bottom, false));
            g.fillRect(bx, bottom - v * (bottom - top), bw, v * (bottom - top));
            const float gx = x + col * .44f, gw = col * .35f,
                        gain = on ? data_.values[(size_t)M::gainLow + (size_t)i] : 0;
            line(g, gx, top, gx + gw, top, .1f);
            line(g, gx, bottom, gx + gw, bottom, .1f);
            line(g, gx - 3, middle, gx + gw + 3, middle, .4f);
            float delta = clamp(gain / 24, -1, 1) * (bottom - top) / 2;
            g.setColour(col::acN.withAlpha(.25f));
            g.fillRect(gx, juce::jmin(middle, middle - delta), gw, std::abs(delta));
            if (std::abs(gain) > .05f)
                line(g, gx, middle - delta, gx + gw, middle - delta, 1, col::text);
            text(g, signedDb(gain) + " dB", center, h - 8, on ? col::text : col::acN,
                 juce::Justification::centred, 9);
        }
    } else {
        const float left = 29, right = w - 12, top = 16, bottom = h - 43;
        const auto Y = [&](float v) { return top + (1 - clamp((v + 60) / 60, 0, 1)) * (bottom - top); };
        for (float v : {0.f, -24.f, -48.f}) {
            text(g, juce::String((int)v), left - 6, Y(v) + 3, col::acN, juce::Justification::right, 9);
            line(g, left, Y(v), right, Y(v), .1f);
        }
        const float thr = Y(value(prefix_ + "fx.comp.thr"));
        for (float x = left; x < right; x += 7)
            line(g, x, thr, juce::jmin(x + 3, right), thr, on ? .6f : .2f);
        text(g, "THR", right, thr - 4, col::acN, juce::Justification::right);
        auto X = [&](size_t i) { return right - (float)(history_.size() - 1 - i) / 89 * (right - left); };
        for (auto key : {M::compIn, M::compOut}) {
            juce::Path p;
            for (size_t i = 0; i < history_.size(); ++i) {
                if (i)
                    p.lineTo(X(i), Y(history_[i][key]));
                else
                    p.startNewSubPath(X(i), Y(history_[i][key]));
            }
            stroke(g, p, key == M::compIn ? col::acN.withAlpha(.5f) : col::text,
                   key == M::compIn ? 1.f : 1.5f);
        }
        const float grTop = h - 29;
        text(g, "GR", left - 6, grTop + 9, col::acN, juce::Justification::right);
        line(g, left, grTop, right, grTop, .22f);
        juce::Path p;
        p.startNewSubPath(left, grTop);
        for (size_t i = 0; i < history_.size(); ++i)
            p.lineTo(X(i), grTop + clamp(on ? history_[i][M::reduction] / 24 : 0, 0, 1) * 20);
        p.lineTo(right, grTop);
        p.closeSubPath();
        g.setColour(col::acN.withAlpha(.4f));
        g.fillPath(p);
    }
}

void FxModuleView::drawEcho(juce::Graphics &g, float w, float h) {
    const float left = 30, right = w - 14, width = right - left, time = juce::jmax(.001f, echoTime());
    text(g, "LIVE RETURN", left, 13);
    text(g, "~3 s HISTORY", right, 13, col::acN, juce::Justification::right);
    for (int repeat = 1; (float)repeat * time < 3 && repeat < 16; ++repeat) {
        float x = right - (float)repeat * time / 3 * width;
        line(g, x, 23, x, h - 29, .08f + std::pow(value(prefix_ + "fx.delay.fb"), (float)repeat) * .14f);
        if (repeat < 5 && time > .15f)
            text(g, juce::String::fromUTF8("×") + juce::String(repeat), x, h - 30, col::acN,
                 juce::Justification::centred);
    }
    const auto X = [&](size_t i) { return right - (float)(history_.size() - 1 - i) / 89 * width; };
    for (int channel = 0; channel < 2; ++channel) {
        auto key = channel ? M::echoR : M::echoL;
        float y = channel ? 84.f : 43.f;
        auto ink = channel ? col::acN : col::text;
        text(g, channel ? "R" : "L", 12, y + 3);
        line(g, left, y, right, y, .22f);
        if (history_.size() > 1) {
            juce::Path edge;
            for (size_t i = 0; i < history_.size(); ++i) {
                float yy = y - level(history_[i][key]) * 18;
                if (i)
                    edge.lineTo(X(i), yy);
                else
                    edge.startNewSubPath(X(i), yy);
            }
            juce::Path ribbon = edge;
            for (size_t i = history_.size(); i-- > 0;)
                ribbon.lineTo(X(i), y + level(history_[i][key]) * 18);
            ribbon.closeSubPath();
            g.setGradientFill(
                juce::ColourGradient(ink.withAlpha(0.f), left, 0, ink.withAlpha(.18f), right, 0, false));
            g.fillPath(ribbon);
            g.setGradientFill(juce::ColourGradient(ink.withAlpha(0.f), left, 0, ink, right, 0, false));
            g.strokePath(edge, juce::PathStrokeType(1.3f));
        }
        const float a = level(data_[key]) * 18;
        if (a > 0)
            line(g, right, y - a, right, y + a, 1, ink);
    }
    const float y = h - 13;
    if (tape_) {
        text(g, juce::String::fromUTF8("Δ"), 12, y + 3);
        line(g, left, y, right, y, .15f);
        for (auto key : {M::driftL, M::driftR}) {
            juce::Path p;
            for (size_t i = 0; i < history_.size(); ++i) {
                float yy = y - (power_.isOn() ? clamp(history_[i][key] / .003f, -1, 1) * 7 : 0);
                if (i)
                    p.lineTo(X(i), yy);
                else
                    p.startNewSubPath(X(i), yy);
            }
            stroke(g, p, (key == M::driftL ? col::text : col::acN).withAlpha(power_.isOn() ? .7f : .2f));
        }
    } else
        text(g, "PING-PONG RETURN", left, y + 3);
}

void FxModuleView::drawReverb(juce::Graphics &g, float w, float h) {
    float half = (w - 32) / 2, center = w / 2;
    text(g,
         prefix_.isEmpty() ? "LIVE TAIL"
                           : (bus_ == 0 ? juce::String("MAIN") : "AUX " + juce::String(bus_)) + " RETURN",
         12, 14);
    text(g, "3 s HISTORY", w - 12, 14, col::acN, juce::Justification::right);
    const auto project = [&](float d) { return juce::Point<float>(1 - d * .72f, h - 23 - d * 69); };
    for (float x : {-1.f, -.5f, 0.f, .5f, 1.f}) {
        auto back = project(1), front = project(0);
        line(g, center + x * half * back.x, back.y, center + x * half, front.y, .17f);
    }
    for (int i = 24; i >= 0; --i) {
        float depth = (float)i / 24.f;
        auto p = project(depth);
        M sample;
        if (!i)
            sample = data_;
        else
            for (auto it = history_.rbegin(); it != history_.rend(); ++it)
                if (it->seconds <= data_.seconds - depth * 3) {
                    sample = *it;
                    break;
                }
        float l = level(sample[M::verbL]), r = level(sample[M::verbR]);
        juce::Path path;
        for (int step = 0; step <= 48; ++step) {
            float x = (float)step / 24.f - 1, amp = x < 0 ? l : r,
                  shape = std::pow(juce::jmax(0.f, std::sin(juce::MathConstants<float>::pi * std::abs(x))), 1.35f);
            float xx = center + x * half * p.x, yy = p.y - shape * amp * 51 * p.x;
            if (step)
                path.lineTo(xx, yy);
            else
                path.startNewSubPath(xx, yy);
        }
        stroke(g, path, (i ? col::acN : col::text).withAlpha(i ? .15f + (1 - depth) * .42f : .9f),
               i ? .8f : 1.4f);
        path.lineTo(center + half * p.x, p.y);
        path.lineTo(center - half * p.x, p.y);
        path.closeSubPath();
        g.setColour(col::acN.withAlpha((l + r) * .025f * (1 - depth)));
        g.fillPath(path);
    }
    text(g, "L", 16, h - 8);
    text(g, "NOW", center, h - 8, col::acN, juce::Justification::centred);
    text(g, "R", w - 16, h - 8, col::acN, juce::Justification::right);
}

void FxModuleView::drawDrive(juce::Graphics& g, float w, float h) {
    const float left = 22, right = w - 14, middle = h * .38f, scale = h * .22f;
    const bool on = power_.isOn();
    const double amount = value(prefix_ + "fx.drive.amt"), pre = 1 + amount * 2, k = 1 + amount * 12;
    const double mix = value(prefix_ + "fx.drive.mix"), angle = mix * juce::MathConstants<double>::halfPi;
    const double tone = on ? value(prefix_ + "fx.drive.tone") : 0;
    fable::DriveColor shape;
    shape.setParams(value(prefix_ + "fx.drive.type"), 0); shape.reset();
    text(g, "TRANSFER / MIX", left, 14);
    line(g, left, middle, right, middle);
    line(g, (left + right) / 2, middle - scale, (left + right) / 2, middle + scale);
    line(g, left, middle + scale, right, middle - scale, .25f);
    juce::Path transfer, response;
    const double sr = juce::jmax(8000.0, data_.sampleRate), pole = std::exp(-2 * juce::MathConstants<double>::pi * 1000 / sr);
    const float toneY = h * .83f;
    text(g, "WET TONE", left, h * .7f);
    line(g, left, toneY, right, toneY);
    for (int i = 0; i <= 160; ++i) {
        const float x = left + (right - left) * (float)i / 160;
        const double input = i / 80.0 - 1;
        const double output = on ? std::cos(angle) * input + std::sin(angle) * shape.shape(input * pre, k, 1 / (pre * std::tanh(k))) : input;
        const float y = middle - (float)output * scale;
        const double freq = 100 * std::pow(100.0, i / 160.0);
        const auto z = std::polar(1.0, -2 * juce::MathConstants<double>::pi * freq / sr);
        const double gain = tone * (tone < 0 ? .5 : 1);
        const double dbGain = 20 * std::log10(std::abs(1.0 + gain * (1.0 - (1 - pole) / (1.0 - pole * z))));
        const float ty = toneY - (float)dbGain * h * .012f;
        if (i) { transfer.lineTo(x, y); response.lineTo(x, ty); }
        else { transfer.startNewSubPath(x, y); response.startNewSubPath(x, ty); }
    }
    stroke(g, transfer, col::text, 1.5f); stroke(g, response, col::acN, 1.2f);
    text(g, "100 Hz", left, h - 5);
    text(g, "1k", (left + right) / 2, h - 5, col::acN, juce::Justification::centred);
    text(g, "10k", right, h - 5, col::acN, juce::Justification::right);
}

void FxModuleView::drawEq(juce::Graphics &g, float width, float height) {
    const float right = width - 12, bottom = height - 20;
    const auto X = [&](float freq) { return 26 + std::log(freq / 20) / std::log(1000.f) * (right - 26); };
    const auto Y = [&](float gain) { return 12 + (15 - gain) / 30 * (bottom - 12); };
    for (float v : {12.f, 0.f, -12.f}) {
        line(g, 26, Y(v), right, Y(v), std::abs(v) < 0.01f ? .27f : .08f);
        text(g, (v > 0 ? "+" : "") + juce::String((int)v), 20, Y(v) + 3, col::acN,
             juce::Justification::right);
    }
    for (float f : {20.f, 100.f, 1000.f, 10000.f, 20000.f}) {
        line(g, X(f), 12, X(f), bottom, .08f);
        text(g, f >= 1000 ? juce::String((int)(f / 1000)) + "k" : juce::String((int)f), X(f), height - 6, col::acN,
             f < 21      ? juce::Justification::left
             : f > 19999 ? juce::Justification::right
                          : juce::Justification::centred);
    }
    std::array<fable::Biquad, 4> coefs;
    const double sr = juce::jmax(8000.0, data_.sampleRate);
    for (int i = 0; i < 4; ++i) {
        auto key = [&](int j) { return prefix_ + "fx.eq." + juce::String(eqKeys[i][j]); };
        float f = value(key(0)), gain = value(key(4)) > .5f ? value(key(1)) : 0, q = value(key(2));
        int type = (int)value(key(3));
        if (type == 0)
            coefs[(size_t)i].lowShelf(f, gain, sr, q);
        else if (type == 1)
            coefs[(size_t)i].peaking(f, q, gain, sr);
        else
            coefs[(size_t)i].highShelf(f, gain, sr, q);
    }
    std::array<juce::Path, 5> traces;
    for (int i = 0; i <= 240; ++i) {
        float x = 26 + (float)i / 240.f * (right - 26);
        double freq = 20 * std::pow(1000., i / 240.);
        double w = 2 * juce::MathConstants<double>::pi * freq / sr;
        auto z = std::polar(1.0, -w);
        double sum = 0;
        for (int b = 0; b < 5; ++b) {
            double response = sum;
            if (b < 4) {
                auto &c = coefs[(size_t)b];
                response = 10 * std::log10(std::max(1e-20, std::norm(c.b0 + c.b1 * z + c.b2 * z * z) /
                                                               std::norm(1.0 + c.a1 * z + c.a2 * z * z)));
                sum += response;
            } else if (!power_.isOn())
                response = 0;
            float y = Y(clamp((float)response, -30, 30));
            if (i)
                traces[(size_t)b].lineTo(x, y);
            else
                traces[(size_t)b].startNewSubPath(x, y);
        }
    }
    {
        juce::Graphics::ScopedSaveState save(g);
        g.reduceClipRegion(juce::Rectangle<int>(25, 11, (int)width - 36, (int)height - 30));
        auto fill = traces[(size_t)selected_];
        fill.lineTo(right, Y(0));
        fill.lineTo(26, Y(0));
        fill.closeSubPath();
        g.setGradientFill(
            juce::ColourGradient(col::acN.withAlpha(.23f), 0, 0, col::acN.withAlpha(.02f), 0, 126, false));
        g.fillPath(fill);
        for (int i = 0; i < 4; ++i) {
            if (i == selected_) {
                juce::Path dashed;
                float dash[] = {3, 3};
                juce::PathStrokeType(.8f).createDashedStroke(dashed, traces[(size_t)i], dash, 2);
                g.setColour(col::acN.withAlpha(.5f));
                g.fillPath(dashed);
            } else
                stroke(g, traces[(size_t)i], col::acN.withAlpha(.16f), .8f);
        }
        stroke(g, traces[4], power_.isOn() ? col::text : col::acN.withAlpha(.5f), 1.6f);
    }
    for (int i = 0; i < 4; ++i) {
        float x = X(value(prefix_ + "fx.eq." + juce::String(eqKeys[i][0]))),
              y = Y(value(prefix_ + "fx.eq." + juce::String(eqKeys[i][1])));
        bool selected = i == selected_, on = value(prefix_ + "fx.eq." + juce::String(eqKeys[i][4])) > .5f;
        g.setColour(selected && on ? col::acN : juce::Colour(0xff11141c));
        g.fillEllipse(x - 7, y - 7, 14, 14);
        g.setColour(selected ? col::text : col::acN);
        g.drawEllipse(x - 7, y - 7, 14, 14, 1.2f);
        text(g, juce::String(i + 1), x, y + 3, selected && on ? col::display : col::text,
             juce::Justification::centred);
        if (selected && hasKeyboardFocus(false)) {
            g.setColour(col::acN.withAlpha(.5f));
            g.drawEllipse(x - 12, y - 12, 24, 24, 1);
        }
    }
}

int FxModuleView::hitBand(juce::Point<float> p) const {
    if (kind_ != Eq || !plot_.toFloat().contains(p))
        return -1;
    for (int i = 3; i >= 0; --i) {
        float f = value(prefix_ + "fx.eq." + juce::String(eqKeys[i][0])),
              gain = value(prefix_ + "fx.eq." + juce::String(eqKeys[i][1]));
        juce::Point<float> node((float)plot_.getX() + 26 + std::log(f / 20) / std::log(1000.f) * (float)(plot_.getWidth() - 38),
                                (float)plot_.getY() + 12 + (15 - gain) / 30 * (float)(plot_.getHeight() - 32));
        if (node.getDistanceFrom(p) < 16)
            return i;
    }
    return -1;
}
void FxModuleView::mouseDown(const juce::MouseEvent &e) {
    if (!e.mods.isLeftButtonDown())
        return;
    int band = hitBand(e.position);
    if (band < 0)
        return;
    selectBand(band);
    grabKeyboardFocus();
    dragging_ = true;
    dragPoint_ = e.position;
    for (const char *field : {"freq", "gain"})
        if (auto *p = source_.parameter(bandKey(field)))
            p->beginChangeGesture();
}
void FxModuleView::mouseDrag(const juce::MouseEvent &e) {
    if (!dragging_)
        return;
    auto delta = e.position - dragPoint_;
    float fine = e.mods.isShiftDown() ? .15f : 1.f;
    write(bandKey("freq"),
          clamp(value(bandKey("freq")) * std::pow(1000.f, delta.x / (float)(plot_.getWidth() - 38) * fine), 20,
                20000),
          false);
    write(bandKey("gain"),
          clamp(value(bandKey("gain")) - delta.y / (float)(plot_.getHeight() - 32) * 30 * fine, -15, 15), false);
    dragPoint_ = e.position;
}
void FxModuleView::finishDrag() {
    if (!dragging_)
        return;
    for (const char *field : {"freq", "gain"})
        if (auto *p = source_.parameter(bandKey(field)))
            p->endChangeGesture();
    dragging_ = false;
}
void FxModuleView::mouseUp(const juce::MouseEvent &) { finishDrag(); }
void FxModuleView::mouseDoubleClick(const juce::MouseEvent &e) {
    int b = hitBand(e.position);
    if (b >= 0) {
        selectBand(b);
        resetBand();
    }
}
void FxModuleView::mouseWheelMove(const juce::MouseEvent &e, const juce::MouseWheelDetails &wheel) {
    int b = hitBand(e.position);
    if (b < 0 || std::abs(wheel.deltaY) < 1e-6f)
        return;
    if (b != selected_)
        selectBand(b);
    write(bandKey("q"), clamp(value(bandKey("q")) * std::exp((wheel.deltaY > 0 ? 1.f : -1.f) *
                                                             (e.mods.isShiftDown() ? .015f : .12f)),
                              .2f, 12));
}
bool FxModuleView::keyPressed(const juce::KeyPress &k) {
    if (kind_ != Eq)
        return false;
    float fine = k.getModifiers().isShiftDown() ? .1f : 1.f;
    auto code = k.getKeyCode();
    if (code == juce::KeyPress::homeKey) {
        resetBand();
        return true;
    }
    if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey) {
        write(bandKey("freq"),
              clamp(value(bandKey("freq")) *
                        std::pow(2.f, (code == juce::KeyPress::leftKey ? -1.f : 1.f) * fine / 12),
                    20, 20000));
        return true;
    }
    if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey) {
        write(bandKey("gain"),
              clamp(value(bandKey("gain")) + (code == juce::KeyPress::upKey ? .5f : -.5f) * fine, -15, 15));
        return true;
    }
    return false;
}

FxChain::FxChain(DeviceUiModel &model, bool tape) : model_(model), tape_(tape) {
    setName("FX chain");
    rebuild();
}
void FxChain::setPad(int pad, const juce::String &name) {
    padName_ = name;
    if (pad == pad_) {
        repaint();
        return;
    }
    pad_ = pad;
    rebuild();
}
void FxChain::rebuild() {
    modules_.clear();
    for (auto kind : {FxModuleView::Eq, FxModuleView::Ott, FxModuleView::Comp, FxModuleView::Drive,
                      FxModuleView::Chorus, FxModuleView::Echo, FxModuleView::Reverb}) {
        addAndMakeVisible(modules_.add(new FxModuleView(
            model_, kind, tape_, pad_ < 0 ? juce::String() : "pad" + juce::String(pad_) + ".",
            juce::jmax(0, pad_))));
    }
    resized();
}
void FxChain::paint(juce::Graphics &g) {
    g.setFont(monoFont(9));
    g.setColour(col::acN);
    auto head = getLocalBounds().removeFromTop(22);
    g.drawText(pad_ < 0 ? "SIGNAL FLOW"
                        : "PAD " + juce::String(pad_ + 1).paddedLeft('0', 2) + "  " + padName_,
               head, juce::Justification::centredLeft);
    g.drawText(
        tape_ ? juce::String::fromUTF8("EQ  ›  OTT  ›  COMP  ›  DRIVE  ›  CHORUS  ›  TAPE ECHO  ›  REVERB")
              : juce::String::fromUTF8("EQ  ›  OTT  ›  COMP  ›  DRIVE  ›  CHORUS  ›  DELAY  ›  REVERB"),
        head, juce::Justification::centredRight);
}
void FxChain::resized() {
    auto r = getLocalBounds();
    r.removeFromTop(26);
    const int gap = 10, topH = (r.getHeight() - gap) / 2;
    auto top = r.removeFromTop(topH);
    r.removeFromTop(gap);
    auto bottom = r;
    auto place = [&](FxModuleView::Kind kind, juce::Rectangle<int> area) {
        for (auto *m : modules_)
            if (m->kind() == kind)
                m->setBounds(area);
    };
    {
        int third = (top.getWidth() - 2 * gap) / 3;
        place(FxModuleView::Eq, top.removeFromLeft(third));
        top.removeFromLeft(gap);
        place(FxModuleView::Ott, top.removeFromLeft(third));
        top.removeFromLeft(gap);
        place(FxModuleView::Comp, top);
        place(FxModuleView::Drive, bottom.removeFromLeft(juce::jmax(220, bottom.getWidth() / 4)));
        bottom.removeFromLeft(gap);
    }
    place(FxModuleView::Chorus, bottom.removeFromLeft(180));
    bottom.removeFromLeft(gap);
    place(FxModuleView::Echo, bottom.removeFromLeft((bottom.getWidth() - gap) * (tape_ ? 58 : 50) / 100));
    bottom.removeFromLeft(gap);
    place(FxModuleView::Reverb, bottom);
}
DevicePageTabs::DevicePageTabs() {
    setName("Device pages");
    for (auto *b : {&sound_, &fx_}) {
        addAndMakeVisible(b);
        b->setClickingTogglesState(true);
        b->setRadioGroupId(714);
        b->setColour(juce::TextButton::buttonOnColourId, col::acN.withAlpha(.18f));
        b->setColour(juce::TextButton::textColourOnId, col::text);
        b->onClick = [this] {
            if (onChange)
                onChange();
        };
    }
    sound_.setToggleState(true, juce::dontSendNotification);
}
void DevicePageTabs::resized() {
    auto r = getLocalBounds();
    sound_.setBounds(r.removeFromLeft(100));
    r.removeFromLeft(5);
    fx_.setBounds(r.removeFromLeft(120));
}
} // namespace fui
