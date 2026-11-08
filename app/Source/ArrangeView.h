#pragma once

#include <nc/engine/ProjectState.hpp>
#include <nc/engine/UndoStack.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <vector>

#include "AudioEngine.h"
#include "InspectorPanel.h"
#include "Session.h"
#include "Theme.h"
#include "Widgets.h"

namespace nc
{
class ArrangeView final : public juce::Component
{
public:
    explicit ArrangeView (AudioEngine& engineToUse);

    std::function<void (int)> onTrackSelected;
    std::function<void (int, int)> onInsertClicked;
    std::function<void (int)> onStripClicked;
    std::function<void (const juce::String&)> onStatus;

    void setSelectedTrack (int track);
    int getSelectedTrack() const { return selectedTrack; }
    void setTool (int toolIndex);
    void zoomIn();
    void zoomOut();
    void refresh();

    // Multi-level undo/redo (engine UndoStack, depth 100 default).
    // Every edit pushes the *before* state; drags push once at gesture start.
    bool undo();
    bool redo();
    bool canUndo() const { return undoStack.canUndo(); }
    bool canRedo() const { return undoStack.canRedo(); }
    std::size_t undoDepth() const { return undoStack.undoDepth(); }
    std::size_t redoDepth() const { return undoStack.redoDepth(); }
    void setUndoDepth (std::size_t depth) { undoStack.setMaxDepth (depth); }

    // Keyboard clip editing (Pro Tools/Cubase pattern): Tab cycles clips,
    // Left/Right nudges, Shift+Left/Right trims, Up/Down moves track,
    // ,/. cycles comp takes, C toggles an equal-power crossfade.
    // Each returns false when there is no selected clip to edit.
    bool selectNextClip();
    bool selectPrevClip();
    bool nudgeSelected (int bars);
    bool trimSelectedLeft (int bars);
    bool trimSelectedRight (int bars);
    bool moveSelectedTrack (int deltaTracks);
    bool cycleSelectedCompTake (int delta);
    bool applyCrossfadeSelected();

    // Opens the piano-roll editor for the selected MIDI clip (or the clip
    // under double-click). Edits are undoable and publish immediately.
    bool openPianoRollForSelected();
    bool openPianoRollForClip (int clipIndex);

    // Basic edit functions, also reachable from the toolbar and Edit menu.
    bool splitSelectedAtPlayhead();
    bool duplicateSelected();
    bool deleteSelected();

    // Web workspace write path. Indices address the *audio* schedule (the
    // order publishTimeline() hands to AudioEngine), so MIDI clips in between
    // cannot shift them. Each edit is undoable and republishes immediately.
    bool webEditClip (int scheduleIndex, const juce::var& patch);
    bool webSplitClip (int scheduleIndex, double bar);
    bool webDuplicateClip (int scheduleIndex);
    bool webDeleteClip (int scheduleIndex);
    void webSetLoopRange (double startBar, double endBar);
    bool hasSelectedClip() const { return selectedClip >= 0; }
    int clipCount() const { return static_cast<int> (clips.size()); }

    /** Empties the timeline for a new session: clips, selection and undo all
        go, and the engine schedule is republished empty. */
    void clearSession();

    // Loads the file through the engine and places it on the selected track.
    bool importAudioFile (const juce::File& file);

    // Places a recorded take on its armed track at the record-start bar.
    bool placeRecordedTake (const juce::File& file, int track, double startBar);

    // AudioSuite-style offline editing of the selected clip.
    bool applyAudioSuite (const nc::engine::audiosuite::Process& process);
    bool repeatSelected (int times);
    bool stripSilenceSelected();

    /** Copies every referenced source into <projectFolder>/Media and repoints
        the clips at the copies. Returns files copied, or -1 on failure. */
    int collectMediaTo (const juce::File& projectFolder, juce::String& report);

    void writeClipsToDocument (nc::engine::ProjectDocument& document, const juce::File& projectFile) const;
    int readClipsFromDocument (const nc::engine::ProjectDocument& document,
                               const juce::File& projectFile, int& missing);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    struct Clip
    {
        int track = 0;
        int startBar = 1;
        int lengthBars = 4;
        juce::String name;
        int kind = 0;
        juce::uint32 seed = 0;
        float gainDb = 0.0f;
        float fadeInBars = 0.25f;
        float fadeOutBars = 0.25f;
        float crossfadeBars = 0.0f; // equal-power overlap; 0 = linear fades
        int compTake = 0;           // active comp take (0 = first)
        int assetId = -1;
        double sourceOffsetSeconds = 0.0;
        std::vector<nc::engine::MidiNote> notes; // MIDI clips: clip-relative beats
        std::vector<nc::engine::MidiCC> ccs;
        std::vector<nc::engine::MidiBend> bends;
    };

