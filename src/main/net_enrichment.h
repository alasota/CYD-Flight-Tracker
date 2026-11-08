// net_enrichment — the lookups poll_pipeline uses on the board: the hexdb.io
// caches and the NVS copy behind EnrichmentPort, memoised per poll so phase B
// doesn't repeat (or re-log) what phase A already looked up.
//
// Device-only (net_task): aircraft_lookup, route_lookup and lookup_store hold
// the logic.
#pragma once

#include "airspace/services/poll_pipeline.h"  // EnrichmentPort

// The ports for one poll; call enrichmentClearMemos() before and after it.
EnrichmentPort productionPorts();
void enrichmentClearMemos();
