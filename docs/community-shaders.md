# Community shaders: download, prepare and contribute

[Português brasileiro](community-shaders.pt-BR.md) · [Español](community-shaders.es.md)

**Available in [release 0.4.20](https://github.com/xPXavierx/gow3-macos-arm64/releases/tag/v0.4.20).**
Older downloads may not have the **Community shaders** button. The official
catalogue and GitHub contribution validator are online.

**Play → discover combinations → contribute → review → everyone receives an update.**

The shared catalogue contains descriptions for rebuilding shaders and graphics
pipelines. Each player's Mac uses its own compatible game dump to compile native
GPU results. You do not download another player's compiled GPU cache.

## Download and prepare — no GitHub account required

1. Open the launcher, select your own compatible extracted dump and click **Play**.
2. Before the first Play in a launcher session, it checks for official updates,
   at most once a day. It downloads and verifies the signed catalogue when available.
3. Before gameplay, the Mac prepares the compatible, reconstructible combinations
   from the catalogue using your dump. Wait for preparation to finish.
4. Prepared results stay in the local cache for reuse. Existing compatible results
   are reused; an update does not mean recompiling everything from scratch.

For a manual check, choose **Community shaders → Update official catalogue**.
**This downloads and verifies only. Click Play afterward to prepare it.**
Downloads do not require a GitHub account. If the service is unavailable, the game
can use bundled descriptions, valid local data and its existing cache. Keep your
dump accessible; a catalogue does not replace the game files.

## Contribute — GitHub account required

1. Play normally. The game records new combinations automatically.
2. Exit the game and return to the launcher. It indicates newly discovered entries
   that are not already in the bundled or downloaded official catalogue.
3. Open **Community shaders → Contribute on GitHub** and confirm **Continue on GitHub**.
4. Finder selects a `GOW3-<digest>.txt` file. Your browser opens the official form.
5. Sign in to GitHub, drag **that selected file** into the attachment field and
   click **Create** to submit the issue. Optionally name the areas you played.

**Opening the browser does not submit anything.** Attaching the file and submitting
the form are required. No Terminal, cache-folder search or shader selection is
needed. If there are no new discoveries, keep playing other areas; no submission
is needed. The app does not know whether you completed the browser submission.

Only descriptions are included: no game dump, saves, logs or compiled GPU programs.
Do not attach those files yourself. **Export discoveries** under **Advanced** is
for manual diagnostics; it does not upload anything or replace the contribution flow.

## What happens after submission?

The GitHub validator checks the attachment as bounded data. A successful check
means **ready for review**, not accepted, compiled or published. Maintainers review
and test the new combinations, remove duplicates and publish a signed catalogue.
Players receive that approved update through the launcher. Nothing from an issue
is automatically installed on other players' Macs.

The catalogue combines approved discoveries from any campaign area. It does not
yet cover the entire game or guarantee zero stutter. Invalid or incomplete entries
cannot be counted as prepared; previously unseen combinations can still require
compilation. Required draws wait rather than being omitted.

**Clear shader cache** discards local compiled graphics results. It preserves
saves, settings, ARM64 game preparation and downloaded official catalogue data;
playing afterward rebuilds the graphics cache. It is not needed for normal updates.

For validation, signatures and maintainer publication steps, see the
[technical service guide](community-catalogue-service.md).
