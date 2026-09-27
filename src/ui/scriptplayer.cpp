/* FORMAT: a .jjs script is text: entries end with ';', each followed by a
 * newline.  The level reader stores each entry, with "\n" appended, as one
 * 1000-byte line; the report reader works on whole lines instead.
 *
 * The player runs one line at a time.  playScript() runs a line: it only sets
 * state (a glide target, a wait, a spline, a camera value).  The update
 * functions advance that state by one frame, and tick() orders them: the
 * spline runs every frame alongside later commands; only the stream wait, the
 * timed wait and the glide hold the next line back; at most one line runs per
 * frame; a frame that fetches a line does no waiting or gliding, and a wait
 * that expires fetches the next line only on the frame after.
 *
 * KAROO_JJS_FX is a negative control: "blank" stores every entry as ".", so
 * the instruction captions come up empty; "glide" reverses every movetoxyz
 * direction.  KAROO_JJS_DIAG=1 logs each command run, and
 * KAROO_JJS_DUMP=<file> appends each script's entry count and an FNV-1a hash
 * of its lines. */

#include "camera.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "log.h"
#include "scriptplayer.h"
#include "stream.h"
#include <stdlib.h>
#include "gamelog.h"
#include "gameglobals.h"
#include "splinepath.h"
#include <stdlib.h>
#include <math.h>

/* Stops (if still playing), releases and deletes each stream, re-reading the
 * slot before each step; the deleting destructor comes from the stream's own
 * vtable. */
void ScriptPlayer::releaseStreams()
{
    typedef void (*deleting_dtor_fn)(void *, int);

    for (int i = 0; i < 255; ++i) {
        if (streams_[i] == NULL)
            continue;
        if (streams_[i]->dwThread_done == 0)
            CStream_Stop(streams_[i]);
        CStream_ReleaseResources(streams_[i]);
        if (streams_[i] != NULL)
            (*(deleting_dtor_fn *)*(void **)streams_[i])(streams_[i], 1);
        streams_[i] = NULL;
    }
}

void JJScript_ReleaseScriptStreamBuffers(ScriptPlayer *self)
{
    self->releaseStreams();
}

/* The empty string copied into the object before a parse. */
#define GLOBAL_SCRATCH_STR ""

/* 64 KB: everything the 16-bit masked index can address, so an overlong entry
 * cannot leave the buffer.  No shipped entry is over 196 characters. */
static char s_entry[0x10000];

#define JJS_LOG_FIRST 6

static bool fx_blank(void)
{
    static int cached = -1;
    if (cached < 0) {
        char buf[16];
        cached = 0;
        if (GetEnvironmentVariableA("KAROO_JJS_FX", buf, sizeof(buf)))
            cached = (lstrcmpiA(buf, "blank") == 0);
        log_write("jjscript: FX mode = %s\n", cached ? "blank" : "off");
    }
    return cached != 0;
}

int JJScript_ReadForLevel(ScriptPlayer *self, const char *path)
{
    return self->readForLevel(path);
}

