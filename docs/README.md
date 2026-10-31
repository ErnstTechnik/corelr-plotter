# Documentation index

<img src="branding/machine-icon.svg" alt="" width="64" align="right">

| Folder | Contents |
|---|---|
| [`../requirements/`](../requirements/README.md) | what the system must do (`functional/`) and how it does it (`design/`), with the traceability rule |
| [`hardware/`](hardware/PinMapping.md) | pin allocation, bench wiring guide, geometry picture (`images/Geometry.svg`) |
| [`protocol/`](protocol/SerialProtocol.md) | the serial protocol (a grbl 1.1 subset, for Candle), the EEPROM layout, the brief for the host side |
| [`bringup/`](bringup/Bringup-Status.md) | the dated journal of what was verified on the machine and what was not, and the checklists |
| [`design-notes/`](design-notes/NextAlgorithm-StepDecoupling.md) | studies and experiments kept for the record (step decoupling, rejected patches) |
| [`standards/`](standards/README.md) | the reusable Arduino/embedded-C coding standards the code follows (project-agnostic copy) |

Everything is English Markdown, meant to be converted to HTML later. A result worth keeping goes into a requirement, with the command that produced it.
