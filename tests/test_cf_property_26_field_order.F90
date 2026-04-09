!> @file test_cf_property_26_field_order.F90
!> @brief Property 4: Field Selection and Ordering Preservation
!>
!> Feature: tide-pio-regrid-output, Property 4: Field Selection and Ordering Preservation
!> Validates: Requirements 7.1, 7.4
!>
!> For any non-empty ordered subset of field names drawn from a stream's
!> field_maps model variable names, when that subset is provided as
!> output_fields, the Output_Manager SHALL define NetCDF variables for
!> exactly those fields and in exactly the order specified — no extra
!> fields, no missing fields, and no reordering.
program test_cf_property_26_field_order
  use ESMF
  use pio
  use tide_output_mod
  use shr_kind_mod, only : r8 => shr_kind_r8
  implicit none

  integer, parameter :: NUM_ITERATIONS = 120
  integer, parameter :: MIN_POOL_SIZE  = 5
  integer, parameter :: MAX_POOL_SIZE  = 10
  integer, parameter :: NLON = 4
  integer, parameter :: NLAT = 3
  ! Coordinate variables (lon, lat, time) are always present
  integer, parameter :: NUM_COORD_VARS = 3

  type(tide_output_state_t) :: state
  type(ESMF_Grid)           :: target_grid
  type(ESMF_VM)             :: vm
  type(iosystem_desc_t)     :: pio_sys
  type(file_desc_t)         :: check_file
  integer :: rc, test_rc, rcode
  integer :: num_pass, num_fail
  integer :: my_pet, pet_count, mpi_comm
  integer :: iter, pool_size, subset_size
  integer :: i, j, nvars, idx
  logical :: already_picked

  ! Field name generation
  character(len=64), allocatable :: pool(:)
  character(len=64), allocatable :: subset(:)
  integer, allocatable :: pick_indices(:)
  character(len=256) :: out_file
  character(len=256) :: var_name_buf

  ! Simple LCG PRNG state
  integer(8) :: prng_state

  num_pass = 0
  num_fail = 0

  ! -------------------------------------------------------------------
  ! Initialize ESMF
  ! -------------------------------------------------------------------
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

  ! -------------------------------------------------------------------
  ! Initialize PIO
  ! -------------------------------------------------------------------
  call PIO_Init(my_pet, mpi_comm, pet_count, 0, 1, &
               PIO_REARR_BOX, pio_sys)

  ! -------------------------------------------------------------------
  ! Create a simple 4x3 rectilinear target grid
  ! -------------------------------------------------------------------
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

  ! Seed the PRNG
  prng_state = 987654321_8

  ! ===================================================================
  ! Property test loop
  ! ===================================================================
  do iter = 1, NUM_ITERATIONS

    ! -----------------------------------------------------------------
    ! (a) Generate a pool of 5-10 random field names
    ! -----------------------------------------------------------------
    pool_size = lcg_rand_range(MIN_POOL_SIZE, MAX_POOL_SIZE)
    allocate(pool(pool_size))
    do i = 1, pool_size
      call generate_field_name(i, iter, pool(i))
    end do

    ! -----------------------------------------------------------------
    ! (b) Select a random ordered subset (1 to pool_size fields)
    ! -----------------------------------------------------------------
    subset_size = lcg_rand_range(1, pool_size)
    allocate(subset(subset_size))
    allocate(pick_indices(subset_size))

    ! Pick subset_size unique indices from [1..pool_size] preserving
    ! the random selection order (NOT sorted — this tests ordering).
    do i = 1, subset_size
      do
        idx = lcg_rand_range(1, pool_size)
        already_picked = .false.
        do j = 1, i - 1
          if (pick_indices(j) == idx) then
            already_picked = .true.
            exit
          end if
        end do
        if (.not. already_picked) exit
      end do
      pick_indices(i) = idx
      subset(i) = pool(idx)
    end do

    ! -----------------------------------------------------------------
    ! (c) Call tide_output_init with the selected field names
    ! -----------------------------------------------------------------
    write(out_file, '(a,i0,a)') "test_field_order_", iter, ".nc"

    call tide_output_init(state, pio_sys, trim(out_file), &
                          target_grid, subset, subset_size, &
                          3600, test_rc)
    if (test_rc /= ESMF_SUCCESS) then
      write(*,'(a,i0,a)') "FAIL iter=", iter, " tide_output_init failed"
      num_fail = num_fail + 1
      deallocate(pool, subset, pick_indices)
      cycle
    end if

    ! Finalize to flush and close the file before reading back
    call tide_output_finalize(state, test_rc)
    if (test_rc /= ESMF_SUCCESS) then
      write(*,'(a,i0,a)') "FAIL iter=", iter, " tide_output_finalize failed"
      num_fail = num_fail + 1
      deallocate(pool, subset, pick_indices)
      cycle
    end if

    ! -----------------------------------------------------------------
    ! (d) Use PIO to read back the NetCDF file and verify
    ! -----------------------------------------------------------------
    rcode = pio_openfile(pio_sys, check_file, PIO_IOTYPE_NETCDF, &
                         trim(out_file), PIO_NOWRITE)
    if (rcode /= PIO_NOERR) then
      write(*,'(a,i0,a)') "FAIL iter=", iter, " cannot reopen output file"
      num_fail = num_fail + 1
      deallocate(pool, subset, pick_indices)
      cycle
    end if

    ! Query total number of variables
    rcode = pio_inquire(check_file, nVariables=nvars)
    if (rcode /= PIO_NOERR) then
      write(*,'(a,i0,a)') "FAIL iter=", iter, " pio_inquire failed"
      call pio_closefile(check_file)
      num_fail = num_fail + 1
      deallocate(pool, subset, pick_indices)
      cycle
    end if

    ! Verify: total vars = coordinate vars + data vars
    if (nvars /= NUM_COORD_VARS + subset_size) then
      write(*,'(a,i0,a,i0,a,i0)') "FAIL iter=", iter, &
        " expected nvars=", NUM_COORD_VARS + subset_size, " got=", nvars
      call pio_closefile(check_file)
      num_fail = num_fail + 1
      deallocate(pool, subset, pick_indices)
      cycle
    end if

    ! Verify: data variables appear in correct order after coordinate vars.
    ! NetCDF variable IDs are 1-based and sequential in definition order.
    ! Coordinate vars are defined first (lon=1, lat=2, time=3),
    ! then data vars start at ID = NUM_COORD_VARS + 1.
    call verify_field_order(check_file, subset, subset_size, iter, test_rc)

    call pio_closefile(check_file)

    if (test_rc == ESMF_SUCCESS) then
      num_pass = num_pass + 1
    else
      num_fail = num_fail + 1
    end if

    ! -----------------------------------------------------------------
    ! (f) Clean up the temp file
    ! -----------------------------------------------------------------
    if (my_pet == 0) then
      open(unit=79, file=trim(out_file), status='old', iostat=rc)
      if (rc == 0) close(79, status='delete')
    end if

    deallocate(pool, subset, pick_indices)
  end do

  ! -------------------------------------------------------------------
  ! Summary
  ! -------------------------------------------------------------------
  write(*,'(a)') '====================================='
  write(*,'(a,i0,a,i0,a,i0,a)') &
    'Property 4 (Field Selection & Ordering): ', num_pass, '/', &
    NUM_ITERATIONS, ' passed (', num_fail, ' failed)'
  write(*,'(a)') '====================================='

  ! -------------------------------------------------------------------
  ! Cleanup
  ! -------------------------------------------------------------------
  call ESMF_GridDestroy(target_grid, rc=rc)
  call pio_finalize(pio_sys, rc)
  call ESMF_Finalize(rc=rc)

  if (num_fail > 0) stop 1

