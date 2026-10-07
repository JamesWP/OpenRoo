/* ScriptPlayer: the level instruction-script (.jjs) player, a sub-object of
 * Game.  It holds a level's script, up to 1000 lines of 1000 bytes, and plays
 * it: the intro flythrough camera (a spline, then glides and waits), captions,
 * and the script's streamed sounds.  The level report reads the scripts' texts
 * through it too. */
#pragma once

#include <ostream>
 
#include "audiodev.h"
#include "splinepath.h"

class SoundManager;

class ScriptPlayer {
public:
     

    enum { LINE_SIZE = 1000, LINE_MAX = 1000 };

    // Parses <path>.jjs into the line table.
    int  readForLevel(const char *path);
    // The level report's reader: writes the script's texts to sink and counts
    // its spline lines and text blocks.
    int  readTextsForReport(const char *path, std::ostream &sink);

    // The per-frame tick: the camera spline, a glide, the waits, and the next
    // line through playScript.  now and dt in milliseconds.
    void tick(double now, double dt);
    // Stops, releases and deletes each of the 255 stream slots.
    void releaseStreams();

    // 1 once a script has loaded; zeroed before each level's read.  Every
    // "this level has a script" test reads it.
    int            loaded() const                  { return loaded_; }
    void           setLoaded(int l)                { loaded_ = l; }
    // The script is running: set when a level is loaded (the flythrough is
    // armed), cleared by Enter and by the tick after the last line.  The
    // checkpoint restore needs it and loaded() both.
    int            running() const                 { return running_; }
    // The caption the running script shows (the frame renderer draws it).
    const char    *caption() const                 { return scratch_; }
    void           setRunning(int r)               { running_ = r; }
    // The camera spline is being evaluated; the tick drives it while this and
    // cameraMode() are set.
    void           setSplineActive(int a)          { splineActive_ = a; }
    int            splineActive() const            { return splineActive_; }

    // What the checkpoint restore copies into the Game: the camera mode, the
    // distance, the eye and the spline's last point.
    unsigned char  cameraMode() const              { return cameraMode_; }
    float          cameraDistance() const          { return cameraDistance_; }
    float          eye(int i) const                { return eye_[i]; }
    float          splinePoint(int i) const        { return splinePoint_[i]; }

    // Lines in the table.
    unsigned short lineCount() const               { return lineCount_; }

    // The level report's two counters.
    unsigned short splineLines() const             { return splineLines_; }
    void           setSplineLines(unsigned short n) { splineLines_ = n; }
    unsigned short textBlocks() const              { return textBlocks_; }
    void           setTextBlocks(unsigned short n) { textBlocks_ = n; }

    // The sound manager the script's sounds play through.
    void           setSoundManager(SoundManager *sm) { soundManager_ = sm; }

    // The embedded spline, for callers that hand it on.
    SplinePath         *spline()                   { return &spline_; }

    ScriptPlayer();
    virtual ~ScriptPlayer();
    ScriptPlayer(const ScriptPlayer &) = delete;
    ScriptPlayer &operator=(const ScriptPlayer &) = delete;
    void clearStreams();  // zeroes streams_[]


private:
    unsigned char playScript(const char *line);
    void updateStreamWait();
    void updateSpline();
    void updateWait();
    void updateGlide();
    void runNextCommand();
     

    unsigned short splineLines_{};
    unsigned short textBlocks_{};
    unsigned short againLine_{};       // "fromhere" sets it to cursor + 1; "again" jumps back to it
    unsigned int   durations_[255]{};  // seconds a soundless "playwave WAIT" waits, by id
    SoundManager  *soundManager_{};       // set by the fixed sound setup
    unsigned char  waitStream_{};         // the stream waitingOnStream_ waits on
    audiodev::Stream *streams_[255];  // by id
    unsigned int   waitingOnStream_{};  // "playwave <id> <wait>": the tick stalls until its stream finishes
    unsigned int   streamReady_{};      // "initwave"'s prepare result
    audiodev::Stream stream_;         // the embedded stream
    unsigned int   field_92d_{};  // never read
    float          splinePoint_[3]{};
    SplinePath     spline_;
    int            splineActive_{};
    unsigned int   field_955_{};  // never read
    float          angle_[3]{};   // "anglexyz"; never read
    double         duration_{};   // ms, of the spline or the glide
    double         dt_{};         // the tick's frame dt
    float          dir_[3]{};     // the glide's unit direction
    double         start_{};      // now_ when the spline or glide began
    float          speed_{};      // glide units per second
    float          from_[3]{};    // the eye when the glide began; never read
    float          target_[3]{};  // the glide's end point
    unsigned int   moving_{};     // "movetoxyz" in progress
    double         now_{};        // the tick's clock
    int            loaded_{};
    float          cameraDistance_{};          // set by "distance"
    char           currentLine_[LINE_SIZE]{};  // the line being played
    float          eye_[3]{};                  // set by "eyexyz" and the glide
    unsigned char  cameraMode_{};
    int            running_{};
    double         waitSeconds_{};
    unsigned int   waiting_{};    // a timed wait in progress
    double         waitStart_{};  // now_ when it began
    unsigned short cursor_{};     // the next line to play
    unsigned short lineCount_{};
    char           scratch_[0x3ea]{};  // the caption, and scratch for parsing
    char           lines_[LINE_MAX][LINE_SIZE]{};
};
