# BookOrbit reading statistics (fork build)

Build with `pio run -e x4pro_bookorbit`. This extends `x4pro` and enables
`CROSSPOINT_ENABLE_BOOKORBIT_STATS`; ordinary firmware environments do not record
or upload statistics. No statistics screen is added to the reader.

Configure KOReader sync with your BookOrbit KOReader credentials and a custom
server URL ending in `/api/v1/koreader`, for example
`http://books.example.ts.net:6060/api/v1/koreader`. Use the actual port of your
deployment. The existing Tailscale transport supports HTTP MagicDNS URLs. The
statistics adapter reuses that connection and authentication. Other KOSync
server URLs do not activate the adapter.

## Recording and upload

- EPUB only. Reading intervals begin after a clean page is displayed. Menus,
  child activities, rendering, and sleep pause recording.
- Page dwell shorter than five seconds is discarded. An unchanged page counts
  at most two minutes until another page is shown or reading explicitly resumes.
  These are dwell measurements, not proof that a person was reading.
- Durations use a monotonic timer. Dates use validated UTC from the X4 Pro RTC;
  sync attempts NTP refresh when needed. BookOrbit applies the user's timezone.
- Checkpoints occur every 30 seconds and on orderly transitions. Abrupt power
  loss can lose the current uncheckpointed interval. No Wi-Fi starts while reading.
- Invoking the existing KOReader sync uploads queued stats independently of
  whether progress is pushed, pulled, or already equal. Upload work is bounded;
  repeat sync when a large backlog remains. A stats error does not fail progress
  sync.
- The adapter sends content hashes and logical progress units from 0 to 10,000.
  BookOrbit derives percentages from that ratio. These units are not physical
  pages or KOReader's layout page numbers. Unmatched books remain queued.
- Progress and stats use the same stable per-device ID on BookOrbit. Other
  KOSync servers retain the existing CrossPoint identity. Previously recorded
  estimates under the old shared device ID are not migrated automatically.

The server must support the BookOrbit plugin `page-stats` and `sweeps` endpoints.
For installations that estimate reading time from ordinary KOSync pushes, verify
that the deployed version replaces overlapping estimates when a sweep completes
(BookOrbit PR #1140). The local client tests cannot establish your server version
or this server-side reconciliation behavior.

## Persistence and recovery

Files live under `/.crosspoint/reading-stats/<scope>/`. The scope hashes the
server URL, username, and device identity. Switching accounts or servers never
uploads another scope's queue. Returning to that configuration resumes it.

Each `.bin` file is an immutable, versioned batch of at most eight events with a
CRC32. Writes are closed and read back before publishing by rename. A file is
removed only after the server acknowledges every event, including duplicates.
If a response is lost or power fails during acknowledgment handling, replaying
the same events is safe. `.tmp` files are incomplete unpublished writes.

A `.clk` file stores an immutable boot-to-UTC anchor. An event recorded before
time was available can be dated after time becomes available in the same boot.
If that boot ends without a valid anchor, its events remain undated and are not
uploaded with an invented date. Corrupt and undated batches remain on SD for
inspection; they do not prevent eligible batches from uploading.

`sweep.pending` persists completion work across reboot, even after the last
acknowledged batch was removed. A failed completion request is retried on the
next sync.

The queue is limited to 2,048 published batch files per scope. At one checkpoint
file every 30 seconds this is roughly 17 hours of continuously counted reading;
shorter page visits and pauses change that figure. Sync regularly. If full or
the SD card fails, old records are preserved, but further recording can stop.
Clock anchors and incomplete temporary files are separate from that limit.
Do not delete the queue while it contains reading you want to upload.

## Verification

1. Use a book already matched in BookOrbit. Read for several minutes, visit a
   menu, resume, then sync. Check BookOrbit's sessions and daily reading time;
   menu and sleep time should not be included.
2. Sync again. Totals should not increase for acknowledged events.
3. Read offline, sleep/restart, reconnect, and sync. Check the original reading
   date rather than the upload date, including a session near local midnight.
4. Interrupt a connection and retry. Confirm no double counting and that an
   unmatched book remains queued.
5. Check serial logs for `BOSTATS`, RTC validity, and SD/network errors. Compare
   free heap before reading, after several page turns, and after sync; verify on
   hardware that RTC time survives normal sleep/restart.

Host tests cover timing, bounded payloads, acknowledgments, and outbox damage.
The final firmware builds and the steps above are both needed before relying on
the integration for historical statistics.
