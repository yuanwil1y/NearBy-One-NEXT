# Agent D frozen host fixture

These headers are **test-only** narrow snapshots of the public symbols Agent B's coordinator consumes from Agent D. They are not production replacements for D.

Authoritative source commit:

```text
accdf91441d0abaf6a5d69aac31394465ab49b06
```

Mirrored public contracts:

```text
firmware/components/scan_session/include/scan_session.h
firmware/components/radio_runtime/include/radio_runtime.h
```

Only the declarations required by the host coordinator test are reproduced. Production firmware must compile against D's real headers and components.