    struct Snapshot
    {
        std::vector<Clip> clips;
        int selectedClip = -1;
    };

    enum class DragMode { none, move, trimLeft, trimRight, fadeIn, fadeOut, gain };

    void layoutAreas();
    void paintToolbar (juce::Graphics&);
    void paintRuler (juce::Graphics&);
    void paintLanes (juce::Graphics&);
    void paintClips (juce::Graphics&);
    void paintTrackHeaders (juce::Graphics&);
    void paintPlayhead (juce::Graphics&);
    void paintClipContents (juce::Graphics&, const Clip&, juce::Rectangle<float>);
    void paintClipFades (juce::Graphics&, const Clip&, juce::Rectangle<float>, bool showHandles);
    void paintClipGain (juce::Graphics&, const Clip&, juce::Rectangle<float>, bool selected);
    void paintMiniMeter (juce::Graphics&, juce::Rectangle<int>, int trackIndex);
    void paintTrackControls (juce::Graphics&, juce::Rectangle<int>, int trackIndex);

    int trackAt (juce::Point<int>) const;
    int clipAt (juce::Point<int>) const;
    int toolAt (juce::Point<int>) const;
    int functionAt (juce::Point<int>) const;
    int fadeHandleAt (const Clip&, juce::Point<int>) const;
    juce::Rectangle<float> fadeHandleBounds (const Clip&, bool fadeIn) const;
    float clipGainY (const Clip&, juce::Rectangle<float> body) const;
    juce::Rectangle<float> clipBounds (const Clip&) const;
    juce::Rectangle<int> laneBounds (int trackIndex) const;
    juce::Rectangle<int> headerControl (int trackIndex, int control) const;
    double barToX (double bar) const;
    double xToBar (double x) const;
    void seekToX (int x);
    int shownTracks() const;
    int visibleTrackRows() const;
    void ensureTrackVisible (int track);
    void toggleArm (int track);
    void splitClip (int clipIndex, int bar);
    void addClipAt (int trackIndex, int bar);
    void pushUndo();
    void publishTimeline();
    void applyMidiNotes (int clipIndex, std::vector<nc::engine::MidiNote> notes);

    AudioEngine& engine;
    InspectorPanel inspector;

    std::vector<Clip> clips;
    int selectedTrack = 0;
    int selectedClip = -1;
    int hoverClip = -1;
    int hoverTrack = -1;
    int tool = 0;
    int visibleBars = 16;
    int firstVisibleTrack = 0;

    // Below this a lane stops being readable; taller sessions scroll instead.
    static constexpr int minLaneHeight = 44;
    int loopStartBar = 1;
    int loopEndBar = 17;
    bool snap = true;

    bool draggingPlayhead = false;
    bool draggingLoop = false;
    bool dragUndoPushed = false;
    DragMode dragMode = DragMode::none;
    int loopDragMode = 0;
    int dragClipIndex = -1;
    int dragOriginBar = 1;
    int dragOriginStart = 1;
    int dragOriginLength = 4;
    int dragOriginX = 0;
    float dragOriginGain = 0.0f;
    float dragOriginFadeIn = 0.0f;
    float dragOriginFadeOut = 0.0f;
    double dragOriginSourceOffset = 0.0;
    int loopOriginBar = 1;
    int loopOriginLength = 16;
    double dragAnchorBar = 1.0;
    double viewStartBar = 1.0;
    float lastPlayheadX = -1.0f;

    nc::engine::UndoStack<Snapshot> undoStack;
    bool wasPlaying = false;

    std::array<juce::Rectangle<int>, 6> toolRects;
    std::array<juce::Rectangle<int>, 3> functionRects;

    juce::Rectangle<int> toolbarArea, tracksHeaderArea, trackHeaderColumn, rulerArea,
                         loopArea, laneArea, inspectorArea, timelineArea, trackNameHeader,
                         snapArea, gridArea, zoomOutArea, zoomInArea, barsReadoutArea, toolNameArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ArrangeView)
};
} // namespace nc
