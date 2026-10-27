# FL Studio load/close crash investigation — V1.3

## Observed
FL Studio Fruity Wrapper reports the plugin crashed while loading its settings, and the plugin also crashes when closing. The root cause is **not confirmed** without a debugger stack trace.

## Defensive changes
- Avoid unconditional dereference of `getConstrainer()` during editor construction.
- Stop editor timer, disconnect graph callback and destroy APVTS attachments early during teardown.
- Reject malformed, empty, or oversized state data before APVTS replaceState.
- Optional minimal diagnostic editor to isolate UI lifecycle from DSP/state issues.

## Isolation procedure
1. Back up the FL Studio project and restart FL Studio; do not save a project after the reported plugin crash.
2. Test in a *new empty* FL Studio project first. Remove old VST3 versions from other scan paths; rescan the new version.
3. Build normal plugin and test: (a) add plugin (b) close its window (c) reopen it (d) remove plugin from mixer.
4. If it still crashes, configure CMake with `-DSKELLY_DIAGNOSTIC_EDITOR=ON`, build and test the same actions. Do not use that diagnostic build for production.
5. If minimal editor works, crash is in GUI construction, paint, timers, or destruction. If it also crashes, focus on `setStateInformation`, processor lifetime, VST3 wrapper or DSP; capture a dump and stack.
6. For state-only reproduction, test opening a saved FLP in a *copy* of the project after confirming blank project works.

## Debug stack
Use Visual Studio > Debug > Attach to Process > FL64.exe (Native), break on thrown Win32 access violation 0xC0000005, reproduce, then copy Call Stack + exception address; alternatively collect the FL Studio crash dump. Avoid sending full project files with personal data.

## Build
`cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`
`cmake --build build --config Release`
Diagnostic: `cmake -S . -B build-diagnostic -DSKELLY_DIAGNOSTIC_EDITOR=ON`
`cmake --build build-diagnostic --config Release`

## Validation
Source-level changes only. Windows VST3 / FL Studio runtime crash resolution is not yet verified.
