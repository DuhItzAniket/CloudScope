# Ph28: QML Frontend + Live Pipeline — DONE

- `AppController` full: worker QThread (capture→classify+segment→render→publish),
  drop-oldest semantics via sequential worker, HUD props (label/conf/fps/device/
  objects/maskOk), overlay mode + conf threshold, snapshot, 100-line log.
- `Main.qml`: dark theme, live view (`image://frames/live` + tick refresh),
  source row (field + probe + Start/Stop), overlay radios (Rect/Polygon/Symmetry),
  conf slider (Qt6 `stepSize` — fixed after qmltest caught `step` error),
  Now-grid, log pane, version footer.
- `--qmltest` (offscreen): **UI loaded OK**. Remaining messages are offscreen
  NativeStyle warnings only (resolve on real display).
- `main.cpp`: GUI entry + retained `--camtest`/`--infertest` CLI; model paths
  overridable; QML warnings hook kept for field debugging.
- Honest note: live loop thread code paths reuse verified infer/render fns;
  on-screen run left for morning acceptance (Ph33) — headless cannot click Start.
