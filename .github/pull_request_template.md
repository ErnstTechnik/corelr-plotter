## What and why
<!-- The Conventional Commit line is the PR title. Here: the reason, in a few sentences. -->

## Requirements and documentation
- [ ] Requirement(s) realised or amended: <!-- REQ-CoreLR-F-… / REQ-CoreLR-D-… , or why there is none -->
- [ ] Docs and requirements updated in this PR
- [ ] `CHANGELOG.md` `[Unreleased]` updated
- [ ] `BREAKING CHANGE:` footer if the EEPROM format, the serial protocol, the units or the wiring change

## Measurements (required for any change to `firmware/`)
| Variant | Flash before → after | RAM before → after | Worst stack before → after |
|---|---|---|---|
| serial | | | |
| sd | | | |

## Verification
- [ ] `tests/host/run_all.sh CORELR_MATH_48` and `CORELR_MATH_64` pass
- [ ] New or changed tests were put through `tools/mutate.py` (batch: )
- [ ] **On the machine:** verified / **not verified** (say which)
