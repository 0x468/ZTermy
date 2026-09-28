# Windows heap-lifetime checks

Heob 4.2 is a developer-only, external detector. It is not linked, embedded,
started or distributed with ztermy. Use isolated test data; reports include
process paths and allocation stacks and must not be uploaded automatically.

## Reproduce

In the MSVC developer shell, with the configured Qt dynamic installation,
Python 3 and 7-Zip (needed only for first installation):

```powershell
cmake --build --preset msvc-dynamic-debug --target ztermy ztermy_leak_detector_probe ztermy_qt_heap_lifetime_probe ztermy_local_terminal_session_tests ztermy_ai_conversation_model_tests
python scripts/test_heob_summary.py
pwsh -File scripts/run_heob_checks.ps1 -InstallHeob -IncludeGui
```

`-Python` can select an explicit interpreter. `-BuildDirectory` can select a
symbol-bearing dynamic build. Do not rebuild that executable while the detector
is running. The runner times out and terminates only its own process tree;
interrupted/incomplete reports are invalid, never clean. A GUI report can take
minutes to classify and symbolize after the visible test has finished.

Pinned download: https://github.com/ssbssa/heob/releases/tag/4.2

Archive SHA-256:
`b8dbdccfa87b6745feb325d301b4ed98d860e821b8fd8158837420936de36225`.
Installation downloads into ignored `build/tools`, verifies the archive before
extraction, and does not change PATH or machine settings. The tested executable
has a valid SignPath Foundation signature. The per-run manifest records its
binary hash, build directory and Git revision.

## Interpretation

- First run a matched clean/intentional-4096-byte-leak calibration. A successful
  positive control must also resolve the source filename; otherwise stop.
- Track `DefinitelyLost`, `IndirectlyLost`, `PossiblyLost` and `StillReachable`
  separately. Unknown error categories and invalid reads also require review.
  The summarizer streams XML rather than loading a large QML report tree at once.
- `-p0` avoids page-per-allocation guard overhead. This is a lifetime check, not
  a use-after-free/overflow gate or a performance benchmark. `-L0` disables heap
  contents. No global injection, persistent verifier flags or child injection.
- Small probes retain reachable-stack output (`-l3`). GUI runs classify leaks
  but omit reachable-stack output (`-l2`) to reduce report size. Absence of that
  category in GUI summaries does **not** mean zero reachable memory.
- CRT heap hooks do not account for all VirtualAlloc/custom allocator arenas,
  GPU resources, handles or every possible application workflow. Passing is a
  bounded observation, not proof that the entire product is leak-free.
- QML/Qt process-global roots and shutdown order require controls. A third-party
  frame alone is not justification to suppress a report. Keep raw diagnostics
  and compare fixed overhead with growth under repeat counts.

## Scenarios

1. Matched clean and deliberate leak calibration.
2. Bare QCoreApplication baseline, and Qt-only logging repeated 1/30 times.
3. Production SVG image provider with all 79 icons, unique colors and 1/10/30
   cycles. Verify the real 4 MiB cache cap and destroy the provider before exit.
4. Queued callbacks delivered and canceled by receiver destruction, 100 cycles.
   Weak references verify captured payload destruction as well as heap reporting.
5. Local terminal CPR, repeated start/stop and output coalescing fixtures.
6. AI model streaming, image-payload retention limits, text limits and cancel.
7. Optional real QML Profile picker and shared toolbar interaction smokes.
   Their shutdown uses the same `releaseQmlResources()` path as normal exit.
   The picker also repeats 10/30 dark/light open/select/reset cycles in one process.
   Runs use `QT_HASH_SEED=0` locally to control hash-table capacity variance;
   normal application environment/settings are untouched.

Raw reports/manifests stay under `build/memory/heob-*`; no generated binaries,
large reports or user data belong in commits. The runner completes measurements
even when reports need review and prints that status; it does not auto-suppress
Qt findings or advertise a green no-leak result.

## 2026-09-29 measurements

Calibrated Debug probes found a fixed Qt logging baseline, not per-cycle icon
growth. All selected workload assertions passed under the detector:

