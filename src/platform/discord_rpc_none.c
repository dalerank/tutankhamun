// Discord Rich Presence where there is no Discord client to talk to (the browser build): the presence never starts,
// and the game's calls (game/discord_presence.c) find it not running.

#include "platform/discord_rpc.h"

void discord_rpc_set_verbose(bool verbose)
{
    (void) verbose;
}

bool discord_rpc_start(const char *app_id)
{
    (void) app_id;
    return false;
}

bool discord_rpc_running(void)
{
    return false;
}

void discord_rpc_set_activity(const char *details, const char *state)
{
    (void) details;
    (void) state;
}

void discord_rpc_stop(void)
{
}
