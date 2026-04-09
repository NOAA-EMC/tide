!> @file test_integration_output.F90
!> @brief Integration tests for the end-to-end output pipeline.
!>
!> Validates Requirements: 3.1, 3.2, 3.3, 4.1, 4.2, 4.3, 5.2, 5.3, 5.4, 5.5,
!>                         6.1, 6.2, 6.3, 6.4
!>
!> Tests the Output Manager integration:
!>   1. Init ESMF + PIO, create grid, call tide_output_init → file created
!>   2. tide_output_should_write with dt < frequency → false
!>   3. tide_output_should_write with dt >= frequency → true
!>   4. Write dummy data, finalize, reopen and verify:
!>      - Correct dimensions (lon=4, lat=3, time=unlimited)
!>      - Correct variables (lon, lat, time, field1, field2)
!>      - CF global attributes (Conventions, history, source)
!>      - Coordinate variable attributes (units, standard_name, axis)
program test_integration_output
  use ESMF
  use pio
  use tide_output_mod
  use shr_kind_mod, only : r8 => shr_kind_r8
  implicit none

  integer, parameter :: NLON = 4
  integer, parameter :: NLAT = 3
  integer, parameter :: NUM_FIELDS = 2
  integer, parameter :: OUTPUT_FREQ = 3600  ! 1 hour in seconds

  type(tide_output_state_t) :: state
  type(ESMF_Grid)           :: target_grid
  type(ESMF_VM)             :: vm
  type(iosystem_desc_t)     :: pio_sys
  type(file_desc_t)         :: check_file
  integer :: rc, test_rc, rcode
  integer :: num_pass, num_fail
  integer :: my_pet, pet_count, mpi_comm
  character(len=64)  :: field_names(NUM_FIELDS)
  character(len=256) :: out_file
  real(r8), allocatable :: field_data(:,:,:)
  logical :: should_write, file_exists

  ! PIO inquiry variables
  integer :: ndims, nvars, ngatts, unlimdimid
  integer :: lon_dimid, lat_dimid, time_dimid
  integer :: lon_varid, lat_varid, time_varid
  integer :: fld1_varid, fld2_varid
  integer :: dimlen

  num_pass = 0
  num_fail = 0
  out_file = "test_integration_output.nc"
  field_names(1) = "sst"
  field_names(2) = "ice_frac"

  ! ===================================================================
  ! Initialize ESMF
  ! ===================================================================
  call ESMF_Initialize(rc=rc)
  if (rc /= ESMF_SUCCESS) then
    print *, "FATAL: ESMF_Initialize failed"
    stop 1
  end if

  call ESMF_VMGetCurrent(vm, rc=rc)
  call ESMF_VMGet(vm, localPet=my_pet, petCount=pet_count, &
                  mpiCommunicator=mpi_comm, rc=rc)
  if (rc /= ESMF_SUCCESS) then
    print *, "FATAL: ESMF_VMGet failed"
    stop 1
  end if

  ! ===================================================================
  ! Initialize PIO
  ! ===================================================================
  call PIO_Init(my_pet, mpi_comm, pet_count, 0, 1, &
               PIO_REARR_BOX, pio_sys)

  ! ===================================================================
  ! Create a 4x3 rectilinear target grid with coordinates
  ! ===================================================================
  target_grid = ESMF_GridCreateNoPeriDim( &
    maxIndex=(/NLON, NLAT/), &
    coordSys=ESMF_COORDSYS_SPH_DEG, &
    rc=rc)
  if (rc /= ESMF_SUCCESS) then
    print *, "FATAL: ESMF_GridCreateNoPeriDim failed"
    stop 1
  end if

  call ESMF_GridAddCoord(target_grid, staggerloc=ESMF_STAGGERLOC_CENTER, rc=rc)
  if (rc /= ESMF_SUCCESS) then
    print *, "FATAL: ESMF_GridAddCoord failed"
    stop 1
  end if

  call fill_grid_coords(target_grid, NLON, NLAT, rc)
  if (rc /= ESMF_SUCCESS) then
    print *, "FATAL: fill_grid_coords failed"
    stop 1
  end if


  ! =====================================================================
  ! Test 1: tide_output_init creates the output file
  !   Validates: Req 5.2, 5.3, 5.4, 5.5
  ! =====================================================================
  call tide_output_init(state, pio_sys, trim(out_file), target_grid, &
                        field_names, NUM_FIELDS, OUTPUT_FREQ, test_rc)
  if (test_rc /= ESMF_SUCCESS) then
    print *, "FAIL: Test 1 - tide_output_init failed"
    num_fail = num_fail + 1
    goto 900
  else
    print *, "PASS: Test 1 - tide_output_init succeeded"
    num_pass = num_pass + 1
  end if

  ! =====================================================================
  ! Test 2: tide_output_should_write with dt < frequency → false
  !   Validates: Req 6.3
  ! =====================================================================
  should_write = tide_output_should_write(state, 1800)  ! 30 min < 1 hour
  if (.not. should_write) then
    print *, "PASS: Test 2 - should_write returns false when dt < frequency"
    num_pass = num_pass + 1
  else
    print *, "FAIL: Test 2 - should_write should be false for dt < frequency"
    num_fail = num_fail + 1
  end if

  ! =====================================================================
  ! Test 3: tide_output_should_write with cumulative dt >= frequency → true
  !   Validates: Req 6.2
  ! =====================================================================
  ! Already accumulated 1800s from Test 2, add another 1800s → 3600s total
  should_write = tide_output_should_write(state, 1800)
  if (should_write) then
    print *, "PASS: Test 3 - should_write returns true when cumulative dt >= frequency"
    num_pass = num_pass + 1
  else
    print *, "FAIL: Test 3 - should_write should be true when cumulative dt >= frequency"
    num_fail = num_fail + 1
  end if

  ! =====================================================================
  ! Test 4: Write dummy data via tide_output_write
  !   Validates: Req 6.1, 6.2
  ! =====================================================================
  allocate(field_data(NLON, NLAT, NUM_FIELDS))
  field_data(:,:,1) = 300.0_r8   ! SST-like values
  field_data(:,:,2) = 0.5_r8     ! Ice fraction-like values

  call tide_output_write(state, field_data, 3600.0_r8, test_rc)
  if (test_rc /= ESMF_SUCCESS) then
    print *, "FAIL: Test 4 - tide_output_write failed"
    num_fail = num_fail + 1
  else
    print *, "PASS: Test 4 - tide_output_write succeeded"
    num_pass = num_pass + 1
  end if
  deallocate(field_data)

  ! =====================================================================
  ! Test 5: tide_output_finalize closes the file
  !   Validates: Req 6.4
  ! =====================================================================
  call tide_output_finalize(state, test_rc)
  if (test_rc /= ESMF_SUCCESS) then
    print *, "FAIL: Test 5 - tide_output_finalize failed"
    num_fail = num_fail + 1
    goto 900
  else
    print *, "PASS: Test 5 - tide_output_finalize succeeded"
    num_pass = num_pass + 1
  end if


  ! =====================================================================
  ! Test 6: Verify output file exists on disk
  ! =====================================================================
  inquire(file=trim(out_file), exist=file_exists)
  if (file_exists) then
    print *, "PASS: Test 6 - output file exists after finalize"
    num_pass = num_pass + 1
  else
    print *, "FAIL: Test 6 - output file does not exist after finalize"
    num_fail = num_fail + 1
    goto 900
  end if

  ! =====================================================================
  ! Reopen the file with PIO for verification
  ! =====================================================================
  rcode = pio_openfile(pio_sys, check_file, PIO_IOTYPE_NETCDF, &
                       trim(out_file), PIO_NOWRITE)
  if (rcode /= PIO_NOERR) then
    print *, "FAIL: Cannot reopen output file for verification"
    num_fail = num_fail + 1
    goto 900
  end if

  ! Set PIO error handling to return errors instead of aborting
  call pio_seterrorhandling(check_file, PIO_BCAST_ERROR)

  ! =====================================================================
  ! Test 7: Verify correct number of dimensions (lon, lat, time = 3)
  !   Validates: Req 5.2
  ! =====================================================================
  rcode = pio_inquire(check_file, nDimensions=ndims, nVariables=nvars, &
                      nAttributes=ngatts, unlimitedDimId=unlimdimid)
  if (rcode /= PIO_NOERR) then
    print *, "FAIL: Test 7 - pio_inquire failed"
    num_fail = num_fail + 1
    goto 800
  end if

  if (ndims == 3) then
    print *, "PASS: Test 7 - correct number of dimensions (3)"
    num_pass = num_pass + 1
  else
    print *, "FAIL: Test 7 - expected 3 dimensions, got", ndims
    num_fail = num_fail + 1
  end if

  ! =====================================================================
  ! Test 8: Verify correct number of variables (lon, lat, time + 2 fields = 5)
  !   Validates: Req 5.3
  ! =====================================================================
  if (nvars == 5) then
    print *, "PASS: Test 8 - correct number of variables (5)"
    num_pass = num_pass + 1
  else
    print *, "FAIL: Test 8 - expected 5 variables, got", nvars
    num_fail = num_fail + 1
  end if

  ! =====================================================================
  ! Test 9: Verify lon dimension size = NLON
  !   Validates: Req 5.2
  ! =====================================================================
  rcode = pio_inq_dimid(check_file, 'lon', lon_dimid)
  if (rcode /= PIO_NOERR) then
    print *, "FAIL: Test 9 - lon dimension not found"
    num_fail = num_fail + 1
  else
    rcode = pio_inq_dimlen(check_file, lon_dimid, dimlen)
    if (dimlen == NLON) then
      print *, "PASS: Test 9 - lon dimension size correct (", NLON, ")"
      num_pass = num_pass + 1
    else
      print *, "FAIL: Test 9 - lon dimension expected", NLON, "got", dimlen
      num_fail = num_fail + 1
    end if
  end if

  ! =====================================================================
  ! Test 10: Verify lat dimension size = NLAT
  !   Validates: Req 5.2
  ! =====================================================================
  rcode = pio_inq_dimid(check_file, 'lat', lat_dimid)
  if (rcode /= PIO_NOERR) then
    print *, "FAIL: Test 10 - lat dimension not found"
    num_fail = num_fail + 1
  else
    rcode = pio_inq_dimlen(check_file, lat_dimid, dimlen)
    if (dimlen == NLAT) then
      print *, "PASS: Test 10 - lat dimension size correct (", NLAT, ")"
      num_pass = num_pass + 1
    else
      print *, "FAIL: Test 10 - lat dimension expected", NLAT, "got", dimlen
      num_fail = num_fail + 1
    end if
  end if

  ! =====================================================================
  ! Test 11: Verify time dimension exists and is unlimited
  !   Validates: Req 5.2
  ! =====================================================================
  rcode = pio_inq_dimid(check_file, 'time', time_dimid)
  if (rcode /= PIO_NOERR) then
    print *, "FAIL: Test 11 - time dimension not found"
    num_fail = num_fail + 1
  else
    if (time_dimid == unlimdimid) then
      print *, "PASS: Test 11 - time dimension is unlimited"
      num_pass = num_pass + 1
    else
      print *, "FAIL: Test 11 - time dimension is not the unlimited dimension"
      num_fail = num_fail + 1
    end if
  end if


  ! =====================================================================
  ! Test 12: Verify data variables exist (sst, ice_frac)
  !   Validates: Req 5.3
  ! =====================================================================
  rcode = pio_inq_varid(check_file, 'sst', fld1_varid)
  if (rcode == PIO_NOERR) then
    print *, "PASS: Test 12a - 'sst' variable found"
    num_pass = num_pass + 1
  else
    print *, "FAIL: Test 12a - 'sst' variable not found"
    num_fail = num_fail + 1
  end if

  rcode = pio_inq_varid(check_file, 'ice_frac', fld2_varid)
  if (rcode == PIO_NOERR) then
    print *, "PASS: Test 12b - 'ice_frac' variable found"
    num_pass = num_pass + 1
  else
    print *, "FAIL: Test 12b - 'ice_frac' variable not found"
    num_fail = num_fail + 1
  end if

  ! =====================================================================
  ! Test 13: Verify CF global attribute: Conventions = "CF-1.8"
  !   Validates: Req 5.4
  ! =====================================================================
  call check_global_att(check_file, 'Conventions', 'CF-1.8', 13, num_pass, num_fail)

  ! =====================================================================
  ! Test 14: Verify CF global attribute: history
  !   Validates: Req 5.4
  ! =====================================================================
  call check_global_att(check_file, 'history', 'Created by TIDE', 14, num_pass, num_fail)

  ! =====================================================================
  ! Test 15: Verify CF global attribute: source = "TIDE"
  !   Validates: Req 5.4
  ! =====================================================================
  call check_global_att(check_file, 'source', 'TIDE', 15, num_pass, num_fail)

  ! =====================================================================
  ! Test 16: Verify lon coordinate variable attributes
  !   Validates: Req 5.5
  ! =====================================================================
  rcode = pio_inq_varid(check_file, 'lon', lon_varid)
  if (rcode == PIO_NOERR) then
    call check_var_att(check_file, lon_varid, 'units', 'degrees_east', '16a', num_pass, num_fail)
    call check_var_att(check_file, lon_varid, 'standard_name', 'longitude', '16b', num_pass, num_fail)
    call check_var_att(check_file, lon_varid, 'axis', 'X', '16c', num_pass, num_fail)
  else
    print *, "FAIL: Test 16 - lon variable not found for attribute check"
    num_fail = num_fail + 3
  end if

  ! =====================================================================
  ! Test 17: Verify lat coordinate variable attributes
  !   Validates: Req 5.5
  ! =====================================================================
  rcode = pio_inq_varid(check_file, 'lat', lat_varid)
  if (rcode == PIO_NOERR) then
    call check_var_att(check_file, lat_varid, 'units', 'degrees_north', '17a', num_pass, num_fail)
    call check_var_att(check_file, lat_varid, 'standard_name', 'latitude', '17b', num_pass, num_fail)
    call check_var_att(check_file, lat_varid, 'axis', 'Y', '17c', num_pass, num_fail)
  else
    print *, "FAIL: Test 17 - lat variable not found for attribute check"
    num_fail = num_fail + 3
  end if

  ! =====================================================================
  ! Test 18: Verify time coordinate variable has axis="T"
  !   Validates: Req 5.5
  ! =====================================================================
  rcode = pio_inq_varid(check_file, 'time', time_varid)
  if (rcode == PIO_NOERR) then
    call check_var_att(check_file, time_varid, 'axis', 'T', '18', num_pass, num_fail)
  else
    print *, "FAIL: Test 18 - time variable not found for attribute check"
    num_fail = num_fail + 1
  end if

  ! =====================================================================
  ! Test 19: Verify time dimension has 1 record (we wrote once)
  !   Validates: Req 5.2, 6.2
  ! =====================================================================
  rcode = pio_inq_dimlen(check_file, time_dimid, dimlen)
  if (rcode == PIO_NOERR .and. dimlen == 1) then
    print *, "PASS: Test 19 - time dimension has 1 record after 1 write"
    num_pass = num_pass + 1
  else
    print *, "FAIL: Test 19 - expected 1 time record, got", dimlen
    num_fail = num_fail + 1
  end if

