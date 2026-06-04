# tests

Headless behavioral tests for tcxArtnet, run automatically in CI. **You don't
need to run this by hand** — it's a console program that asserts the addon's
behavior (ArtDmx / ArtSync / ArtPoll wire format, channel & universe edge cases,
the universe cap, and a Sender↔Receiver loopback round-trip) and exits non-zero
on failure.

CI (`TrussC-org/ci-actions`) builds and runs it on macOS / Windows / Linux.
To run it locally anyway:

```bash
trusscli update
trusscli run
```
