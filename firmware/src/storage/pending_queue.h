// Local queue of judging records awaiting sync to home base (PROTOCOL.md
// POST /sync/submissions). Not implemented yet — this is a later phase,
// once the judging UI and WiFi sync exist. Placeholder so the directory
// structure matches the project layout now.
#pragma once

namespace storage {
// TODO(sync phase): queued-submission persistence (append-on-close,
// mark-sent-on-ack, survive a crash/battery pull per CONTEXT.md's
// resilience principle) goes here.
}  // namespace storage
