// SPDX-License-Identifier: MIT
// The OQS KAT build force-includes src/oqs/kat_compat.h, which defines API_H
// before the official driver includes this path. Standalone builds find their
// parameter-specific api.h first and never use this shim.

#ifndef API_H
#error "src/api.h is only a guarded include shim for the OQS KAT build"
#endif
