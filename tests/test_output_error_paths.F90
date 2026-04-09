!> @file test_output_error_paths.F90
!> @brief Unit tests for Output Manager error paths.
!>
!> Validates Requirements 5.7 and 5.8:
!>   5.7 - tide_output_finalize flushes pending writes and closes the file
!>   5.8 - tide_output_init with invalid path returns error
program test_output_error_paths
  use ESMF
  use pio
  use tide_output_mod
  use shr_kind_mod, only : r8 => shr_kind_r8
  implicit none

  type(tide_output_state_t) :: state
  type(ESMF_Grid)           :: target_grid
  type(ESMF_VM)             :: vm
  type(iosystem_desc_t)     :: pio_sys
  integer :: rc, test_rc, rcode
  integer :: num_pass, num_fail
  integer :: my_pet, pet_count, mpi_comm
  integer :: nlon, nlat
  character(len=256) :: valid_file
  character(len=64) :: fnames(2)
  real(r8), allocatable :: field_data(:,:,:)
  type(file_desc_t) :: check_file
  logical :: file_exists

  num_pass = 0
  num_fail = 0
  nlon = 4
  nlat = 3

  ! -----------------------------------------------------------------------
  ! Initialize ESMF
  ! -----------------------------------------------------------------------
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

  ! -----------------------------------------------------------------------
  ! Initialize PIO
  ! -----------------------------------------------------------------------
  call PIO_Init(my_pet, mpi_comm, pet_count, 0, 1, &
               PIO_REARR_BOX, pio_sys)

  ! -----------------------------------------------------------------------
  ! Create a simple 4x3 rectilinear target grid
  ! -----------------------------------------------------------------------
  target_grid = ESMF_GridCreateNoPeriDim( &
    maxIndex=(/nlon, nlat/), &
    coordSys=ESMF_COORDSYS_SPH_DEG, &
    rc=rc)
  if (rc /= ESMF_SUCCESS) then
    print *, "FATAL: ESMF_GridCreateNoPeriDim failed"
    stop 1
  end if

  ! Add center stagger coordinates
  call ESMF_GridAddCoord(target_grid, staggerloc=ESMF_STAGGERLOC_CENTER, rc=rc)
  if (rc /= ESMF_SUCCESS) then
    print *, "FATAL: ESMF_GridAddCoord failed"
    stop 1
  end if

  ! Fill coordinate values
  call fill_grid_coords(target_grid, nlon, nlat, rc)
  if (rc /= ESMF_SUCCESS) then
    print *, "FATAL: fill_grid_coords failed"
    stop 1
  end if


  ! =====================================================================
  ! Test 1: tide_output_init with invalid/nonexistent directory path → error
  !   Validates Requirement 5.8
  ! =====================================================================
  fnames(1) = 'sst'
  fnames(2) = 'ice_frac'

  call tide_output_init(state, pio_sys, "/no/such/dir/output.nc", &
                        target_grid, fnames(1:2), 2, 3600, test_rc)
  if (test_rc /= ESMF_SUCCESS) then
    print *, "PASS: Test 1 - invalid path returns error"
    num_pass = num_pass + 1
  else
    print *, "FAIL: Test 1 - invalid path should have returned error"
    num_fail = num_fail + 1
    ! Clean up if it somehow succeeded
    call tide_output_finalize(state, rc)
  end if

  ! =====================================================================
  ! Test 2: tide_output_init with valid path, write data, finalize,
  !         verify file exists and was properly closed
  !   Validates Requirement 5.7
  ! =====================================================================
  valid_file = "test_output_finalize.nc"

  call tide_output_init(state, pio_sys, trim(valid_file), &
                        target_grid, fnames(1:2), 2, 3600, test_rc)
  if (test_rc /= ESMF_SUCCESS) then
    print *, "FAIL: Test 2 - tide_output_init with valid path failed, rc=", test_rc
    num_fail = num_fail + 1
    goto 200
  end if

  ! Write a single time record of dummy data
  allocate(field_data(nlon, nlat, 2))
  field_data = 42.0_r8

  call tide_output_write(state, field_data, 3600.0_r8, test_rc)
  deallocate(field_data)

  if (test_rc /= ESMF_SUCCESS) then
    print *, "FAIL: Test 2 - tide_output_write failed, rc=", test_rc
    num_fail = num_fail + 1
    call tide_output_finalize(state, rc)
    goto 200
  end if

  ! Finalize (flush + close)
  call tide_output_finalize(state, test_rc)
  if (test_rc /= ESMF_SUCCESS) then
    print *, "FAIL: Test 2 - tide_output_finalize returned error, rc=", test_rc
    num_fail = num_fail + 1
    goto 200
  end if

  ! Verify the file exists on disk
  inquire(file=trim(valid_file), exist=file_exists)
  if (.not. file_exists) then
    print *, "FAIL: Test 2 - output file does not exist after finalize"
    num_fail = num_fail + 1
    goto 200
  end if

  ! Verify the file can be re-opened by PIO (proves it was properly closed)
  rcode = pio_openfile(pio_sys, check_file, PIO_IOTYPE_NETCDF, &
                       trim(valid_file), PIO_NOWRITE)
  if (rcode /= PIO_NOERR) then
    print *, "FAIL: Test 2 - cannot reopen output file via PIO after finalize"
    num_fail = num_fail + 1
  else
    call pio_closefile(check_file)
    print *, "PASS: Test 2 - finalize flushes and closes file correctly"
    num_pass = num_pass + 1
  end if

  ! Clean up temp file
  if (my_pet == 0) then
    open(unit=78, file=trim(valid_file), status='old', iostat=rc)
    if (rc == 0) close(78, status='delete')
  end if

200 continue

  ! -----------------------------------------------------------------------
  ! Summary
  ! -----------------------------------------------------------------------
  print *, "====================================="
  print *, "Output error path tests: ", num_pass, " passed, ", num_fail, " failed"
  print *, "====================================="

  ! -----------------------------------------------------------------------
  ! Cleanup
  ! -----------------------------------------------------------------------
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

end program test_output_error_paths
