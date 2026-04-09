!> @file test_cf_property_27_invalid_field.F90
!> @brief Property 5: Invalid Output Field Rejection
!>
!> Feature: tide-pio-regrid-output, Property 5: Invalid Output Field Rejection
!> Validates: Requirements 7.3
!>
!> For any field name string that does not appear in the stream's
!> field_maps model variable names, when that string is included in
!> output_fields, the TIDE initialization SHALL return an error code
!> and the output file SHALL not be created.
program test_cf_property_27_invalid_field
  use ESMF
  use tide_yaml_mod
  use tide_mod, only : tide_validate_output_fields
  use shr_kind_mod, only : cl => shr_kind_cl
  use, intrinsic :: iso_c_binding
  implicit none

  integer, parameter :: NUM_ITERATIONS   = 120
  integer, parameter :: MIN_VALID_FIELDS = 2
  integer, parameter :: MAX_VALID_FIELDS = 8
  integer, parameter :: MAX_NAME_LEN     = 48

  integer :: rc, test_rc
  integer :: num_pass, num_fail
  integer :: iter, num_valid, num_output, inject_pos
  integer :: i, valid_idx, num_validated

  ! C-compatible storage for model_vars (the "known valid" set)
  character(kind=c_char, len=1), allocatable, target :: valid_bufs(:,:)
  type(c_ptr), allocatable, target :: valid_ptrs(:)

  ! Fortran-side copy of valid names for reuse in output_fields
  character(len=MAX_NAME_LEN), allocatable :: valid_names(:)

  ! C-compatible storage for output_fields (contains at least one invalid)
  character(kind=c_char, len=1), allocatable, target :: out_bufs(:,:)
  type(c_ptr), allocatable, target :: out_ptrs(:)

  ! The output config struct
  type(tide_output_config_t) :: output_cfg

  ! Validated fields returned by the subroutine
  character(len=cl), allocatable :: validated_fields(:)

  ! Scratch for name generation
  character(len=MAX_NAME_LEN) :: name_buf
  character(len=MAX_NAME_LEN) :: invalid_name

  ! Simple LCG PRNG state
  integer(8) :: prng_state

  num_pass = 0
  num_fail = 0

  ! -------------------------------------------------------------------
  ! Initialize ESMF (needed for ESMF_SUCCESS / ESMF_FAILURE constants)
  ! -------------------------------------------------------------------
  call ESMF_Initialize(rc=rc)
  if (rc /= ESMF_SUCCESS) then
    print *, "FATAL: ESMF_Initialize failed"
    stop 1
  end if

  ! Seed the PRNG
  prng_state = 314159265_8

  ! ===================================================================
  ! Property test loop
  ! ===================================================================
  do iter = 1, NUM_ITERATIONS

    ! -----------------------------------------------------------------
    ! (a) Generate a set of known valid model variable names
    ! -----------------------------------------------------------------
    num_valid = lcg_rand_range(MIN_VALID_FIELDS, MAX_VALID_FIELDS)

    allocate(valid_bufs(MAX_NAME_LEN, num_valid))
    allocate(valid_ptrs(num_valid))
    allocate(valid_names(num_valid))

    do i = 1, num_valid
      call generate_valid_name(i, iter, name_buf)
      valid_names(i) = name_buf
      call pack_c_string(name_buf, valid_bufs(:, i))
      valid_ptrs(i) = c_loc(valid_bufs(1, i))
    end do

    ! -----------------------------------------------------------------
    ! (b) Build output_fields with at least one invalid name injected
    ! -----------------------------------------------------------------
    num_output = lcg_rand_range(1, num_valid) + 1  ! +1 for the invalid entry
    inject_pos = lcg_rand_range(1, num_output)

    allocate(out_bufs(MAX_NAME_LEN, num_output))
    allocate(out_ptrs(num_output))

    ! Generate the invalid name (prefix "INVALID_" never used by valid names)
    call generate_invalid_name(iter, invalid_name)

    ! Fill output_fields: valid names except at inject_pos
    valid_idx = 0
    do i = 1, num_output
      if (i == inject_pos) then
        call pack_c_string(invalid_name, out_bufs(:, i))
      else
        valid_idx = valid_idx + 1
        if (valid_idx > num_valid) valid_idx = 1
        call pack_c_string(valid_names(valid_idx), out_bufs(:, i))
      end if
      out_ptrs(i) = c_loc(out_bufs(1, i))
    end do

    ! -----------------------------------------------------------------
    ! (c) Build the tide_output_config_t
    ! -----------------------------------------------------------------
    output_cfg%output_fields     = c_loc(out_ptrs(1))
    output_cfg%num_output_fields = num_output
    output_cfg%output_enabled    = 1
    output_cfg%target_grid_file  = c_null_ptr
    output_cfg%output_file       = c_null_ptr
    output_cfg%output_frequency  = 3600
    output_cfg%regrid_method     = c_null_ptr

    ! -----------------------------------------------------------------
    ! (d) Call tide_validate_output_fields — expect ESMF_FAILURE
    ! -----------------------------------------------------------------
    test_rc = ESMF_SUCCESS
    num_validated = 0
    if (allocated(validated_fields)) deallocate(validated_fields)

    call tide_validate_output_fields(output_cfg, c_loc(valid_ptrs(1)), &
                                     num_valid, validated_fields, &
                                     num_validated, test_rc)

    if (test_rc /= ESMF_SUCCESS) then
      num_pass = num_pass + 1
    else
      write(*,'(a,i0,a,a,a)') "FAIL iter=", iter, &
        " expected ESMF_FAILURE but got SUCCESS. Invalid='", &
        trim(invalid_name), "'"
      num_fail = num_fail + 1
    end if

    ! Verify num_validated is 0 on failure
    if (test_rc /= ESMF_SUCCESS .and. num_validated /= 0) then
      write(*,'(a,i0,a,i0)') "WARN iter=", iter, &
        " num_validated should be 0 on failure, got=", num_validated
    end if

    ! -----------------------------------------------------------------
    ! (e) Clean up iteration allocations
    ! -----------------------------------------------------------------
    if (allocated(validated_fields)) deallocate(validated_fields)
    deallocate(valid_bufs, valid_ptrs, valid_names)
    deallocate(out_bufs, out_ptrs)

  end do

  ! -------------------------------------------------------------------
  ! Summary
  ! -------------------------------------------------------------------
  write(*,'(a)') '====================================='
  write(*,'(a,i0,a,i0,a,i0,a)') &
    'Property 5 (Invalid Output Field Rejection): ', num_pass, '/', &
    NUM_ITERATIONS, ' passed (', num_fail, ' failed)'
  write(*,'(a)') '====================================='

  ! -------------------------------------------------------------------
  ! Cleanup
  ! -------------------------------------------------------------------
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

  !> Generate a deterministic valid field name.
  !> Uses prefix "mv_" so it is distinguishable from invalid names.
  subroutine generate_valid_name(field_idx, iteration, name)
    integer, intent(in) :: field_idx, iteration
    character(len=MAX_NAME_LEN), intent(out) :: name
    integer :: suffix

    suffix = lcg_rand_range(100, 9999)
    write(name, '(a,i0,a,i0,a,i0)') "mv_", field_idx, "_i", iteration, "_s", suffix
  end subroutine generate_valid_name

  !> Generate a deterministic invalid field name.
  !> Uses prefix "INVALID_" which no valid name ever uses.
  subroutine generate_invalid_name(iteration, name)
    integer, intent(in) :: iteration
    character(len=MAX_NAME_LEN), intent(out) :: name
    integer :: suffix

    suffix = lcg_rand_range(1000, 99999)
    write(name, '(a,i0,a,i0)') "INVALID_", iteration, "_x", suffix
  end subroutine generate_invalid_name

  !> Pack a Fortran string into a null-terminated C character buffer.
  subroutine pack_c_string(fstr, cbuf)
    character(len=*), intent(in) :: fstr
    character(kind=c_char, len=1), intent(out) :: cbuf(:)
    integer :: k, slen

    slen = len_trim(fstr)
    do k = 1, min(slen, size(cbuf) - 1)
      cbuf(k) = fstr(k:k)
    end do
    cbuf(min(slen, size(cbuf) - 1) + 1) = c_null_char
  end subroutine pack_c_string

end program test_cf_property_27_invalid_field