contains

  !> Simple Linear Congruential Generator (LCG) PRNG.
  !> Returns a pseudo-random integer in [lo, hi].
  function lcg_rand_range(lo, hi) result(val)
    integer, intent(in) :: lo, hi
    integer :: val
    integer(8) :: a, c, m

    a = 1103515245_8
    c = 12345_8
    m = 2147483648_8  ! 2^31

    prng_state = mod(a * prng_state + c, m)
    val = lo + int(mod(abs(prng_state), int(hi - lo + 1, 8)))
  end function lcg_rand_range

  !> Generate a deterministic field name from pool index and iteration.
  !> Produces names like "fld_03_i042" to ensure uniqueness within a pool.
  subroutine generate_field_name(pool_idx, iteration, name)
    integer, intent(in) :: pool_idx, iteration
    character(len=64), intent(out) :: name
    integer :: suffix

    suffix = lcg_rand_range(100, 9999)
    write(name, '(a,i0,a,i0,a,i0)') "fld_", pool_idx, "_i", iteration, "_s", suffix
  end subroutine generate_field_name

  !> Verify that data variables in the NetCDF file match the expected
  !> subset in exactly the specified order.
  subroutine verify_field_order(pfile, expected, nexpected, iteration, rc)
    type(file_desc_t), intent(inout) :: pfile
    character(len=64), intent(in) :: expected(:)
    integer, intent(in) :: nexpected
    integer, intent(in) :: iteration
    integer, intent(out) :: rc

    character(len=256) :: vname
    integer :: i, rcode

    rc = ESMF_SUCCESS

    do i = 1, nexpected
      ! Data variable IDs start at NUM_COORD_VARS + 1
      rcode = pio_inq_varname(pfile, NUM_COORD_VARS + i, vname)
      if (rcode /= PIO_NOERR) then
        write(*,'(a,i0,a,i0,a)') "FAIL iter=", iteration, &
          " pio_inq_varname failed for varid=", NUM_COORD_VARS + i, ""
        rc = ESMF_FAILURE
        return
      end if

      if (trim(vname) /= trim(expected(i))) then
        write(*,'(a,i0,a,i0,a,a,a,a,a)') "FAIL iter=", iteration, &
          " var ", i, " expected='", trim(expected(i)), &
          "' got='", trim(vname), "'"
        rc = ESMF_FAILURE
        return
      end if
    end do

  end subroutine verify_field_order

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

end program test_cf_property_26_field_order
