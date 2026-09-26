# Nervous Distribution

## 40. Distribution

Local and remote messaging share the same syntax and semantics:

```text
pid <- message;
```

Wire representations must not expose local atom IDs, pointers, scheduler identity, or native struct layout.

Rolling upgrades are made practical by keeping dispatch exact (Section 4) and pushing payload evolution into map/record field extraction (Section 7); see "Decoding Is Not Dispatch" there for the full argument.

---

## 41. Process Migration Across Machines

Network process migration is not a v1 promise.

A suspended process is largely language-controlled state and may eventually be serializable.

External resources such as sockets, file descriptors, and OS handles are non-migratable capabilities unless explicitly recreated, renegotiated, delegated, or abandoned.

Stable PID identity across migration may eventually require forwarding or routing semantics.
