# Data Stream Handlers

The data stream handler modules manage the interface between ESMF data streams and
the TIDE component model. They provide initialization, mesh setup, clock management,
field mapping between streams and export states, and field list advertisement and
realization for NUOPC-compliant components.

## dshr_mod

Top-level data stream handler module. Provides component initialization
(`dshr_model_initphase`, `dshr_init`), mesh creation and masking (`dshr_mesh_init`),
run clock synchronization (`dshr_set_runclock`), restart read/write, orbital parameter
management, alarm initialization, and scalar state accessors for NUOPC data model
components.

::: doxy.tide.Class
    name: dshr_mod

## dshr_dfield_mod

Data field mapping module. Defines the `dfield_type` linked list for associating
export state field pointers with stream field bundle indices. Provides `dshr_dfield_add`
(with 1-D and 2-D overloads) to register field mappings and `dshr_dfield_copy` to
populate export state arrays from stream data at each time step.

::: doxy.tide.Class
    name: dshr_dfield_mod

## dshr_fldlist_mod

Field list advertisement and realization module. Defines the `fldlist_type` linked list
for tracking advertised field names and optional ungridded dimension bounds. Provides
`dshr_fldlist_add` to register fields and `dshr_fldlist_realize` to create ESMF fields
on a mesh and realize them into a NUOPC state, handling both scalar and gridded fields.

::: doxy.tide.Class
    name: dshr_fldlist_mod
