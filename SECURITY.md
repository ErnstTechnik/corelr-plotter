# Security and safety policy

This firmware drives motors on a machine that holds a cable-suspended carriage. Two kinds of defect deserve a private report rather than a public issue:
- **a way to make the machine move, or lose its position, that the user did not ask for** (a malformed serial line, a crafted GCode file, a reset that
  corrupts the calibration);
- **a way to overrun memory** (the 2 KB of RAM leaves very little margin; a stack or buffer overflow is a safety issue here).

Report it through GitHub's private vulnerability reporting for this repository (Security → *Report a vulnerability*). Include the variant (`sd` or `serial`),
the version or commit, and the bytes or file that trigger it. Expect an acknowledgement within a week.

Supported versions: the latest release only.

Never test an unverified firmware or GCode file on a machine with a pen on the wall or a person within reach of the carriage.