800 continue
  call pio_closefile(check_file)

  ! Clean up temp file
  if (my_pet == 0) then
    open(unit=78, file=trim(out_file), status='old', iostat=rc)
    if (rc == 0) close(78, status='delete')
  end if

900 continue

  ! ===================================================================
  ! Summary
  ! ===================================================================
  print *, "====================================="
  print *, "Integration output tests: ", num_pass, " passed, ", num_fail, " failed"
  print *, "====================================="

  ! ===================================================================
  ! Cleanup
  ! ===================================================================
  call ESMF_GridDestroy(target_grid, rc=rc)
  call pio_finalize(pio_sys, rc)
  call ESMF_Finalize(rc=rc)

  if (num_fail > 0) stop 1

contains


  !> Fill a rectilinear grid with evenly spaced lon/lat coordinates.
  subroutine fill_grid_coords(grid, nx, ny, rc)
    type(ESMF_Grid), intent(inout) :: grid
    integer,         intent(in)    :: nx, ny
    integer,         intent(out)   :: rc

    real(ESMF_KIND_R8), pointer :: coordX(:,:), coordY(:,:)
    integer :: i, j, lbnd(2), ubnd(2)
    real(ESMF_KIND_R8) :: dlon, dlat

    rc = ESMF_SUCCESS

    call ESMF_GridGetCoord(grid, coordDim=1, localDe=0, &
                           staggerloc=ESMF_STAGGERLOC_CENTER, &
                           computationalLBound=lbnd, computationalUBound=ubnd, &
                           farrayPtr=coordX, rc=rc)
    if (rc /= ESMF_SUCCESS) return

    call ESMF_GridGetCoord(grid, coordDim=2, localDe=0, &
                           staggerloc=ESMF_STAGGERLOC_CENTER, &
                           farrayPtr=coordY, rc=rc)
    if (rc /= ESMF_SUCCESS) return

    dlon = 360.0_ESMF_KIND_R8 / real(nx, ESMF_KIND_R8)
    dlat = 180.0_ESMF_KIND_R8 / real(ny, ESMF_KIND_R8)

    do j = lbnd(2), ubnd(2)
      do i = lbnd(1), ubnd(1)
        coordX(i, j) = (real(i, ESMF_KIND_R8) - 0.5_ESMF_KIND_R8) * dlon
        coordY(i, j) = -90.0_ESMF_KIND_R8 + (real(j, ESMF_KIND_R8) - 0.5_ESMF_KIND_R8) * dlat
      end do
    end do

  end subroutine fill_grid_coords

  !> Check a global text attribute against an expected value.
  subroutine check_global_att(pfile, att_name, expected, test_num, npass, nfail)
    type(file_desc_t), intent(inout) :: pfile
    character(len=*),  intent(in)    :: att_name, expected
    integer,           intent(in)    :: test_num
    integer,           intent(inout) :: npass, nfail

    character(len=256) :: val
    integer :: rcode

    rcode = pio_inq_att(pfile, PIO_GLOBAL, trim(att_name))
    if (rcode /= PIO_NOERR) then
      write(*,'(a,i0,a,a,a)') "FAIL: Test ", test_num, &
        " - global attribute '", trim(att_name), "' not found"
      nfail = nfail + 1
      return
    end if

    val = ' '
    rcode = pio_get_att(pfile, PIO_GLOBAL, trim(att_name), val)
    if (rcode /= PIO_NOERR) then
      write(*,'(a,i0,a,a,a)') "FAIL: Test ", test_num, &
        " - cannot read global attribute '", trim(att_name), "'"
      nfail = nfail + 1
      return
    end if

    if (trim(val) == trim(expected)) then
      write(*,'(a,i0,a,a,a,a,a)') "PASS: Test ", test_num, &
        " - global attribute '", trim(att_name), "' = '", trim(expected), "'"
      npass = npass + 1
    else
      write(*,'(a,i0,a,a,a,a,a,a,a)') "FAIL: Test ", test_num, &
        " - global attribute '", trim(att_name), "' expected '", &
        trim(expected), "' got '", trim(val), "'"
      nfail = nfail + 1
    end if
  end subroutine check_global_att

  !> Check a variable text attribute against an expected value.
  subroutine check_var_att(pfile, varid, att_name, expected, test_label, npass, nfail)
    type(file_desc_t), intent(inout) :: pfile
    integer,           intent(in)    :: varid
    character(len=*),  intent(in)    :: att_name, expected, test_label
    integer,           intent(inout) :: npass, nfail

    character(len=256) :: val
    integer :: rcode

    rcode = pio_inq_att(pfile, varid, trim(att_name))
    if (rcode /= PIO_NOERR) then
      write(*,'(a,a,a,a,a)') "FAIL: Test ", trim(test_label), &
        " - attribute '", trim(att_name), "' not found"
      nfail = nfail + 1
      return
    end if

    val = ' '
    rcode = pio_get_att(pfile, varid, trim(att_name), val)
    if (rcode /= PIO_NOERR) then
      write(*,'(a,a,a,a,a)') "FAIL: Test ", trim(test_label), &
        " - cannot read attribute '", trim(att_name), "'"
      nfail = nfail + 1
      return
    end if

    if (trim(val) == trim(expected)) then
      write(*,'(a,a,a,a,a,a,a)') "PASS: Test ", trim(test_label), &
        " - attribute '", trim(att_name), "' = '", trim(expected), "'"
      npass = npass + 1
    else
      write(*,'(a,a,a,a,a,a,a,a,a)') "FAIL: Test ", trim(test_label), &
        " - attribute '", trim(att_name), "' expected '", &
        trim(expected), "' got '", trim(val), "'"
      nfail = nfail + 1
    end if
  end subroutine check_var_att

end program test_integration_output
