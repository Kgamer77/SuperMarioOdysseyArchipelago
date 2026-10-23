#pragma once

// Runtime-configurable multiplayer relay server address, replacing the old
// compile-time-only SMOR_MP_SERVER_IP/SMOR_MP_SERVER_PORT baked into the
// binary. Ships a plain, human-editable sd:/SMO-Randomizer/server.ini
// (key=value lines) so a different server can be pointed at without
// recompiling - useful for streamers/other communities running their own
// relay - and so a player never has to type an IP on stream via the in-game
// keyboard just to connect. An in-game override (Client::openKeyboardIP()/
// openKeyboardPort(), reached from ModConfig's Multiplayer page) can still
// change it at runtime; that override is written back to the same file so
// it persists across reboots.
namespace ServerConfig {

    // Reads sd:/SMO-Randomizer/server.ini if present; writes it with the
    // compiled-in default if missing or unparseable. Call once, early at
    // boot (see SDFileRedirection.cpp), before anything could possibly try
    // to connect.
    void load();

    const char* getIp();
    int getPort();

    // Whether receives are issued non-blocking (server.ini: nonblocking_recv).
    //
    // DEFAULT OFF, and that default is deliberate. Real hardware does not need
    // it: concurrent send-while-recv-is-parked works there, which is why
    // multiplayer has always worked on a Switch. Ryujinx is where a blocking
    // recv also blocks a concurrent socket::Send on the same fd, deadlocking
    // every outgoing packet the moment the recv thread starts.
    //
    // Keeping it off by default means a hardware SD card gets byte-for-byte the
    // socket behaviour that already works, and only a card that explicitly asks
    // for it (an emulator's) takes the different path. server.ini lives per-SD
    // card, so the same binary can be correct on both.
    bool useNonBlockingRecv();

    // Sets a new address and immediately persists it to server.ini, so an
    // in-game override survives a reboot the same way the shipped default
    // does.
    void setOverride(const char* ip, int port);

    // Session-only override: changes the address getIp()/getPort() report for
    // the rest of this boot WITHOUT touching server.ini. Backs the title
    // Multiplayer menu's IP/Port rows, which are deliberately NOT persisted -
    // a player pointing at a friend's server for one session should not have
    // that silently become their permanent default.
    //
    // Kept in storage separate from the file-backed values specifically so
    // load() cannot clobber it: Client::connectWithRoomCode() calls load() on
    // every connect (to pick up an .ini edited after boot), which would
    // otherwise throw away the override on the very connect it was set for.
    // Pass null/0 to leave that half unchanged.
    void setSessionOverride(const char* ip, int port);

    // True once either half of a session override has been set. Only used for
    // display ("unsaved changes" marker), never for connect decisions.
    bool hasSessionOverride();

    // Promotes whatever getIp()/getPort() currently report into the file-backed
    // values, writes server.ini, and clears the session layer on success - so
    // afterwards the "unsaved changes" marker correctly goes away, because the
    // values really are what is on disk now.
    //
    // This is the explicit opt-in behind the menu's Save Settings row. Typing an
    // address stays session-only (see setSessionOverride); it only reaches disk
    // when the player asks for it.
    //
    // Returns false if the file could not be written - worth surfacing rather
    // than assuming, since that write really can fail (a missing parent
    // directory silently defeated it before SDPaths::ensureRandomizerRoot()).
    bool commitSessionOverride();

    // The value as it came from server.ini / the compiled default, ignoring
    // any session override.
    const char* getFileIp();
    int getFilePort();

} // namespace ServerConfig
