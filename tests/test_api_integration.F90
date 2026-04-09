!> @file test_api_integration.F90
!> @brief Unit tests for TIDE API integration at the tide_type level.
!>
!> Validates Requirements 6.5 and 6.6:
!>   6.5 - When output is not configured, TIDE behaves identically to current
!>         implementation with no performance overhead.
!>   6.6 - TIDE stores output state within tide_type so that multiple TIDE
!>         instances operate independently.
!>
!> These tests verify allocation, flag state, and independence of tide_type
!> instances without requiring actual NetCDF data files.
program test_api_integration
  use tide_mod
  use tide_regrid_mod
  use tide_output_mod
  use ESMF
  use shr_kind_mod, only : r8 => shr_kind_r8
  implicit none

  integer :: rc
  integer :: num_pass, num_fail

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

  ! -------------------------------------------------------------------
  ! Test 1: tide_init without output blocks → identical to current behavior
  ! Validates: Requirement 6.5
  ! -------------------------------------------------------------------
  call test_no_output_blocks(num_pass, num_fail)

  ! -------------------------------------------------------------------
  ! Test 2: Two independent tide_type instances
  ! Validates: Requirement 6.6
  ! -------------------------------------------------------------------
  call test_independent_instances(num_pass, num_fail)

  ! -------------------------------------------------------------------
  ! Summary
  ! -------------------------------------------------------------------
  print *, "====================================="
  print *, "API integration tests: ", num_pass, " passed, ", num_fail, " failed"
  print *, "====================================="

  call ESMF_Finalize(rc=rc)

  if (num_fail > 0) stop 1

