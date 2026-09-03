# X4 Pro Tailscale BookOrbit build

The `x4pro_tailscale` environment adds an experimental on-demand Tailscale
transport for OPDS and KOReader sync servers addressed by a direct
`100.64.0.0/10` tailnet IP or fully qualified MagicDNS name.
It uses the independent [MicroLink](https://github.com/CamM2325/microlink)
client and is not affiliated with or supported by Tailscale.

## Configure

Create a one-off, non-ephemeral Tailscale auth key restricted to an `x4pro`
tag. A durable node is important because the reader spends most of its time
offline. Pass the key only to the first Tailscale build, then flash the
resulting enrollment binary:

```sh
git submodule update --init --recursive
CROSSPOINT_TAILSCALE_AUTH_KEY='tskey-auth-REPLACE_ME' pio run -e x4pro_tailscale
```

Open a tailnet OPDS catalog once to enroll the reader and persist its node
identity in NVS. Then build and flash the same environment **without** the key
and revoke the enrollment key:

```sh
pio run -e x4pro_tailscale
```

Do not distribute or keep using the enrollment binary. Erasing the reader's
NVS removes its node identity and requires repeating enrollment.

Configure the CrossPoint OPDS server with the BookOrbit host's fully qualified
MagicDNS name:

```text
http://olympus.tailNNNNNN.ts.net
```

A direct tailnet IPv4 URL such as `http://100.x.y.z:3000/api/v1/opds` also
works. Short MagicDNS names, subnet routes, and HTTPS MagicDNS URLs are not
supported. Append the OPDS path and port when the BookOrbit deployment does
not expose its catalog at the HTTP root.

Use a dedicated BookOrbit OPDS username and password. The HTTP request travels
inside WireGuard; the firmware establishes the peer tunnel before sending OPDS
credentials. Restrict the tagged X4 Pro node to the BookOrbit host and port in
the tailnet policy.

To use BookOrbit's KOReader progress sync, configure its fully qualified
MagicDNS HTTP URL as the custom sync server. Authentication, account creation,
progress download, and progress upload use the same on-demand tunnel.

Tailscale starts only while the OPDS browser or KOReader sync activity is
active and is stopped before CrossPoint tears down Wi-Fi. It cannot receive
traffic while the reader sleeps.

## Hardware validation plan

1. Confirm BookOrbit is reachable from another tailnet client at its fully
   qualified MagicDNS URL, and restrict the `x4pro` tag to that host and TCP
   port in the tailnet policy.
2. Connect the X4 Pro over USB. Build and upload the enrollment image with a
   new one-off tagged key, without saving the key in a project file:

   ```sh
   CROSSPOINT_TAILSCALE_AUTH_KEY='tskey-auth-REPLACE_ME' \
     pio run -e x4pro_tailscale -t upload
   pio device monitor -b 115200
   ```

3. Join Wi-Fi, configure the MagicDNS OPDS URL and dedicated BookOrbit
   credentials, then open the catalog. Confirm the serial log reports tailnet
   connection, hostname resolution, and a WireGuard peer tunnel without resets,
   watchdog warnings, or allocation failures.
4. Browse multiple catalog pages, run a search, and download a large EPUB.
   Open the downloaded book to verify the streamed file is complete.
5. Configure the same MagicDNS host as the custom KOReader sync server. Verify
   account authentication, progress download, and progress upload from a book.
6. Leave the OPDS browser and KOReader sync screen. Confirm MicroLink stops,
   Wi-Fi shuts down, and the reader can sleep normally.
7. Build and upload a keyless image, revoke the enrollment key, and power-cycle
   the reader. Reopen BookOrbit and confirm the NVS-backed node identity
   reconnects without reenrollment.
8. Repeat catalog, download, and progress-sync tests on a second Wi-Fi network
   and after an interrupted connection. Confirm recovery does not create
   duplicate tailnet nodes or require a factory reset.
9. Verify the tailnet policy denies the reader access to an unrelated peer and
   port. Remove the enrollment binary after validation.

Validation passes when feed navigation, search, EPUB download, and KOReader
progress sync work after a keyless cold boot; failures recover cleanly;
unrelated tailnet access is denied; and leaving either network activity restores
the normal sleep behavior.

## Current constraints

- X4 Pro only.
- Direct tailnet IPv4 peers and fully qualified `.ts.net` MagicDNS names only;
  subnet routes and short MagicDNS names are not enabled.
- MagicDNS URLs currently require plain HTTP.
- Hardware validation is required before treating this build as reliable.
- The enrollment build compiles the auth key into its firmware image. Replace
  it with a keyless build and revoke the key after enrollment succeeds.
