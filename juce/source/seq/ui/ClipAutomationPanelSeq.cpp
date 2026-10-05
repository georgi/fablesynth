#include "ClipAutomationPanel.h"
#include "../SeqProcessor.h"
namespace fui {
ClipAutomationPanel::ClipAutomationPanel(SeqAudioProcessor& p) : ClipAutomationPanel() { proc_=&p; }
void ClipAutomationPanel::setTarget(int scene,int track,ParameterSource source,std::function<int()> pad) {
    scene_=scene; track_=track;
    Source context;
    context.identity=juce::String(scene)+":"+juce::String(track);
    context.machine=track>=0 && track<(int)proc_->conductor().session().tracks.size()
        ? proc_->conductor().session().tracks[(size_t)track].machine : fable::Machine::WT1;
    context.clip=[this]() -> const fable::ClipData* {
        const auto& s=proc_->conductor().session();
        if (scene_<0 || scene_>=(int)s.scenes.size() || track_<0 || track_>=(int)s.tracks.size()) return nullptr;
        const auto& sc=s.scenes[(size_t)scene_];
        return sc.hasClip[(size_t)track_] ? &sc.clips[(size_t)track_] : nullptr;
    };
    context.beginEdit=[this]{ proc_->pushUndoSnapshot(); };
    context.undo=[this]{ proc_->undo(); };
    context.redo=[this]{ proc_->redo(); };
    context.write=[this](const auto& lanes){ proc_->conductor().updateClipAutomation(scene_,track_,lanes); };
    context.elapsed=[this]{
        if (!clip() || !proc_->conductor().playing() || proc_->conductor().ownerOf(track_)!=scene_) return -1.0;
        return std::max(0.0,proc_->currentFrame.load()-proc_->conductor().anchor())/
            ((proc_->preparedSampleRate()*60)/proc_->conductor().session().bpm/4);
    };
    setSource(std::move(context),std::move(source),std::move(pad));
}
}
