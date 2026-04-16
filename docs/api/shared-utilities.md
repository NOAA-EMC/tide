# Shared Utilities

The shared utility modules in `src/share/` provide fundamental types, constants, calendar
utilities, and string helpers used throughout the TIDE library and its dependencies.

## shr_kind_mod

Precision and kind constants module. Defines standard Fortran kind parameters for real
(`SHR_KIND_R8`, `SHR_KIND_R4`), integer (`SHR_KIND_I8`, `SHR_KIND_I4`), and character
(`SHR_KIND_CS`, `SHR_KIND_CL`, `SHR_KIND_CX`) types used across all TIDE modules.

::: doxy.tide.Class
    name: shr_kind_mod

## shr_cal_mod

Calendar utility module. Provides routines for converting between elapsed days and
calendar dates (year, month, day), coded calendar dates (`yyyymmdd`), and computing
the number of days between arbitrary dates.

::: doxy.tide.Class
    name: shr_cal_mod

## shr_const_mod

Physical constants module. Defines double-precision parameters for commonly used
physical and mathematical constants such as pi, Earth radius, gravity, and time
conversion factors used in scientific computations.

::: doxy.tide.Class
    name: shr_const_mod

## shr_string_mod

String and list manipulation module. Provides general string utilities and
delimiter-based list operations for parsing, splitting, and manipulating
colon-separated field lists used in stream configuration.

::: doxy.tide.Class
    name: shr_string_mod
