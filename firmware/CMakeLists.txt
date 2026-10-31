# Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
# SPDX-License-Identifier: GPL-3.0-or-later
# The two firmware images. Each is a complete build of core + sketch + vendored libraries (cmake/Firmware.cmake).
corelr_add_variant(serial SERIAL)
corelr_add_variant(sd     SD)
