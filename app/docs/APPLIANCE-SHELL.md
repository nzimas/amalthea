# Appliance shell and parameter randomization — 0.19.0

The current executable contains the appliance router and Tracker. Future appliances
can be routed from the home grid; the FX tile currently opens a clearly labelled
development preview and does not claim USB audio processing support. No kernel,
bootloader or stock executable is changed. The existing boot helper displays the
new `assets/identity/ready.png`; the native renderer then displays the same identity
and the home grid. Enter retains physical code315.

FUNC-down followed by Shift-down, with both held, opens Cancel/OK. Repeated key-down
messages cannot reopen it; the reverse modifier order cannot trigger it. OK opens
No/Yes save choice. Only explicit Yes schedules the normal atomic session-bank
save. The current active slot is used, falling back to the selected slot when there
is no active session. A failed save keeps the workspace open. No discards it and
returns home. Tracker re-entry starts blank and stopped, with global device
preferences preserved. Home/other tracker shortcuts cannot bypass exit prompts.

Event X taps modify only the selected tab's parameter in the pending event. The
pending random seed commits with Enter and is discarded with Cancel. Shift+hold X
changes only that parameter over the active column length, retaining other fields,
other columns, other tracks and hidden rows. The bulk action is a single checkpoint.
Its current-row result is copied into the pending event without disturbing other
pending fields. Notes retain existing onset/rest structure in bulk operation and
obey the chosen scale/root, including at the lower pitch boundary. Volume has a10%
floor. Condition choices remain positive occurrence/cycle pairs. FX randomization
on its value tab retains the insert type and changes amount only.

Validation: all28 tests pass on Mac and ARM Linux. The new appliance/randomization
suite also passes on the Brick and under AddressSanitizer. Hardware UI verification
covered Enter into Tracker, F2 to Pan, tap X, Shift+hold X, FUNC→Shift, OK, No and the
return to home. Boot PNG display was verified through the real pic2fb helper.
The device binary and boot image checksums match their local release artifacts.
Existing live-bank files were neither saved nor loaded during QA; their checksums
remain unchanged. Save-before-exit was exercised using a separate temporary fixture.
