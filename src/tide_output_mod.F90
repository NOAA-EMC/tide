!> @file tide_output_mod.F90
!> @brief Output Manager module for the TIDE library.
!>
!> Provides PIO-based NetCDF output: file creation with CF-compliant metadata,
!> time-record writing, frequency gating, and resource cleanup.
module tide_output_mod
  use ESMF
  use pio
  use shr_kind_mod,  only : r8 => shr_kind_r8, cl => shr_kind_cl
  use shr_const_mod, only : shr_const_spval
  implicit none
  private

  ! -------------------------------------------------------------------------
  ! Public types
  ! -------------------------------------------------------------------------

  !> @brief Metadata for a single output variable in the NetCDF file.
  type, public :: tide_output_field_t
    character(len=256) :: field_name
    integer            :: pio_varid
  end type tide_output_field_t

  !> @brief State for one output stream (one per output-enabled TIDE stream).
  type, public :: tide_output_state_t
    type(file_desc_t)                          :: pio_file
    type(io_desc_t)                            :: io_desc
    type(tide_output_field_t), allocatable     :: fields(:)
    integer                                    :: num_fields
    integer                                    :: time_varid
    integer                                    :: time_record      ! Current time record index
    integer                                    :: output_frequency  ! Write interval in seconds
    integer                                    :: elapsed_seconds   ! Seconds since last write
    logical                                    :: initialized = .false.
  end type tide_output_state_t

  ! -------------------------------------------------------------------------
  ! Public interface
  ! -------------------------------------------------------------------------
  public :: tide_output_init
  public :: tide_output_write
  public :: tide_output_should_write
  public :: tide_output_finalize

