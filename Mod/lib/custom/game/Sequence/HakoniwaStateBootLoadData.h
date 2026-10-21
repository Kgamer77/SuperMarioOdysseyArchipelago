#pragma once

// Only the exported control entry point is needed by the Nuzlocke sequence
// bridge.  The object is constructed and owned by HakoniwaSequence.
class HakoniwaStateBootLoadData {
public:
    void startLoad();
};