int ScriptPlayer::readForLevel(const char *path)
{
    char name[128];  // PRESERVED: 128 bytes, unchecked
    FILE *fp;
    unsigned idx = 0;  // 32-bit, masked when indexing
    static int logged = 0;

    // Cleared in this order, before the file is even opened.
    againLine_   = 0;
    field_92d_   = 0;
    field_955_   = 0;
    loaded_      = 0;
    cursor_      = 0;
    waiting_     = 0;
    cameraMode_  = 0;
    running_     = 0;
    moving_      = 0;
    splineActive_ = 0;
    waitingOnStream_ = 0;
    streamReady_ = 0;

    releaseStreams();

    lineCount_ = 0;
    strcpy(scratch_, GLOBAL_SCRATCH_STR);

    strcpy(name, path);
    strcat(name, ".jjs");

    fp = fopen(name, "r");
    if (fp == NULL)
        return 0;  // PRESERVED: a missing file leaves the object cleared

    while (!feof(fp)) {
        int c = fgetc(fp);  // PRESERVED: the end-of-file read's -1 is stored as 0xff

        if ((char)c == ';') {
            WORD  n   = lineCount_;
            char *rec = lines_[0] + n * LINE_SIZE;  // PRESERVED: no bound on the entry count

            s_entry[idx & 0xffff] = '\0';
            strcat(s_entry, "\n");
            idx = 0;

            if (fx_blank())
                strcpy(rec, ".\n");
            else
                strcpy(rec, s_entry);

            lineCount_ = (WORD)(n + 1);

            fgetc(fp);  // the newline after the ';', discarded
        } else {
            s_entry[idx & 0xffff] = (char)c;  // PRESERVED: the index is masked to 16 bits
            idx++;
        }
    }

    fclose(fp);
    loaded_ = 1;

    // KAROO_JJS_DUMP: the path, the entry count and an FNV-1a hash of every
    // line, for comparison with an independent parse.
    {
        char dump[MAX_PATH];
        if (GetEnvironmentVariableA("KAROO_JJS_DUMP", dump, sizeof(dump))) {
            HANDLE h = CreateFileA(dump, FILE_APPEND_DATA, FILE_SHARE_READ, NULL,
                                   OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (h != INVALID_HANDLE_VALUE) {
                unsigned long hash = 2166136261UL;
                unsigned n = lineCount_;
                for (unsigned i = 0; i < n; i++) {
                    const char *rec = lines_[i];
                    for (const char *q = rec; *q; q++) {
                        hash ^= (unsigned char)*q;
                        hash *= 16777619UL;
                    }
                }
                char line[512];
                int len = wsprintfA(line, "%s entries=%u hash=%08lx\r\n",
                                    name, n, hash);
                DWORD w = 0;
                WriteFile(h, line, (DWORD)len, &w, NULL);
                CloseHandle(h);
            }
        }
    }

    if (logged < JJS_LOG_FIRST) {
        logged++;
        log_write("jjscript: '%s' entries=%u\n",
                  name, (unsigned)lineCount_);
    }
    return 1;
}

/* The level report's reader: copies a script's "text" blocks to the sink
 * (ScriptTexts.txt) and counts spline lines.  A line starting "text" opens a
 * block; inside it, lines of two or more characters are written, after a
 * "\n%d. Text:\n" header once per block; a line starting ';' closes the block
 * and counts it in textBlocks_; a line ending ';' closes it too, without the
 * ';'.  Any line starting "splinexyz" counts in splineLines_.
 *
 * PRESERVED:
 *   - It returns 0 on every path, a failed open included.
 *   - The line buffer is pre-filled, pointlessly, before the first read.
 *   - fgets' result is not checked, so at the end the last line is
 *     examined once more.
 *   - A second in-block flag is not cleared on the trailing-';' path; it is
 *     only read while the first is set, so it changes nothing.
 *   - The header number is masked to 16 bits before the +1.
 *   - The path and scratch buffers are fixed-size and unchecked.
 * Checked byte for byte by tools/levelreport.py over all 80 levels. */

#define JJSR_LINE_MAX   0x80
#define JJSR_LOG_FIRST  4

int
JJScript_ReadTextsForReport(ScriptPlayer *self, const char *path, FILE *sink)
{
    return self->readTextsForReport(path, sink);
}

int ScriptPlayer::readTextsForReport(const char *path, FILE *sink)
{
    char name[0x80];
    char line[JJSR_LINE_MAX];
    char scratch[0x100];
    FILE *fp;
    int inBlock = 0, inBlockMirror = 0, wroteHeader = 0;
    unsigned textIndex = 0;
    static int logged = 0;

    strcpy(name, path);
    strcat(name, ".jjs");

    fp = fopen(name, "r+t");  // text update mode, needlessly writable

    strcpy(line, GLOBAL_SCRATCH_STR);  // PRESERVED: overwritten by the first read

    if (fp == NULL)
        return 0;  // PRESERVED: always 0

    while (!feof(fp)) {
        char prefix4[5], prefix9[10];
        size_t len;

        fgets(line, JJSR_LINE_MAX, fp);  // PRESERVED: result unchecked

        memcpy(prefix4, line, 4);
        prefix4[4] = '\0';

        if (!inBlock) {
            if (strcmp(prefix4, "text") == 0) {
                inBlock       = 1;
                inBlockMirror = 1;  // PRESERVED: the redundant mirror
                wroteHeader   = 0;
            }
        } else if (line[0] == ';') {
            textBlocks_ = (WORD)(textBlocks_ + 1);
            inBlock       = 0;
            inBlockMirror = 0;
        } else {
            len = strlen(line);
            if (len > 1 && inBlockMirror) {  // two or more characters, counting the newline
                if (!wroteHeader) {
                    sprintf(scratch, "\n%d. Text:\n", (int)((textIndex & 0xffff) + 1));
                    fputs(scratch, sink);
                    wroteHeader = 1;
                }
                if (line[len - 2] == ';') {
                    inBlock     = 0;  // PRESERVED: the mirror is not cleared
                    wroteHeader = 0;
                    line[len - 2] = '\0';
                    fputs(line, sink);
                    fputs("\n", sink);
                    textIndex++;
                } else {
                    fputs(line, sink);
                }
            }
        }

        memcpy(prefix9, line, 9);
        prefix9[9] = '\0';
        if (strcmp(prefix9, "splinexyz") == 0)
            splineLines_ = (WORD)(splineLines_ + 1);
    }

    fclose(fp);

    if (logged < JJSR_LOG_FIRST) {
        logged++;
        log_write("jjsreport: '%s' texts=%u splines=%u\n", name, textIndex,
                  (unsigned)splineLines_);
    }
    return 0;  // PRESERVED: always 0
}

/* The line commands.  PRESERVED, each marked where it happens: movetoxyz keeps
 * only the low byte of each coordinate; the end of a spline negates the eye's
 * y; "distance" compares the whole line, not the token; "text" on a line under
 * six characters copies a negative length; initwave keeps a pointer into a
 * stack buffer; an all-delimiter line reaches strcmp with NULL. */

static const char JJS_DELIMS[] = " ,\t\n;";

static bool jjs_fx_glide(void)
{
    static int cached = -1;
    if (cached < 0) {
        char buf[16];
        cached = GetEnvironmentVariableA("KAROO_JJS_FX", buf, sizeof(buf))
                 && lstrcmpiA(buf, "glide") == 0;
    }
    return cached != 0;
}

static bool jjs_diag(void)
{
    static int cached = -1;
    if (cached < 0) {
        char buf[16];
        cached = GetEnvironmentVariableA("KAROO_JJS_DIAG", buf, sizeof(buf))
                 && buf[0] == '1';
    }
    return cached != 0;
}

/* Truncation toward zero to 64 bits; callers keep the bits they need. */
static long long ftol64(double v) { return (long long)v; }

static float next_float(void) { return (float)atof(strtok(NULL, JJS_DELIMS)); }

unsigned char ScriptPlayer::playScript(const char *line)
{
    char buf[LINE_SIZE];
    strcpy(buf, line);
    const char *cmd = strtok(buf, JJS_DELIMS);
    SplinePath *spline = &spline_;

    if (jjs_diag())
        log_write("jjscript: line %u cmd %s\n", cursor_, cmd ? cmd : "(null)");

    if (strcmp(cmd, "fromhere") == 0) {
        againLine_ = cursor_ + 1;
        return 0x37;
    }
    if (strcmp(cmd, "gotoxyz") == 0) {
        eye_[0] = next_float();
        eye_[1] = next_float();
        eye_[2] = next_float();
        return 2;
    }
    if (strcmp(cmd, "movetoxyz") == 0) {
        // PRESERVED: each target coordinate keeps only its low byte.
        for (int i = 0; i < 3; ++i)
            target_[i] = (float)(int)(ftol64(atof(strtok(NULL, JJS_DELIMS))) & 0xff);
        float speed = next_float();
        moving_ = 1;
        speed_ = speed;
        memcpy(from_, eye_, sizeof(from_));
        start_ = now_;
        float dx = target_[0] - eye_[0];
        float dy = target_[1] - eye_[1];
        float dz = target_[2] - eye_[2];
        float len = (float)sqrt((double)dx * dx + (double)dy * dy + (double)dz * dz);
        // DETERMINISM: a 32-bit truncation widened as unsigned.
        duration_ = (double)(unsigned int)ftol64(len / (speed * 0.001f));
        dir_[0] = dx / len;
        dir_[1] = dy / len;
        dir_[2] = dz / len;
        if (jjs_fx_glide())
            for (int i = 0; i < 3; ++i)
                dir_[i] = -dir_[i];
        return 3;
    }
    if (strcmp(cmd, "observe") == 0) {
        cameraMode_ = (unsigned char)ftol64(atof(strtok(NULL, JJS_DELIMS)));
        splineActive_ = (cameraMode_ == 0);
        return 5;
    }
    if (strcmp(cmd, "setcamposxyz") == 0) {
        splinePoint_[0] = next_float();
        splinePoint_[1] = (float)-atof(strtok(NULL, JJS_DELIMS));
        splinePoint_[2] = next_float();
        return 0xc;
    }
    if (strcmp(cmd, "setcamtargetxyz") == 0) {
        eye_[0] = next_float();
        eye_[1] = (float)-atof(strtok(NULL, JJS_DELIMS));
        eye_[2] = next_float();
        return 0xd;
    }
    if (strcmp(cmd, "again") == 0) {
        running_ = 1;
        cursor_ = againLine_;
        return 6;
    }
    if (strcmp(cmd, "break") == 0) {
        cursor_ = 0;
        running_ = 0;
        return 7;
    }
    // PRESERVED: compares the whole buffer, not the token; the same unless the
    // line begins with a delimiter.
    if (strcmp(buf, "distance") == 0) {
        cameraDistance_ = next_float();
        return 8;
    }
    if (strcmp(cmd, "text") == 0) {
        // Drops "text " and the trailing "\n".  PRESERVED: a line under six
        // characters gives a negative length.
        size_t n = strlen(line) - 6;
        memcpy(scratch_, line + 5, n);
        scratch_[n] = '\0';
        return 9;
    }
    if (strcmp(cmd, "wait") == 0) {
        waitStart_ = now_;
        waiting_ = 1;
        waitSeconds_ = atof(strtok(NULL, JJS_DELIMS));
        return 4;
    }
    if (strcmp(cmd, "anglexyz") == 0) {
        angle_[0] = next_float();
        angle_[1] = next_float();
        angle_[2] = next_float();
        return 10;
    }
    if (strcmp(cmd, "splinexyz") == 0) {
        field_92d_ = 1;
        Spline_PurgeControlPoints(spline);
        memcpy(splinePoint_, g_camera.eye, sizeof(splinePoint_));  // the camera's eye (camera.h)
        start_ = now_;
        // DETERMINISM: an int product widened as unsigned.
        duration_ = (double)(unsigned int)(atoi(strtok(NULL, JJS_DELIMS)) * 1000);
        float x = next_float();
        float y = (float)-atof(strtok(NULL, JJS_DELIMS));
        char *tz = strtok(NULL, JJS_DELIMS);
        float z = (float)atof(tz);
        Spline_AddControlPoint(spline, x, z, y);  // (x, z, -y)
        // Further triples; a point is added only when all three parsed, and a
        // missing value keeps the previous one.  Ends when a z is missing.
        while (tz != NULL) {
            int got = 0;
            char *t = strtok(NULL, JJS_DELIMS);
            if (t) { x = (float)atof(t); ++got; }
            t = strtok(NULL, JJS_DELIMS);
            if (t) { y = (float)-atof(t); ++got; }
            tz = strtok(NULL, JJS_DELIMS);
            if (tz) { z = (float)atof(tz); ++got; }
            if (got == 3)
                Spline_AddControlPoint(spline, x, z, y);
        }
        splineActive_ = 1;
        return 0xb;
    }
    if (strcmp(cmd, "initwave") == 0) {
        GameLog_LogMessage(&g_logger, 1, "IS: initwave noticed");
        char *name = strtok(NULL, JJS_DELIMS);
        if (name) {
            if (soundManager_)
                streamWave_.pFilename = name;  // PRESERVED: points into buf, a stack buffer
            char *tid = strtok(NULL, JJS_DELIMS);
            if (tid) {
                unsigned char id = (unsigned char)atoi(tid);
                if (!soundManager_) {
                    durations_[id] = 10;
                    return 0xc;
                }
                if (streams_[id]) {
                    GameLog_LogMessage(&g_logger, 3,
                        "IS: warning - Stream sound buffer width id %d already initialized!", id);
                    return 0xc;
                }
                void *mem = malloc(sizeof(CStreamSoundbuffer));
                CStreamSoundbuffer *s = mem ? CStream_Initialize((CStreamSoundbuffer *)mem) : NULL;
                streams_[id] = s;
                streamReady_ = CStream_Prepare(s, &streamWave_);
                if (streamReady_)
                    GameLog_LogMessage(&g_logger, 1,
                        "IS: Stream buffer width name %s successfully initialized, ID=%d",
                        streamWave_.pFilename, id);
                else
                    GameLog_LogMessage(&g_logger, 1,
                        "IS: warning - Stream sound buffer width name %s could not initialized !",
                        streamWave_.pFilename);
            }
        }
        return 0xc;
    }
    if (strcmp(cmd, "playwave") == 0) {
        char *tid = strtok(NULL, JJS_DELIMS);
        if (tid) {
            unsigned char id = (unsigned char)atoi(tid);
            if (!soundManager_) {
                // Soundless: "WAIT" waits the id's nominal duration.
                char *w = strtok(NULL, JJS_DELIMS);
                if (w && strcmp(w, "WAIT") == 0) {
                    waitStart_ = now_;
                    waiting_ = 1;
                    waitSeconds_ = (double)durations_[id];
                }
            } else if (streams_[id]) {
                CStream_Play(streams_[id]);
                waitingOnStream_ = 0;
                char *w = strtok(NULL, JJS_DELIMS);
                if (w && strcmp(w, "WAIT") == 0) {
                    waitingOnStream_ = 1;
                    waitStream_ = id;
                }
                GameLog_LogMessage(&g_logger, 1,
                    "IS: Stream sound buffer width ID=%d started,wait=%d", id, waitingOnStream_);
            }
        }
        return 0xd;
    }
    if (cmd[0] == '/' && cmd[1] == '/')
        return 0xff;
    return 0;
}

/* A "playwave <id> WAIT" wait ends when that stream has finished. */
void ScriptPlayer::updateStreamWait()
{
    if (waitingOnStream_ && streams_[waitStream_]->dwThread_done) {
        waitingOnStream_ = 0;
        streamReady_ = 0;
    }
}

/* The camera spline, evaluated at the elapsed fraction of its duration. */
void ScriptPlayer::updateSpline()
{
    if (!splineActive_ || !cameraMode_)
        return;
    double elapsed = now_ - start_;
    if (elapsed < duration_) {
        float out[3];
        const float *p = (const float *)Spline_EvalBezierPath(
            &spline_, out, (float)(elapsed / duration_));
        memcpy(splinePoint_, p, sizeof(splinePoint_));
    } else {
        eye_[1] = -eye_[1];  // PRESERVED
        splineActive_ = 0;
    }
}

/* A "wait <seconds>" ends once that many seconds of the clock have passed. */
void ScriptPlayer::updateWait()
{
    if (waiting_ && waitSeconds_ * 1000.0 <= now_ - waitStart_)
        waiting_ = 0;
}

/* The movetoxyz glide: step along dir_, snap to the target when time is up. */
void ScriptPlayer::updateGlide()
{
    if (!moving_)
        return;
    if (duration_ <= now_ - start_) {
        moving_ = 0;
        memcpy(eye_, target_, sizeof(eye_));
        return;
    }
    float dt = (float)dt_;
    float step = speed_ * 0.001f;
    for (int i = 0; i < 3; ++i)
        eye_[i] = dt * dir_[i] * step + eye_[i];
}

/* Fetches the line at the cursor and runs it; logs it if it is not a command.
 */
void ScriptPlayer::runNextCommand()
{
    if (lineCount_ < cursor_)
        return;
    strcpy(currentLine_, lines_[cursor_]);
    ++cursor_;
    if (lineCount_ < cursor_)
        running_ = 0;
    if (playScript(currentLine_))
        return;
    currentLine_[strlen(currentLine_) - 1] = '\0';  // drop the "\n"
    GameLog_LogMessage(&g_logger, 3, "IS:** error at command %d:%s **",
                       (int)cursor_, currentLine_);
}

void ScriptPlayer::tick(double now, double dt)
{
    now_ = now;
    dt_ = dt;
    updateStreamWait();
    updateSpline();
    if (waitingOnStream_)
        return;
    if (!waiting_ && !moving_) {
        runNextCommand();
        return;
    }
    updateWait();
    updateGlide();
}

static void *const g_ScriptPlayerVtable[1] = { (void *)&ScriptPlayer_ScalarDestructor };

/* The embedded stream and spline, then these stores. */
void ScriptPlayer::construct()
{
    CStream_Initialize(&stream_);
    Spline_Construct(&spline_);
    cursor_       = 0;
    splineActive_ = 0;
    soundManager_ = NULL;
    vtable_       = g_ScriptPlayerVtable;
    clearStreams();
}

/* A prepared stream still playing is stopped and released first; one that
 * finished is left to its deinit. */
void ScriptPlayer::destruct()
{
    vtable_ = g_ScriptPlayerVtable;
    if (streamReady_ != 0 && stream_.dwThread_done == 0) {
        CStream_Stop(&stream_);
        CStream_ReleaseResources(&stream_);
        streamReady_ = 0;
    }
    Spline_Destruct(&spline_);
    CStream_DeinitInstance(&stream_);
}

void ScriptPlayer::clearStreams()
{
    memset(streams_, 0, sizeof(streams_));
}

ScriptPlayer *
ScriptPlayer_ScalarDestructor(ScriptPlayer *self, unsigned char flags)
{
    self->destruct();
    if (flags & 1)
        free(self);
    return self;
}
