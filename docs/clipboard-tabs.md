# Clipboard tabs, first version

## Use

- Open Ditto with its normal activation shortcut: the History tab is selected.
- Use + to create a named tab or pin an existing group. Tabs reuse group IDs and the existing clip records.
- Right-click a custom tab to rename it, move it left/right or unpin it. Unpinning keeps its group and contents.
- Right-click selected clips > Add to tab, or press Ctrl+M. A configured existing Ctrl+M action takes priority.
- Drag one or more selected clips onto a custom tab. The destination highlights while hovering; release to assign the clips and show that tab. Escape cancels. History, the + button, the current custom tab and external drags are not destinations. Groups in a mixed selection are ignored. Dragging out to another application retains the existing paste behavior; internal assignment does not count as a paste.
- The destination menu includes New tab, which creates a tab and assigns the selection.
- A clip belongs to one custom group and also remains in History. Adding it to another tab changes its group; it does not clone its payload.
- Added clips are marked Never Auto Delete. Normal clipboard capture continues to use the existing copy pipeline; selecting a tab does not route new copies into it. Existing explicit copy-to-group shortcuts still work.
- Ctrl+Tab and Ctrl+Shift+Tab cycle tabs. The switch is enabled by default in Options > Quick Paste. Disabling it restores the previous Ctrl+Tab focus behavior.
- Right-click one clip > Edit note. Notes appear before the original preview and are included in searches. Notes are not pasted. Emptying a note removes it.
- New controls have Simplified Chinese translations when that language pack is selected.

## Wallpaper

Options > General > Wallpaper and mask selects a local PNG/JPG/BMP image and enables or disables it. The black mask defaults to 85% and accepts 75-100%. The list centers and crops the image to cover its viewport. Unselected text and row numbers are white; search matches are pale yellow. Selected rows retain the opaque theme highlight. Wallpaper rows use plain-text previews so pasted rich-text colors cannot disappear into the picture; original clipboard payloads are unchanged. Tabs and search controls retain their theme backgrounds. Whole-window transparency is temporarily suppressed while a wallpaper is loaded, preserving text contrast; its saved preference remains available when wallpaper is disabled.

Images must be readable, at most 32 MB and 64 megapixels. A missing or unreadable image falls back to the theme. The path and enable/mask preferences use Ditto's existing settings store. The original image is not copied into the clipboard database; keep it at the selected path. The decoded source is limited to 1024 pixels on its long edge and the composed viewport to 2048. No new service or timer is added, and both image caches are released when the popup hides. Actual working-set impact remains to be measured on a running build.

## Storage and resource usage

The existing Main table gains two additive columns: clipNote (text, empty by default) and quickPasteTabOrder (integer, zero by default). The latter is used on group rows only. Upgrade checks column existence and can resume after a partial upgrade. A partial index contains pinned groups only. No existing payload or description is rewritten by the upgrade.

All tabs share the original list and its existing asynchronous row cache. There are no new services, polling timers, worker threads, thumbnail caches or copies of clipboard payloads. Only pinned group IDs/names and notes for loaded rows are held in memory. Native tab controls and notes have a small additional memory cost; the amount requires a running build to measure.

A full database backup contains the new metadata. Existing individual clip import/export formats have not been extended to carry notes or tab pin order. Explicit group-opening shortcuts and the existing open-by-active-application option retain their behavior.

## Validation

Run `python -m unittest discover -s tests -v`. The tests execute the production SQL against Ditto's original schema, covering upgrades, unchanged payloads, complete history, reassignment, note Unicode/quotes, rank swaps, unpinning, rollback and the partial index.

Build the original solution with a matching Visual C++ toolset, MFC and NuGet dependencies. The tab validation workflow uses the installed v143 toolset on a Windows runner without changing the upstream project toolset.

Native acceptance checks still required after a successful build:

1. Create two tabs, including a Chinese name and an ampersand. Switch with mouse and both shortcuts; close and reopen normally to confirm History.
2. Disable/re-enable tab shortcuts. Check the existing focus action when disabled and a user-configured Ctrl+M action when present.
3. Add multiple clips through the context menu and Ctrl+M, including creation of a new destination. Reassign and verify a single record remains in History.
4. While viewing a custom tab, copy fresh text from another application and verify it is captured in History without automatic categorization.
5. Rename, reorder, unpin, restart and repin. Confirm group contents survive.
6. Add, search, change and clear a note. Double-click/Enter into an external text field; only the original payload should be pasted.
7. Test narrow windows, overflowing tab names, 100/150/200% DPI and the existing list/search/group controls.
8. Compare private bytes and working set at idle and after cycling tabs with the same database. No native measurements have been recorded yet.
9. Drag one/multiple clips from History and between custom tabs; confirm highlight, destination contents, notes, one payload per clip and Never Auto Delete. Cancel with Escape or release over History, +, a blank area or the current tab: contents stay unchanged. Groups alone cannot be dropped; mixed selections assign only clips. External drags onto tabs are rejected. Check overflowing tabs, DPI scaling and repeated open/close cycles, and confirm normal external drag paste still works and internal drops do not increment paste statistics.
10. Select a bright, busy wallpaper; test masks 75/85/100%, selected/unselected rows, notes, search matches and rich-text/image clips. Resize, scroll by mouse/keyboard and switch tabs; wallpaper should remain fixed to the viewport. Test missing/corrupt images, disabling wallpaper, reopening, whole-window transparency and Cancel in both dialogs. Verify original pasted formats are unchanged and image caches are released on hide.