contains

  ! ===========================================================================
  ! Initialisation — create NetCDF file, define dims/vars/attrs
  ! ===========================================================================

  !> @brief Create a CF-compliant NetCDF output file via PIO.
  !>
  !> Defines dimensions (lat, lon, time unlimited), coordinate variables with
  !> CF attributes, data variables for each output field, and global attributes.
  !> After define mode, writes coordinate variable data from the target grid.
  !>
  !> @param state          Output state to initialise.
  !> @param pio_subsystem  PIO I/O system descriptor.
  !> @param output_file    Path to the NetCDF file to create.
  !> @param target_grid    ESMF Grid describing the target coordinate system.
  !> @param field_names    Array of field name strings to define as variables.
  !> @param num_fields     Number of output fields.
  !> @param output_frequency  Write interval in seconds.
  !> @param rc             ESMF_SUCCESS or ESMF_FAILURE.
  subroutine tide_output_init(state, pio_subsystem, output_file, target_grid, &
                              field_names, num_fields, output_frequency, rc)
    type(tide_output_state_t), intent(inout) :: state
    type(iosystem_desc_t),     intent(inout) :: pio_subsystem
    character(len=*),          intent(in)    :: output_file
    type(ESMF_Grid),           intent(in)    :: target_grid
    character(len=*),          intent(in)    :: field_names(:)
    integer,                   intent(in)    :: num_fields
    integer,                   intent(in)    :: output_frequency
    integer,                   intent(out)   :: rc

    integer :: rcode, i
    integer :: lat_dimid, lon_dimid, time_dimid
    integer :: lat_varid, lon_varid
    integer :: nlat, nlon
    integer :: lbnd(2), ubnd(2), grid_rc
    real(ESMF_KIND_R8), pointer :: coordX(:,:), coordY(:,:)
    real(r8), allocatable :: lat_vals(:), lon_vals(:)
    type(var_desc_t) :: pio_var
    integer :: dimids(3)

    rc = ESMF_SUCCESS
    state%initialized = .false.

    ! --- Extract grid dimensions from the target grid ---
    call ESMF_GridGet(target_grid, localDe=0, &
                      staggerloc=ESMF_STAGGERLOC_CENTER, &
                      computationalLBound=lbnd, computationalUBound=ubnd, &
                      rc=grid_rc)
    if (grid_rc /= ESMF_SUCCESS) then
      write(*,*) "ERROR: [TIDE] Failed to query target grid dimensions for output"
      rc = ESMF_FAILURE
      return
    end if

    nlon = ubnd(1) - lbnd(1) + 1
    nlat = ubnd(2) - lbnd(2) + 1

    ! --- Get coordinate arrays from the target grid ---
    nullify(coordX, coordY)
    call ESMF_GridGetCoord(target_grid, coordDim=1, localDe=0, &
                           staggerloc=ESMF_STAGGERLOC_CENTER, &
                           farrayPtr=coordX, rc=grid_rc)
    if (grid_rc /= ESMF_SUCCESS .or. .not. associated(coordX)) then
      write(*,*) "ERROR: [TIDE] Failed to get longitude coordinates from target grid"
      rc = ESMF_FAILURE
      return
    end if

    call ESMF_GridGetCoord(target_grid, coordDim=2, localDe=0, &
                           staggerloc=ESMF_STAGGERLOC_CENTER, &
                           farrayPtr=coordY, rc=grid_rc)
    if (grid_rc /= ESMF_SUCCESS .or. .not. associated(coordY)) then
      write(*,*) "ERROR: [TIDE] Failed to get latitude coordinates from target grid"
      rc = ESMF_FAILURE
      return
    end if

    ! Extract 1-D coordinate vectors (first row for lon, first column for lat)
    allocate(lon_vals(nlon), lat_vals(nlat))
    lon_vals(:) = coordX(:, lbnd(2))
    lat_vals(:) = coordY(lbnd(1), :)

    ! --- Create the NetCDF file via PIO ---
    rcode = pio_createfile(pio_subsystem, state%pio_file, PIO_IOTYPE_NETCDF, &
                           trim(output_file), PIO_CLOBBER)
    if (rcode /= PIO_NOERR) then
      write(*,*) "ERROR: [TIDE] Failed to create output file: ", trim(output_file)
      deallocate(lon_vals, lat_vals)
      rc = ESMF_FAILURE
      return
    end if

    ! --- Define dimensions ---
    rcode = pio_def_dim(state%pio_file, 'lon', nlon, lon_dimid)
    rcode = pio_def_dim(state%pio_file, 'lat', nlat, lat_dimid)
    rcode = pio_def_dim(state%pio_file, 'time', PIO_UNLIMITED, time_dimid)

    ! --- Define coordinate variables with CF attributes ---

    ! lon(lon)
    rcode = pio_def_var(state%pio_file, 'lon', PIO_DOUBLE, (/lon_dimid/), pio_var)
    lon_varid = pio_var%varid
    rcode = pio_put_att(state%pio_file, lon_varid, 'units', 'degrees_east')
    rcode = pio_put_att(state%pio_file, lon_varid, 'standard_name', 'longitude')
    rcode = pio_put_att(state%pio_file, lon_varid, 'axis', 'X')

    ! lat(lat)
    rcode = pio_def_var(state%pio_file, 'lat', PIO_DOUBLE, (/lat_dimid/), pio_var)
    lat_varid = pio_var%varid
    rcode = pio_put_att(state%pio_file, lat_varid, 'units', 'degrees_north')
    rcode = pio_put_att(state%pio_file, lat_varid, 'standard_name', 'latitude')
    rcode = pio_put_att(state%pio_file, lat_varid, 'axis', 'Y')

    ! time(time)
    rcode = pio_def_var(state%pio_file, 'time', PIO_DOUBLE, (/time_dimid/), pio_var)
    state%time_varid = pio_var%varid
    rcode = pio_put_att(state%pio_file, state%time_varid, 'units', 'seconds since 0001-01-01 00:00:00')
    rcode = pio_put_att(state%pio_file, state%time_varid, 'calendar', 'noleap')
    rcode = pio_put_att(state%pio_file, state%time_varid, 'axis', 'T')

    ! --- Define data variables: field(lon, lat, time) ---
    dimids = (/lon_dimid, lat_dimid, time_dimid/)
    state%num_fields = num_fields
    allocate(state%fields(num_fields))

    do i = 1, num_fields
      state%fields(i)%field_name = trim(field_names(i))
      rcode = pio_def_var(state%pio_file, trim(field_names(i)), PIO_DOUBLE, dimids, pio_var)
      state%fields(i)%pio_varid = pio_var%varid
      rcode = pio_put_att(state%pio_file, state%fields(i)%pio_varid, '_FillValue', shr_const_spval)
      rcode = pio_put_att(state%pio_file, state%fields(i)%pio_varid, 'coordinates', 'lon lat')
    end do

    ! --- Write global attributes ---
    rcode = pio_put_att(state%pio_file, PIO_GLOBAL, 'Conventions', 'CF-1.8')
    rcode = pio_put_att(state%pio_file, PIO_GLOBAL, 'history', 'Created by TIDE')
    rcode = pio_put_att(state%pio_file, PIO_GLOBAL, 'source', 'TIDE')

    ! --- End define mode ---
    rcode = pio_enddef(state%pio_file)

    ! --- Write coordinate variable data ---
    rcode = pio_put_var(state%pio_file, lon_varid, lon_vals)
    rcode = pio_put_var(state%pio_file, lat_varid, lat_vals)

    deallocate(lon_vals, lat_vals)

    ! --- Initialise time tracking ---
    state%time_record = 0
    state%output_frequency = output_frequency
    state%elapsed_seconds = 0
    state%initialized = .true.

    write(*,*) "INFO: [TIDE] Output file created: ", trim(output_file)
    write(*,*) "INFO: [TIDE] Output dimensions: lon=", nlon, " lat=", nlat
    write(*,*) "INFO: [TIDE] Output fields:", num_fields, " frequency=", output_frequency, "s"

  end subroutine tide_output_init

  ! ===========================================================================
  ! Frequency gating
  ! ===========================================================================

  !> @brief Check whether the output frequency interval has elapsed.
  !>
  !> Accumulates dt_seconds into elapsed_seconds. Returns .true. when the
  !> cumulative elapsed time >= output_frequency, and resets the counter.
  !>
  !> @param state       Output state with frequency tracking.
  !> @param dt_seconds  Timestep size in seconds.
  !> @return .true. if a write should occur this step.
  logical function tide_output_should_write(state, dt_seconds)
    type(tide_output_state_t), intent(inout) :: state
    integer,                   intent(in)    :: dt_seconds

    state%elapsed_seconds = state%elapsed_seconds + dt_seconds

    if (state%elapsed_seconds >= state%output_frequency) then
      tide_output_should_write = .true.
      state%elapsed_seconds = state%elapsed_seconds - state%output_frequency
    else
      tide_output_should_write = .false.
    end if
  end function tide_output_should_write

  ! ===========================================================================
  ! Write one time record
  ! ===========================================================================

  !> @brief Write regridded field data as a new time record for all fields.
  !>
  !> Increments the time record counter, writes the model time value to the
  !> time coordinate variable, and writes each field's 2-D data slice.
  !>
  !> @param state       Initialised output state.
  !> @param field_data  Array of 2-D field data arrays (nlon x nlat), one per field.
  !> @param model_time  Model time value in seconds for the time coordinate.
  !> @param rc          ESMF_SUCCESS or ESMF_FAILURE.
  subroutine tide_output_write(state, field_data, model_time, rc)
    type(tide_output_state_t), intent(inout) :: state
    real(r8),                  intent(in)    :: field_data(:,:,:)  ! (nlon, nlat, num_fields)
    real(r8),                  intent(in)    :: model_time
    integer,                   intent(out)   :: rc

    integer :: rcode, i
    real(r8) :: time_val(1)

    rc = ESMF_SUCCESS

    if (.not. state%initialized) then
      write(*,*) "ERROR: [TIDE] tide_output_write called on uninitialised state"
      rc = ESMF_FAILURE
      return
    end if

    ! Increment time record
    state%time_record = state%time_record + 1

    ! Write time coordinate value using var_desc_t for PIO generic interface
    time_val(1) = model_time
    block
      type(var_desc_t) :: time_vdesc
      type(var_desc_t) :: fld_vdesc
      integer :: start1(1), count1(1)
      integer :: start3(3), count3(3)
      integer :: nx, ny

      time_vdesc%varid = state%time_varid
      start1(1) = state%time_record
      count1(1) = 1
      rcode = pio_put_var(state%pio_file, time_vdesc, start1, count1, time_val)

      ! Write each field's data slice for this time record
      nx = size(field_data, 1)
      ny = size(field_data, 2)
      start3(1) = 1
      start3(2) = 1
      start3(3) = state%time_record
      count3(1) = nx
      count3(2) = ny
      count3(3) = 1
      do i = 1, state%num_fields
        fld_vdesc%varid = state%fields(i)%pio_varid
        rcode = pio_put_var(state%pio_file, fld_vdesc, start3, count3, &
                            reshape(field_data(:, :, i), (/nx * ny/)))
      end do
    end block

  end subroutine tide_output_write

  ! ===========================================================================
  ! Finalisation
  ! ===========================================================================

  !> @brief Flush pending writes and close the PIO output file.
  !>
  !> @param state  Output state to finalise.
  !> @param rc     ESMF_SUCCESS or ESMF_FAILURE.
  subroutine tide_output_finalize(state, rc)
    type(tide_output_state_t), intent(inout) :: state
    integer,                   intent(out)   :: rc

    rc = ESMF_SUCCESS

    if (.not. state%initialized) then
      write(*,*) "WARNING: [TIDE] tide_output_finalize called on uninitialised state, skipping"
      return
    end if

    call pio_syncfile(state%pio_file)
    call pio_closefile(state%pio_file)

    if (allocated(state%fields)) then
      deallocate(state%fields)
    end if

    state%initialized = .false.
    write(*,*) "INFO: [TIDE] Output file closed"

  end subroutine tide_output_finalize

end module tide_output_mod