| Scenario | Definitely lost | Indirectly lost | Reachable |
| --- | ---: | ---: | ---: |
| Bare Qt | 0 B | 0 B | 768 B |
| Qt logging, 1 / 30 cycles | 192 B | 768 B | 2,256 B |
| Icons, 1 / 10 / 30 cycles | 160 B | 768 B | 2,720 B |
| Callback delivery/cancel, 100 cycles | 0 B | 0 B | 4,864 B |
| Local terminal lifecycle | 192 B | 768 B | 3,568–3,616 B |
| AI model lifecycle | 0 B | 0 B | 2,672 B |

Logging stacks identify `QLoggingRegistry` hash storage and `QMessagePattern`,
also reproduced without ztermy components. These fixed exit reports remain
visible in the results. The icon cache peaks at 655 40x40 ARGB images, within
4 MiB; all captured callback payloads expire. This does not assess all long-lived
Qt/driver retention or real AI provider/attachment workloads.

The callback probe also reproduces clang-tidy's Qt 6.8.3 `qobjectdefs.h:624`
`NewDeleteLeaks` diagnostic, but all delivered/canceled payload weak references
expire and Heob reports zero lost callback blocks. Together with the Qt source's
`SlotObjUniquePtr` adoption this supports the narrowly documented static-analysis
false-positive assessment. No product lifetime or global checker was disabled.

### Real QML repeat comparison

Controlled run: `build/memory/heob-9a2a43a78e6444da8270878aa5368c2b`.
Every report reached `FINISHED`; the clean control reported no losses and the
positive control resolved exactly one 4,096-byte allocation to its source file.

| Profile picker cycles | Definitely lost bytes / blocks | Indirectly lost bytes / blocks | Live content-tree objects after each cycle |
| --- | ---: | ---: | ---: |
| 1 | 503,008 / 317 | 478,816 / 7,718 | 16,564 |
| 10 | 503,584 / 317 | 473,648 / 7,721 | 16,564 |
| 30 | 503,584 / 317 | 476,640 / 7,722 | 16,564 |

Toolbar hover/press checks also passed under Heob (502,176 directly and 477,472
indirectly lost bytes at exit). The repeated picker results do not show sustained
growth proportional to operations; they are **not zero-loss reports**. A stable
object count alone would not establish the absence of other leaks.

Stack review identifies QML property/type-registration metadata as the dominant
exit allocation group, with smaller logging, pixmap/texture, metatype, animation
and native accessibility registries. Unresolved platform frames were additionally
symbolized against `qwindowsd.pdb`, including `QWindowsUiaMainProvider` hash storage.
These are retained as reviewed exit findings, not blanket third-party suppressions.
Qt documents application-global QML registrations and requires all engines to be
destroyed before [clearing registrations](https://doc.qt.io/qt-6/qqml-h.html#qmlClearTypeRegistrations);
no forced runtime GC or registration clearing was added to lower these numbers.

An earlier GUI fixture only released scene-graph resources. Matching its teardown
to ordinary application exit reduced its report from roughly 3 MiB to 0.94 MiB.
This corrects a **test fixture**, not a demonstrated product memory saving. An
earlier 120-second report timeout was rejected as incomplete and is excluded.

The summary parser's five checks cover unfinished reports, distinct reachable/lost
totals, invalid reads, unknown categories and a complete empty report. They passed,
as did focused icon/profile/workspace/native-window regression checks (4/4).
The preceding icon milestone passed the complete 127-test CTest suite in each of
dynamic Debug, dynamic Release and static Release; optional external-host/soak
workloads remain conditional and are not claimed as exercised here.

Conclusion: no operation-proportional business-object leak was demonstrated in
these bounded workloads. Fixed Qt exit findings remain nonzero. Real provider
traffic, full AI attachment rendering, GPU allocations and long-duration usage
are outside this result; this is not evidence that every memory allocation in
the product is necessary.

References: [Qt's Heob workflow](https://doc.qt.io/qtcreator/creator-how-to-use-heob.html),
[Heob source and options](https://github.com/ssbssa/heob).
