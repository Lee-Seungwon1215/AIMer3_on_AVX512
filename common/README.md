# Common AIMer v3 material

This directory is the only source shared by the x86 and Cortex-M55 builds.

- `src/common/`: portable AES, SHAKE, RNG, and declassification helpers
- `src/sig/aimer/reference/aimer-*/`: six namespaced reference backends used
  by both hardware targets
- `KAT/`: official known-answer request and response files
- `upstream-reference/`: untouched official AIMer v3 reference snapshot

Hardware-specific intrinsics, assembly, dispatch code, platform startup, and
benchmark runners do not belong here. The two hardware directories may depend
on this directory; this directory must not depend on either hardware backend.
