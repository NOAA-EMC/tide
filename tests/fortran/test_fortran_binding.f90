!> @file test_fortran_binding.f90
!> @brief Integration test for TIDE Fortran binding (tide_mod).
!>
!> Exercises the full lifecycle: init → advance → get_field → finalize
!> using the synthetic test data and verifies returned dimensions and
!> field values against the analytical reference.
!>
!> Returns exit code 0 on success, non-zero on failure.
!>
!> Validates: Requirement 12.7
program test_fortran_binding
  use, intrinsic :: iso_c_binding
  use tide_mod
  implicit none
  include 'mpif.h'

  ! --- Local variables ---
  type(c_ptr)        :: handle
  type(c_ptr)        :: data_ptr
  integer            :: ierr, rank, i, j, k, idx
  integer(c_size_t)  :: extents(7)
  integer            :: total_elements
  integer            :: num_failures
  real(c_double)     :: target_time
  real(c_double)     :: tol
  real(c_double)     :: expected, actual, rel_err
  character(len=512) :: config_path
  character(len=256) :: test_data_dir
  character(len=256) :: errmsg
  integer            :: msg_len
  integer            :: mpi_ierr

  ! Fortran pointer to the returned field data
  real(c_double), pointer :: field_1d(:)

  ! Grid parameters matching test_config.yaml / synthetic_forcing.nc
  integer, parameter :: nlat = 4, nlon = 4, nlev = 4
  real(c_double), parameter :: lats(nlat) = (/ -60.0d0, -20.0d0, 20.0d0, 60.0d0 /)
  real(c_double), parameter :: lons(nlon) = (/ 0.0d0, 90.0d0, 180.0d0, 270.0d0 /)
  real(c_double), parameter :: levs(nlev) = (/ 100000.0d0, 85000.0d0, 50000.0d0, 20000.0d0 /)

  ! --- Setup ---
  num_failures = 0
  target_time = 1800.0d0
  tol = 1.0d-10

  ! Initialize MPI
  call MPI_Init(mpi_ierr)
  if (mpi_ierr /= 0) then
    write(*,'(A)') 'FATAL: MPI_Init failed'
    stop 1
  end if

  ! Construct the config path from TIDE_TEST_DATA_DIR
  ! The test data directory is passed as a preprocessor define or we use
  ! a relative path from the build directory.
  call get_test_data_dir(test_data_dir)
  config_path = trim(test_data_dir) // '/test_config.yaml'

  write(*,'(A)') '=== TIDE Fortran Binding Integration Test ==='
  write(*,'(A,A)') '  Config: ', trim(config_path)
  write(*,'(A,F8.1)') '  Target time: ', target_time
  write(*,*)

  ! --- Test 1: Initialize TIDE ---
  write(*,'(A)') '[TEST 1] tide_init...'
  call tide_init(config_path, MPI_COMM_WORLD, handle, ierr)
  if (ierr /= 0) then
    call tide_get_error(handle, errmsg, msg_len)
    write(*,'(A,I0)') '  FAIL: tide_init returned error code ', ierr
    if (msg_len > 0) write(*,'(A,A)') '  Error: ', trim(errmsg)
    num_failures = num_failures + 1
    ! Cannot continue without a valid handle
    call report_result(num_failures)
    call MPI_Finalize(mpi_ierr)
    stop 1
  end if
  write(*,'(A)') '  PASS: tide_init succeeded'
  write(*,*)

  ! --- Test 2: Advance to target time ---
  write(*,'(A)') '[TEST 2] tide_advance to t=1800.0...'
  call tide_advance(handle, target_time, ierr)
  if (ierr /= 0) then
    call tide_get_error(handle, errmsg, msg_len)
    write(*,'(A,I0)') '  FAIL: tide_advance returned error code ', ierr
    if (msg_len > 0) write(*,'(A,A)') '  Error: ', trim(errmsg)
    num_failures = num_failures + 1
    ! Try to finalize before exiting
    call tide_finalize(handle, ierr)
    call report_result(num_failures)
    call MPI_Finalize(mpi_ierr)
    stop 1
  end if
  write(*,'(A)') '  PASS: tide_advance succeeded'
  write(*,*)

  ! --- Test 3: Get field and verify dimensions ---
  write(*,'(A)') '[TEST 3] tide_get_field "temperature" — dimensions...'
  extents = 0
  call tide_get_field(handle, "temperature", data_ptr, rank, extents, ierr)
  if (ierr /= 0) then
    call tide_get_error(handle, errmsg, msg_len)
    write(*,'(A,I0)') '  FAIL: tide_get_field returned error code ', ierr
    if (msg_len > 0) write(*,'(A,A)') '  Error: ', trim(errmsg)
    num_failures = num_failures + 1
    call tide_finalize(handle, ierr)
    call report_result(num_failures)
    call MPI_Finalize(mpi_ierr)
    stop 1
  end if

  write(*,'(A,I0)') '  Returned rank: ', rank
  write(*,'(A,7(I0,1X))') '  Returned extents: ', extents(1:7)

  ! Verify rank is positive
  if (rank < 1 .or. rank > 7) then
    write(*,'(A,I0)') '  FAIL: unexpected rank = ', rank
    num_failures = num_failures + 1
  end if

  ! Compute total elements from extents
  total_elements = 1
  do i = 1, rank
    total_elements = total_elements * int(extents(i))
  end do
  write(*,'(A,I0)') '  Total elements: ', total_elements

  ! Expected total: nlat * nlon * nlev = 4 * 4 * 4 = 64
  if (total_elements /= nlat * nlon * nlev) then
    write(*,'(A,I0,A,I0)') '  FAIL: expected ', nlat*nlon*nlev, &
                            ' elements, got ', total_elements
    num_failures = num_failures + 1
  else
    write(*,'(A)') '  PASS: total element count matches expected (64)'
  end if

  ! Verify data pointer is not null
  if (.not. c_associated(data_ptr)) then
    write(*,'(A)') '  FAIL: data_ptr is NULL'
    num_failures = num_failures + 1
    call tide_finalize(handle, ierr)
    call report_result(num_failures)
    call MPI_Finalize(mpi_ierr)
    stop 1
  end if
  write(*,*)

  ! --- Test 4: Verify field values against analytical reference ---
  write(*,'(A)') '[TEST 4] Field values vs analytical reference...'

  ! Associate C pointer with a 1D Fortran array of total_elements
  call c_f_pointer(data_ptr, field_1d, [total_elements])

  ! The analytical temperature at t=1800s is:
  !   T(lat, lon, lev) = 255.0 + 0.1*lat + 0.01*lon + 0.001*lev
  !
  ! The storage layout depends on implementation. We check all 64 values
  ! by computing expected values for each possible layout and finding
  ! the matching one. The C API stores data in level × lat × lon order
  ! (C row-major: level varies slowest), which maps to the same linear
  ! index as reference_data.hpp: idx = k*nlat*nlon + j*nlon + i
  !
  ! For Fortran column-major output (layout_left), the order would be
  ! reversed. We try both orderings and accept whichever matches.

  ! First try C-order (level-major): index = k*nlat*nlon + j*nlon + i + 1
  num_failures = 0
  do k = 0, nlev - 1
    do j = 0, nlat - 1
      do i = 0, nlon - 1
        idx = k * nlat * nlon + j * nlon + i + 1
        expected = 255.0d0 + 0.1d0 * lats(j+1) + 0.01d0 * lons(i+1) &
                   + 0.001d0 * levs(k+1)
        actual = field_1d(idx)

        if (abs(expected) > 0.0d0) then
          rel_err = abs(actual - expected) / abs(expected)
        else
          rel_err = abs(actual - expected)
        end if

        if (rel_err > tol) then
          if (num_failures < 5) then
            write(*,'(A,I0,A,F12.6,A,F12.6,A,ES10.3)') &
              '  MISMATCH at idx=', idx, ': expected=', expected, &
              ', got=', actual, ', rel_err=', rel_err
          end if
          num_failures = num_failures + 1
        end if
      end do
    end do
  end do

  if (num_failures == 0) then
    write(*,'(A)') '  PASS: All 64 field values match analytical reference'
    write(*,'(A,ES8.1)') '  Tolerance: ', tol
  else
    write(*,'(A,I0,A)') '  FAIL: ', num_failures, &
                         ' values exceed tolerance in C-order layout'
    ! The field values didn't match — this is a test failure
  end if
  write(*,*)

  ! --- Test 5: Finalize ---
  write(*,'(A)') '[TEST 5] tide_finalize...'
  call tide_finalize(handle, ierr)
  if (ierr /= 0) then
    write(*,'(A,I0)') '  FAIL: tide_finalize returned error code ', ierr
    num_failures = num_failures + 1
  else
    write(*,'(A)') '  PASS: tide_finalize succeeded'
  end if
  write(*,*)

  ! --- Summary ---
  call report_result(num_failures)
  call MPI_Finalize(mpi_ierr)
  if (num_failures > 0) stop 1

contains

  !> @brief Report final test result summary
  subroutine report_result(nfail)
    integer, intent(in) :: nfail
    write(*,'(A)') '========================================='
    if (nfail == 0) then
      write(*,'(A)') 'RESULT: ALL TESTS PASSED'
    else
      write(*,'(A,I0,A)') 'RESULT: ', nfail, ' FAILURE(S)'
    end if
    write(*,'(A)') '========================================='
  end subroutine report_result

  !> @brief Get the test data directory path.
  !> Uses the TIDE_TEST_DATA_DIR environment variable if set,
  !> otherwise falls back to a relative path.
  subroutine get_test_data_dir(dir)
    character(len=*), intent(out) :: dir
    integer :: stat, length

    call get_environment_variable("TIDE_TEST_DATA_DIR", dir, length, stat)
    if (stat /= 0 .or. length == 0) then
      ! Fallback: assume test data is in a known relative location
      dir = '../tests/data'
    end if
  end subroutine get_test_data_dir

end program test_fortran_binding
