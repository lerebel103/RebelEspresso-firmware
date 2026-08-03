@@
 ## Architecture
@@
 - Hardware revision 2 is the active target (`HW_REVISION=2`).
+
+### Required Hardware Validation
+
+Changes that affect safety-critical real-time behavior must be validated on physical hardware
+before being considered ready to merge. This includes, but is not limited to:
+- heater / SSR control
+- relay and solenoid actuation
+- power state transitions
+- boiler refill logic
+- RTD / sensor acquisition
+- remote-vs-physical command precedence
+- timing, latency, and watchdog behavior
+
+Where a behavior cannot be fully automated in CI or unit tests, it must be verified manually
+on a physical test board and recorded in:
+
+`docs/testing/manual-hardware-validation.md`
+
+This validation step is required for firmware evolutions that can affect safety, timing, or
+machine state. If hardware validation cannot be completed, the limitation must be called out
+explicitly in the PR description or audit notes.
*** End Patch