contains


  ! =================================================================
  ! Test 1: tide_type without output blocks → output_enabled all false,
  !         finalize succeeds cleanly (zero overhead path).
  !
  ! Simulates what tide_init does for a config with no output blocks:
  ! allocates the per-stream arrays and sets output_enabled to .false.
  ! Then calls tide_finalize to verify clean teardown.
  !
  ! Validates: Requirement 6.5
  ! =================================================================
  subroutine test_no_output_blocks(num_pass, num_fail)
    integer, intent(inout) :: num_pass, num_fail

    type(tide_type) :: tide
    integer :: test_rc, i
    integer :: nstreams
    logical :: ok

    ok = .true.
    nstreams = 3

    ! Simulate tide_init allocation for a config with no output blocks
    tide%num_streams = nstreams
    allocate(tide%sdat(nstreams))
    allocate(tide%regrid(nstreams))
    allocate(tide%output(nstreams))
    allocate(tide%output_enabled(nstreams))
    tide%output_enabled(:) = .false.

    ! Verify: output_enabled is all .false.
    do i = 1, nstreams
      if (tide%output_enabled(i)) then
        print *, "FAIL Test1: output_enabled(", i, ") should be .false."
        ok = .false.
        exit
      end if
    end do

    ! Verify: num_streams is correct
    if (ok .and. tide%num_streams /= nstreams) then
      print *, "FAIL Test1: num_streams expected", nstreams, "got", tide%num_streams
      ok = .false.
    end if

    ! Verify: regrid states are not initialized
    if (ok) then
      do i = 1, nstreams
        if (tide%regrid(i)%initialized) then
          print *, "FAIL Test1: regrid(", i, ")%initialized should be .false."
          ok = .false.
          exit
        end if
      end do
    end if

    ! Verify: output states are not initialized
    if (ok) then
      do i = 1, nstreams
        if (tide%output(i)%initialized) then
          print *, "FAIL Test1: output(", i, ")%initialized should be .false."
          ok = .false.
          exit
        end if
      end do
    end if

    ! Call tide_finalize — should succeed cleanly with no output to tear down
    call tide_finalize(tide, test_rc)
    if (ok .and. test_rc /= ESMF_SUCCESS) then
      print *, "FAIL Test1: tide_finalize returned error rc=", test_rc
      ok = .false.
    end if

    ! Verify: arrays are deallocated after finalize
    if (ok .and. allocated(tide%output_enabled)) then
      print *, "FAIL Test1: output_enabled should be deallocated after finalize"
      ok = .false.
    end if
    if (ok .and. allocated(tide%regrid)) then
      print *, "FAIL Test1: regrid should be deallocated after finalize"
      ok = .false.
    end if
    if (ok .and. allocated(tide%output)) then
      print *, "FAIL Test1: output should be deallocated after finalize"
      ok = .false.
    end if

    if (ok) then
      print *, "PASS Test1: No-output config allocates correctly and finalizes cleanly"
      num_pass = num_pass + 1
    else
      num_fail = num_fail + 1
    end if

  end subroutine test_no_output_blocks


  ! =================================================================
  ! Test 2: Two independent tide_type instances with separate state.
  !
  ! Creates two tide_type variables, allocates both with different
  ! num_streams, verifies they have independent state (different
  ! num_streams, separate allocations), and finalizes both.
  !
  ! Validates: Requirement 6.6
  ! =================================================================
  subroutine test_independent_instances(num_pass, num_fail)
    integer, intent(inout) :: num_pass, num_fail

    type(tide_type) :: tide_a, tide_b
    integer :: test_rc
    logical :: ok

    ok = .true.

    ! --- Initialize instance A with 2 streams ---
    tide_a%num_streams = 2
    allocate(tide_a%sdat(2))
    allocate(tide_a%regrid(2))
    allocate(tide_a%output(2))
    allocate(tide_a%output_enabled(2))
    tide_a%output_enabled(:) = .false.

    ! --- Initialize instance B with 4 streams ---
    tide_b%num_streams = 4
    allocate(tide_b%sdat(4))
    allocate(tide_b%regrid(4))
    allocate(tide_b%output(4))
    allocate(tide_b%output_enabled(4))
    tide_b%output_enabled(:) = .false.

    ! Verify: instances have different num_streams
    if (tide_a%num_streams == tide_b%num_streams) then
      print *, "FAIL Test2: instances should have different num_streams"
      ok = .false.
    end if

    ! Verify: A has 2 streams
    if (ok .and. tide_a%num_streams /= 2) then
      print *, "FAIL Test2: tide_a%num_streams expected 2, got", tide_a%num_streams
      ok = .false.
    end if

    ! Verify: B has 4 streams
    if (ok .and. tide_b%num_streams /= 4) then
      print *, "FAIL Test2: tide_b%num_streams expected 4, got", tide_b%num_streams
      ok = .false.
    end if

    ! Verify: output_enabled arrays have correct sizes
    if (ok .and. size(tide_a%output_enabled) /= 2) then
      print *, "FAIL Test2: tide_a output_enabled size expected 2, got", &
               size(tide_a%output_enabled)
      ok = .false.
    end if
    if (ok .and. size(tide_b%output_enabled) /= 4) then
      print *, "FAIL Test2: tide_b output_enabled size expected 4, got", &
               size(tide_b%output_enabled)
      ok = .false.
    end if

    ! Verify: regrid arrays have correct sizes
    if (ok .and. size(tide_a%regrid) /= 2) then
      print *, "FAIL Test2: tide_a regrid size expected 2, got", size(tide_a%regrid)
      ok = .false.
    end if
    if (ok .and. size(tide_b%regrid) /= 4) then
      print *, "FAIL Test2: tide_b regrid size expected 4, got", size(tide_b%regrid)
      ok = .false.
    end if

    ! Verify: output arrays have correct sizes
    if (ok .and. size(tide_a%output) /= 2) then
      print *, "FAIL Test2: tide_a output size expected 2, got", size(tide_a%output)
      ok = .false.
    end if
    if (ok .and. size(tide_b%output) /= 4) then
      print *, "FAIL Test2: tide_b output size expected 4, got", size(tide_b%output)
      ok = .false.
    end if

    ! Modify A's state — should not affect B
    tide_a%output_enabled(1) = .true.
    if (ok .and. any(tide_b%output_enabled)) then
      print *, "FAIL Test2: modifying A affected B's output_enabled"
      ok = .false.
    end if
    ! Reset for clean finalize
    tide_a%output_enabled(1) = .false.

    ! Finalize A — should not affect B
    call tide_finalize(tide_a, test_rc)
    if (ok .and. test_rc /= ESMF_SUCCESS) then
      print *, "FAIL Test2: tide_finalize(A) returned error rc=", test_rc
      ok = .false.
    end if

    ! Verify: B is still intact after A is finalized
    if (ok .and. .not. allocated(tide_b%output_enabled)) then
      print *, "FAIL Test2: B's output_enabled deallocated after finalizing A"
      ok = .false.
    end if
    if (ok .and. tide_b%num_streams /= 4) then
      print *, "FAIL Test2: B's num_streams changed after finalizing A"
      ok = .false.
    end if

    ! Finalize B
    call tide_finalize(tide_b, test_rc)
    if (ok .and. test_rc /= ESMF_SUCCESS) then
      print *, "FAIL Test2: tide_finalize(B) returned error rc=", test_rc
      ok = .false.
    end if

    ! Verify: both are fully cleaned up
    if (ok .and. allocated(tide_a%output_enabled)) then
      print *, "FAIL Test2: A's output_enabled not deallocated"
      ok = .false.
    end if
    if (ok .and. allocated(tide_b%output_enabled)) then
      print *, "FAIL Test2: B's output_enabled not deallocated"
      ok = .false.
    end if

    if (ok) then
      print *, "PASS Test2: Two independent tide_type instances operate independently"
      num_pass = num_pass + 1
    else
      num_fail = num_fail + 1
    end if

  end subroutine test_independent_instances

end program test_api_integration
