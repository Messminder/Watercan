# Windows packaging — under redesign

The Windows packaging documentation and helper scripts have been reset to make way for a focused effort to produce a single standalone Windows executable (one `.exe` file) that embeds required runtime assets and data.

This document has been intentionally cleared. A new, detailed plan and step-by-step instructions will be added once the approach is chosen and prototyped.

If you want to resume immediately, I can:
- Prototype a MinGW cross-build that embeds assets and the JSON data into the executable, or
- Prepare CI/VM steps for building on native Windows and embedding assets.

Tell me which approach you prefer and I'll proceed with a concrete plan.
