# CyoHash

CyoHash is a simple Windows shell extension that can be used from within Windows Explorer to calculate the MD5 hash, SHA-1 hash, SHA-2 hash (SHA-256, SHA-384, SHA-512), or CRC32 checksum of a file.

CyoHash was originally created by **Graham Bull**. Beginning with the modernized 2.x development work, **J.S. Padilla** has continued development with an emphasis on updated Windows compatibility, usability, and optional digital-forensic reporting while retaining the original hashing functionality and licensing.

Current development: **J.S. Padilla**  
Project: https://github.com/JSPadilla

## Current Version

**CyoHash v2.6.0**

### v2.6.0 enhancements

- Main result and hash-detail window titles display the current CyoHash version.
- Added a conventional menu bar organized under File, Hash, View, and Help while retaining the right-click context menus.
- Result columns can be manually resized.
- Column widths are saved per user under `HKCU\Software\CyoHash` and restored on the next launch.
- `Reset Column Widths` restores the default File, Algorithm, and Hash widths.
- Completed results with the same **algorithm + hash value** but originating from different directory paths are visually highlighted.
- `Select Matching Hashes` selects all completed rows with the same algorithm + hash value and uses a dedicated highlight distinct from normal Windows selection.
- Multiple result rows can be selected for export and reporting.
- The completed-hash detail dialog is modeless, allowing multiple detail windows to remain open simultaneously.
- The About dialog identifies Graham Bull as the original creator and J.S. Padilla as the current developer.

### Forensic PDF reporting

CyoHash includes an optional forensic PDF report for one or multiple completed hash results. PDF generation is internal and does not require Microsoft Word, Adobe Acrobat, or another PDF application.

The report includes:

- Case Number/Name.
- Examiner.
- Optional Evidence/Item identifier.
- Source directory path(s).
- Hash Start Time and Hash End Time.
- Report Generated time.
- Hash algorithm(s).
- Number of results included.
- A landscape US Letter table containing File Name, File Size in bytes, Algorithm, and Hash Value.
- Automatic pagination with repeating table headings and `Page X of Y` numbering.
- User-configurable PDF table sorting by **File Name**, **File Size**, **Algorithm**, **Full Path Name**, and **Hash Value**.
- Matching PDF rows are color-coded by **algorithm + hash value**. Every duplicate-hash group receives its own pastel background color, and the same group keeps the same color across report pages.
- Source paths are listed immediately below **Report Generated** as numbered references (`Path 1`, `Path 2`, etc.). Each table row includes a compact **Path** indicator column containing the corresponding number.
- PDF table grid rules are redrawn after row highlighting with an explicit black stroke so highlighted rows retain consistent borders in Microsoft Edge and other PDF viewers.
- The report header keeps **Examiner** directly beneath **Case Number/Name** and omits the redundant hash-algorithm summary because the algorithm is shown per file in the table.
- Each PDF page includes a footer legend explaining that like-colored highlights identify matching hash values within the same algorithm.
- Up to five ordered sort levels can be combined, with independent **Ascending** or **Descending** direction for each level.

The PDF export details dialog includes a **PDF Table Sort Order** section. The first active sort level is the primary key, followed by the second, third, fourth, and fifth active levels as tie-breakers. Any unused level can be set to **None**. Sorting affects only the generated PDF and does not rearrange the main CyoHash results window. Duplicate active sort columns are rejected to avoid ambiguous/redundant sort definitions.

The PDF export details dialog also provides **Suppress user name in source path (%USERPROFILE%)**. When enabled, a displayed source path such as:

`C:\Users\John\Documents\Evidence`

is shown in the PDF as:

`%USERPROFILE%\Documents\Evidence`

This setting changes only the source path presented in the generated PDF. It does **not** modify the original pathname retained by CyoHash or the file used for hashing.

## Building

The original project was developed with Microsoft Visual Studio 2017. The current source has been retargeted for the modern Visual Studio toolchain and Windows SDK used by the v2.6.0 development build. The installer is built with NSIS.

### 1. Build the solution

Load `source\CyoHash.sln` in Visual Studio and build the desired configuration. The current release workflow uses **Release | x64** and builds the main application, shell extension, and installer plugin.

### 2. Build the installer

After building the solution, right-click `install\CyoHash.nsi` and select **Compile NSIS Script**, or open/drag the script into the NSIS compiler (MakeNSISW).

The current installer is x64 and produces a versioned CyoHash setup executable.

## Usage

After installing, access CyoHash by right-clicking a file in Windows Explorer and selecting a hash function from the CyoHash context menu. Completed results can be managed from the CyoHash results window using either the menu bar or the right-click context menu.

For forensic PDF output, select one or more completed hash results and choose **Export Forensic PDF**. Enter the report metadata, optionally suppress the Windows user-profile name from displayed source paths, configure the PDF table sort order if desired, and select the destination PDF file.

## Original Project Information

The original CyoHash documentation described the application as follows:

> CyoHash is a simple shell extension that is used from within Windows Explorer to calculate the MD5 hash, SHA-1 hash, SHA-2 hash (SHA-256, SHA-384, SHA-512), or CRC32 checksum of a file.

The original project used Microsoft Visual Studio 2017 and NSIS for its installer. The core Windows Explorer workflow and original hashing capabilities have been retained through the current development work.

## License

### Simplified BSD License

All the files in this library are covered under the terms of the Berkeley Software Distribution (BSD) License:

Copyright (c) Graham Bull. All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


### v2.6.0 refinement
- PDF Hash Start Time and Hash End Time values now use a common alignment position.
- Removed automatic yellow/info-background shading for cross-path duplicate hashes in the main results list; matching-hash emphasis is now only shown when explicitly selecting matching hashes.
- About dialog now links Graham Bull to the original project at https://github.com/calzakk/cyohash and identifies J.S. Padilla as Contributor.
