/* FrontEnd: the front end's state, owned by Game.  The menu tree, the text
 * entries (cheat and name), the save slots and the high-score table, and the
 * key-rebind state the menus' input handling uses.
 *
 * It reads no other part of Game; Game_Boot sets it up (gameboot.cpp).
 */
#pragma once

#include "textentry.h"
#include "saveslots.h"
#include "menutree.h"
#include "highscores.h"
#include "inputdev.h"

class FrontEnd {
public:
    /* The menu (menutree.h). */
    MenuTree       *menu()                           { return &menu_; }
    const MenuTree *menu() const                     { return &menu_; }

    /* The cheat-code entry (HandleTypedCheatCode polls it every tick) and
     * the name entry (save names and the high-score name). */
    TextEntry     *cheatEntry()                      { return &cheatEntry_; }
    TextEntry     *nameEntry()                       { return &nameEntry_; }
    /* The buffer HandleTypedCheatCode matches typed cheats in. */
    unsigned char *cheatBuffer()                     { return cheatBuffer_; }

    /* The high-score table (highscores.h). */
    HighScoreTable       *highScores()               { return &highScores_; }
    const HighScoreTable *highScores() const         { return &highScores_; }
    /* The save-slot table (saveslots.h). */
    SaveSlots     *saveSlots()                       { return &saveSlots_; }

    /* Key-rebind capture: the controls menu's rebind nodes name the action
     * and set the flag; HandleKeypress captures the next binding and clears
     * it.  RenderControlsRemap reads the flag. */
    int            rebindActive() const              { return rebindActive_; }
    void           setRebindActive(int a)            { rebindActive_ = a; }
    unsigned char  rebindCode() const                { return rebindCode_; }
    void           setRebindCode(unsigned char c)    { rebindCode_ = c; }
    char          *rebindAction()                    { return rebindAction_; }

    /* Save-name text entry in progress (nameEntry()):
     * HandleKeypress sets it on the save-slot nodes, routes keys to the
     * entry while it is set, and clears it when entry ends. */
    int            textEntryActive() const           { return textEntryActive_; }
    void           setTextEntryActive(int a)         { textEntryActive_ = a; }

    /* keypress.cpp: creditsLatch is set when the credits node (5) is first
     * handled, and cleared once the menu leaves it.  creditsRestart asks the
     * credits scroll to restart from now (Menu_RenderCreditsScroll, which
     * clears it). */
    int            creditsLatch() const              { return creditsLatch_; }
    void           setCreditsLatch(int v)             { creditsLatch_ = v; }
    int            creditsRestart() const              { return creditsRestart_; }
    void           setCreditsRestart(int v)             { creditsRestart_ = v; }

private:
    int            creditsLatch_{};
    int            creditsRestart_{};
    /* The typed-cheat buffer; declared up to the cheat entry that follows.
     * Its real length is not established. */
    unsigned char  cheatBuffer_[0x100]{};
    TextEntry      cheatEntry_;
    HighScoreTable highScores_;
    int            textEntryActive_{};
    TextEntry      nameEntry_;
    SaveSlots      saveSlots_;
    unsigned char  rebindCode_{};
    char           rebindAction_[0x100]{};
    int            rebindActive_{};
    MenuTree       menu_;
};
