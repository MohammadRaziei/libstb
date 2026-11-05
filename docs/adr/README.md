# Architecture decision records

One short file per decision that is costly to reverse or easy to forget the reason for. A record is
never rewritten when the decision changes: a new one is added and the old one is marked
*Superseded by ADR-nnnn*.

| ADR | Decision | Status |
| --- | --- | --- |
| [0001](0001-own-logging-module-shaped-like-pythons.md) | libstb has its own small logging module, shaped like Python's `logging` | Accepted |
| [0002](0002-python-logging-bridge.md) | In Python, libstb's records go to the `logging` module through a bridge installed at import | Accepted |
| [0003](0003-logging-policy.md) | What libstb logs, at which level, and what it never logs | Accepted |
