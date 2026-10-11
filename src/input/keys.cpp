/* Keys: the single-key polls.  input_key_down is how every key poll in the
 * game happens; the record and replay in testing/record.h sit behind it. */
#include "keys.h"
#include "record.h"
#include "inputdev.h"
#include "clock.h"
#include "logger.h"

bool input_key_down(int key)
{
    if (input_debug()) {  // which call sites actually execute
        static int n = 0;
        if (n < 40) {
            n++;
            g_logger.write("askdbg: frame %u vkey=0x%02X from ret=%p\n",
                      clock_frame(), key, __builtin_return_address(0));
        }
    }
    bool v;
    if (record_replaying()) {
        replay_async(key, &v);
        return v;
    }
    v = inputdev::keyDown(key);
    record_async(key, v);
    return v;
}
