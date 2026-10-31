/*
 * Version.h
 * Part of the CoreLR wall-plotter firmware project.
 *
 * Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file Version.h
 * @brief The software version, written here and nowhere else (@req{REQ-CoreLR-D-Build-0020}).
 *
 * Read by the firmware (`$I`), by CMake (the names of the release files, the documentation, `project()`), by the exported sketch and the IDE build (it is in `firmware/config/`), and by
 * `tools/analysis/check_version.py` (the release checks and the `release` workflow). The three numbers follow Semantic Versioning (CONTRIBUTING.md: a MAJOR is a recalibration, a change of
 * the serial protocol or of the wiring).
 *
 * Between two releases the suffix is `-dev`: a build of any branch is "the last release plus -dev", never a git hash (a hash in the image would make two builds of the same sources
 * differ, and the `.hex` files are compared byte for byte). The commit of a release empties the suffix and carries the tag `vMAJOR.MINOR.PATCH`; the next commit puts `-dev` back.
 */

#ifndef CORELR_VERSION_H
#define CORELR_VERSION_H

/**
 * @addtogroup CoreLR_Version
 * The only place the version is written. The lines of the three numbers and of the suffix keep this exact shape (`#define NAME value`, one per line): CMake and
 * `tools/analysis/check_version.py` read them as text.
 * @{
 */
#define CORELR_VERSION_MAJOR  1          /**< incremented by a recalibration, a change of the serial protocol or of the wiring */
#define CORELR_VERSION_MINOR  0          /**< incremented by a feature */
#define CORELR_VERSION_PATCH  0          /**< incremented by a fix */
#define CORELR_VERSION_SUFFIX ""         /**< "-dev" between two releases, "" on the commit that carries the tag */

/** Stringizing helper: expands its argument first, then quotes it. */
#define CORELR_VERSION_QUOTE_(x) #x
/** The number `x` as a string literal. */
#define CORELR_VERSION_QUOTE(x)  CORELR_VERSION_QUOTE_(x)

/** The version as one string literal: `<x.y.z>-dev`, or `<x.y.z>` on a release. */
#define CORELR_VERSION_STRING \
   CORELR_VERSION_QUOTE(CORELR_VERSION_MAJOR) "." CORELR_VERSION_QUOTE(CORELR_VERSION_MINOR) "." CORELR_VERSION_QUOTE(CORELR_VERSION_PATCH) CORELR_VERSION_SUFFIX
/** @} */

#endif /* CORELR_VERSION_H */
