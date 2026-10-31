# DocUnavailable.cmake -- what the target `doc` runs when Doxygen was not found at configure time (cmake/Doxygen.cmake).
#
# Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
# SPDX-License-Identifier: GPL-3.0-or-later
message(FATAL_ERROR
    "The target `doc` needs Doxygen, which was not found when this build was configured.\n"
    "Install it (Debian/Ubuntu: sudo apt install doxygen graphviz; graphviz is optional, for the include graphs), "
    "then configure again: cmake --preset <preset>. Nothing else in the build depends on it.")
