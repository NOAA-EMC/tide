!> @file test_cf_property_25_write_freq.F90
!> @brief Property 3: Output Write Frequency Trigger
!>
!> Feature: tide-pio-regrid-output, Property 3: Output Write Frequency Trigger
!> Validates: Requirements 5.6
!>
!> For any positive output_frequency and any sequence of positive dt values,
!> tide_output_should_write returns true exactly when cumulative elapsed time
!> since the last write >= output_frequency, and after returning true the
!> elapsed counter resets so the next trigger occurs after another
!> output_frequency seconds have accumulated.
program test_cf_property_25_write_freq
  use ESMF
  use tide_output_mod, only : tide_output_state_t, tide_output_should_write
  implicit none

  integer, parameter :: NUM_ITERATIONS = 150
  integer, parameter :: MAX_STEPS      = 50
  integer, parameter :: MIN_STEPS      = 20
  integer, parameter :: MIN_FREQ       = 100
  integer, parameter :: MAX_FREQ       = 86400
  integer, parameter :: MIN_DT         = 10
  integer, parameter :: MAX_DT         = 500

  integer :: rc, iter, step, nsteps
  integer :: npass, nfail
  integer :: output_frequency, dt_val
  integer :: cumulative, expected_cumulative
  logical :: should_write, expected_write
  type(tide_output_state_t) :: state

  ! Simple LCG PRNG state
  integer(8) :: prng_state

  npass = 0
  nfail = 0

  ! -------------------------------------------------------------------
  ! Initialize ESMF
  ! -------------------------------------------------------------------
  call ESMF_Initialize(rc=rc)
  if (rc /= ESMF_SUCCESS) then
    write(*,*) 'FATAL: ESMF_Initialize failed'
    stop 1
  end if

  ! Seed the PRNG
  prng_state = 123456789_8

  ! -------------------------------------------------------------------
  ! Property test loop
  ! -------------------------------------------------------------------
  do iter = 1, NUM_ITERATIONS
    ! Generate random output_frequency in [MIN_FREQ, MAX_FREQ]
    output_frequency = lcg_rand_range(MIN_FREQ, MAX_FREQ)

    ! Generate random number of steps in [MIN_STEPS, MAX_STEPS]
    nsteps = lcg_rand_range(MIN_STEPS, MAX_STEPS)

    ! Initialize state manually (no PIO needed for should_write)
    state%output_frequency = output_frequency
    state%elapsed_seconds  = 0

    cumulative = 0
    expected_cumulative = 0

    do step = 1, nsteps
      ! Generate random dt in [MIN_DT, MAX_DT]
      dt_val = lcg_rand_range(MIN_DT, MAX_DT)

      ! Call the function under test
      should_write = tide_output_should_write(state, dt_val)

      ! Track cumulative elapsed in our reference model
      cumulative = cumulative + dt_val

      ! Determine expected behavior:
      ! should_write is true when cumulative since last reset >= output_frequency
      expected_cumulative = expected_cumulative + dt_val
      if (expected_cumulative >= output_frequency) then
        expected_write = .true.
        ! The function subtracts output_frequency (preserves remainder)
        expected_cumulative = expected_cumulative - output_frequency
      else
        expected_write = .false.
      end if

      ! Verify should_write matches expected
      if (should_write .neqv. expected_write) then
        write(*,'(a,i0,a,i0,a,i0,a,l1,a,l1)') &
          'FAIL iter=', iter, ' step=', step, ' dt=', dt_val, &
          ' got=', should_write, ' expected=', expected_write
        nfail = nfail + 1
        goto 100
      end if

      ! Verify internal elapsed_seconds matches our reference
      if (state%elapsed_seconds /= expected_cumulative) then
        write(*,'(a,i0,a,i0,a,i0,a,i0)') &
          'FAIL iter=', iter, ' step=', step, &
          ' elapsed=', state%elapsed_seconds, &
          ' expected_elapsed=', expected_cumulative
        nfail = nfail + 1
        goto 100
      end if
    end do

    ! All steps passed for this iteration
    npass = npass + 1
100 continue
  end do

  ! -------------------------------------------------------------------
  ! Summary
  ! -------------------------------------------------------------------
  write(*,'(a)') '====================================='
  write(*,'(a,i0,a,i0,a,i0,a)') &
    'Property 3 (Write Frequency Trigger): ', npass, '/', NUM_ITERATIONS, &
    ' passed (', nfail, ' failed)'
  write(*,'(a)') '====================================='

  ! -------------------------------------------------------------------
  ! Finalize ESMF
  ! -------------------------------------------------------------------
  call ESMF_Finalize(rc=rc)

  if (nfail > 0) stop 1

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

end program test_cf_property_25_write_freq
