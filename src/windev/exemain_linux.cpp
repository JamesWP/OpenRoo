/* The executable's entry point on Linux: process setup, the game's main, process
 * teardown.  The arguments are joined into one command line, as WinMain gets it. */
#include <string>
#include "process.h"
#include "main.h"

int main(int argc, char **argv)
{
    std::string cmd;
    for (int i = 1; i < argc; i++) {
        if (i > 1) cmd += ' ';
        cmd += argv[i];
    }
    Process_Attach("karoo_hooks.log");
    int r = Main_WinMain(cmd.c_str());
    Process_Detach();
    return r;
}
