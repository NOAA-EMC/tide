!> @file test_field_selection_unit.F90
!> @brief Unit tests for output field selection edge cases.
!>
!> Validates: Requirements 7.1, 7.2, 7.3, 7.4
!>
!> Test 1: Empty output_fields (num_output_fields=0) → returns all model_vars
!> Test 2: Valid subset in specific order → only those fields returned in order
!> Test 3: Invalid entry → error during init
program test_field_selection_unit
  use ESMF
  use tide_yaml_mod
  use tide_mod, only : tide_validate_output_fields
  use shr_kind_mod, only : cl => shr_kind_cl
  use, intrinsic :: iso_c_binding
  implicit none

  integer, parameter :: MAX_NAME_LEN = 64

  integer :: rc
  integer :: num_pass, num_fail

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

  ! -------------------------------------------------------------------
  ! Test 1: Empty output_fields → all model_vars returned
  ! -------------------------------------------------------------------
  call test_empty_output_fields(num_pass, num_fail)

  ! -------------------------------------------------------------------
  ! Test 2: Valid subset in specific order
  ! -------------------------------------------------------------------
  call test_valid_subset_order(num_pass, num_fail)

  ! -------------------------------------------------------------------
  ! Test 3: Invalid entry → error
  ! -------------------------------------------------------------------
  call test_invalid_entry_error(num_pass, num_fail)

  ! -------------------------------------------------------------------
  ! Summary
  ! -------------------------------------------------------------------
  write(*,'(a)') '====================================='
  write(*,'(a,i0,a,i0,a)') &
    'Field Selection Unit Tests: ', num_pass, '/', &
    num_pass + num_fail, ' passed'
  write(*,'(a)') '====================================='

  call ESMF_Finalize(rc=rc)

  if (num_fail > 0) stop 1

contains


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

  ! =================================================================
  ! Test 1: Empty output_fields (num_output_fields=0) → all model_vars
  ! Validates: Requirement 7.2
  ! =================================================================
  subroutine test_empty_output_fields(num_pass, num_fail)
    integer, intent(inout) :: num_pass, num_fail

    ! 3 known model_vars
    character(kind=c_char, len=1), target :: mv_buf1(MAX_NAME_LEN)
    character(kind=c_char, len=1), target :: mv_buf2(MAX_NAME_LEN)
    character(kind=c_char, len=1), target :: mv_buf3(MAX_NAME_LEN)
    type(c_ptr), target :: mv_ptrs(3)

    type(tide_output_config_t) :: output_cfg
    character(len=cl), allocatable :: validated_fields(:)
    integer :: num_validated, test_rc
    logical :: ok

    ! Pack model_vars as C strings
    call pack_c_string("temperature", mv_buf1)
    call pack_c_string("salinity",    mv_buf2)
    call pack_c_string("pressure",    mv_buf3)
    mv_ptrs(1) = c_loc(mv_buf1(1))
    mv_ptrs(2) = c_loc(mv_buf2(1))
    mv_ptrs(3) = c_loc(mv_buf3(1))

    ! Build output_config with num_output_fields=0, output_fields=C_NULL_PTR
    output_cfg%output_fields     = c_null_ptr
    output_cfg%num_output_fields = 0
    output_cfg%output_enabled    = 1
    output_cfg%target_grid_file  = c_null_ptr
    output_cfg%output_file       = c_null_ptr
    output_cfg%output_frequency  = 3600
    output_cfg%regrid_method     = c_null_ptr

    ! Call tide_validate_output_fields
    test_rc = -1
    num_validated = 0
    call tide_validate_output_fields(output_cfg, c_loc(mv_ptrs(1)), &
                                     3, validated_fields, &
                                     num_validated, test_rc)

    ok = .true.

    ! Verify rc == ESMF_SUCCESS
    if (test_rc /= ESMF_SUCCESS) then
      write(*,'(a)') "FAIL Test1: expected ESMF_SUCCESS but got failure"
      ok = .false.
    end if

    ! Verify num_validated == 3
    if (ok .and. num_validated /= 3) then
      write(*,'(a,i0)') "FAIL Test1: expected num_validated=3, got=", num_validated
      ok = .false.
    end if

    ! Verify validated_fields match all model_vars in order
    if (ok) then
      if (trim(validated_fields(1)) /= "temperature") then
        write(*,'(a,a,a)') "FAIL Test1: field(1) expected 'temperature', got '", &
          trim(validated_fields(1)), "'"
        ok = .false.
      end if
    end if
    if (ok) then
      if (trim(validated_fields(2)) /= "salinity") then
        write(*,'(a,a,a)') "FAIL Test1: field(2) expected 'salinity', got '", &
          trim(validated_fields(2)), "'"
        ok = .false.
      end if
    end if
    if (ok) then
      if (trim(validated_fields(3)) /= "pressure") then
        write(*,'(a,a,a)') "FAIL Test1: field(3) expected 'pressure', got '", &
          trim(validated_fields(3)), "'"
        ok = .false.
      end if
    end if

    if (ok) then
      write(*,'(a)') "PASS Test1: Empty output_fields returns all model_vars in order"
      num_pass = num_pass + 1
    else
      num_fail = num_fail + 1
    end if

    if (allocated(validated_fields)) deallocate(validated_fields)

  end subroutine test_empty_output_fields


  ! =================================================================
  ! Test 2: Valid subset in specific order → only those fields in order
  ! Validates: Requirements 7.1, 7.4
  ! =================================================================
  subroutine test_valid_subset_order(num_pass, num_fail)
    integer, intent(inout) :: num_pass, num_fail

    ! 4 known model_vars: "alpha", "beta", "gamma", "delta"
    character(kind=c_char, len=1), target :: mv_buf1(MAX_NAME_LEN)
    character(kind=c_char, len=1), target :: mv_buf2(MAX_NAME_LEN)
    character(kind=c_char, len=1), target :: mv_buf3(MAX_NAME_LEN)
    character(kind=c_char, len=1), target :: mv_buf4(MAX_NAME_LEN)
    type(c_ptr), target :: mv_ptrs(4)

    ! output_fields: ["gamma", "alpha"] (reversed subset)
    character(kind=c_char, len=1), target :: of_buf1(MAX_NAME_LEN)
    character(kind=c_char, len=1), target :: of_buf2(MAX_NAME_LEN)
    type(c_ptr), target :: of_ptrs(2)

    type(tide_output_config_t) :: output_cfg
    character(len=cl), allocatable :: validated_fields(:)
    integer :: num_validated, test_rc
    logical :: ok

    ! Pack model_vars
    call pack_c_string("alpha", mv_buf1)
    call pack_c_string("beta",  mv_buf2)
    call pack_c_string("gamma", mv_buf3)
    call pack_c_string("delta", mv_buf4)
    mv_ptrs(1) = c_loc(mv_buf1(1))
    mv_ptrs(2) = c_loc(mv_buf2(1))
    mv_ptrs(3) = c_loc(mv_buf3(1))
    mv_ptrs(4) = c_loc(mv_buf4(1))

    ! Pack output_fields: ["gamma", "alpha"]
    call pack_c_string("gamma", of_buf1)
    call pack_c_string("alpha", of_buf2)
    of_ptrs(1) = c_loc(of_buf1(1))
    of_ptrs(2) = c_loc(of_buf2(1))

    ! Build output_config with the subset
    output_cfg%output_fields     = c_loc(of_ptrs(1))
    output_cfg%num_output_fields = 2
    output_cfg%output_enabled    = 1
    output_cfg%target_grid_file  = c_null_ptr
    output_cfg%output_file       = c_null_ptr
    output_cfg%output_frequency  = 3600
    output_cfg%regrid_method     = c_null_ptr

    ! Call tide_validate_output_fields
    test_rc = -1
    num_validated = 0
    call tide_validate_output_fields(output_cfg, c_loc(mv_ptrs(1)), &
                                     4, validated_fields, &
                                     num_validated, test_rc)

    ok = .true.

    ! Verify rc == ESMF_SUCCESS
    if (test_rc /= ESMF_SUCCESS) then
      write(*,'(a)') "FAIL Test2: expected ESMF_SUCCESS but got failure"
      ok = .false.
    end if

    ! Verify num_validated == 2
    if (ok .and. num_validated /= 2) then
      write(*,'(a,i0)') "FAIL Test2: expected num_validated=2, got=", num_validated
      ok = .false.
    end if

    ! Verify validated_fields == ["gamma", "alpha"] in that order
    if (ok) then
      if (trim(validated_fields(1)) /= "gamma") then
        write(*,'(a,a,a)') "FAIL Test2: field(1) expected 'gamma', got '", &
          trim(validated_fields(1)), "'"
        ok = .false.
      end if
    end if
    if (ok) then
      if (trim(validated_fields(2)) /= "alpha") then
        write(*,'(a,a,a)') "FAIL Test2: field(2) expected 'alpha', got '", &
          trim(validated_fields(2)), "'"
        ok = .false.
      end if
    end if

    if (ok) then
      write(*,'(a)') "PASS Test2: Valid subset returned in specified order"
      num_pass = num_pass + 1
    else
      num_fail = num_fail + 1
    end if

    if (allocated(validated_fields)) deallocate(validated_fields)

  end subroutine test_valid_subset_order


  ! =================================================================
  ! Test 3: Invalid entry → error during init
  ! Validates: Requirement 7.3
  ! =================================================================
  subroutine test_invalid_entry_error(num_pass, num_fail)
    integer, intent(inout) :: num_pass, num_fail

    ! 3 known model_vars: "sst", "ice", "wind"
    character(kind=c_char, len=1), target :: mv_buf1(MAX_NAME_LEN)
    character(kind=c_char, len=1), target :: mv_buf2(MAX_NAME_LEN)
    character(kind=c_char, len=1), target :: mv_buf3(MAX_NAME_LEN)
    type(c_ptr), target :: mv_ptrs(3)

    ! output_fields: ["sst", "BOGUS_FIELD"]
    character(kind=c_char, len=1), target :: of_buf1(MAX_NAME_LEN)
    character(kind=c_char, len=1), target :: of_buf2(MAX_NAME_LEN)
    type(c_ptr), target :: of_ptrs(2)

    type(tide_output_config_t) :: output_cfg
    character(len=cl), allocatable :: validated_fields(:)
    integer :: num_validated, test_rc
    logical :: ok

    ! Pack model_vars
    call pack_c_string("sst",  mv_buf1)
    call pack_c_string("ice",  mv_buf2)
    call pack_c_string("wind", mv_buf3)
    mv_ptrs(1) = c_loc(mv_buf1(1))
    mv_ptrs(2) = c_loc(mv_buf2(1))
    mv_ptrs(3) = c_loc(mv_buf3(1))

    ! Pack output_fields: ["sst", "BOGUS_FIELD"]
    call pack_c_string("sst",         of_buf1)
    call pack_c_string("BOGUS_FIELD", of_buf2)
    of_ptrs(1) = c_loc(of_buf1(1))
    of_ptrs(2) = c_loc(of_buf2(1))

    ! Build output_config with the invalid subset
    output_cfg%output_fields     = c_loc(of_ptrs(1))
    output_cfg%num_output_fields = 2
    output_cfg%output_enabled    = 1
    output_cfg%target_grid_file  = c_null_ptr
    output_cfg%output_file       = c_null_ptr
    output_cfg%output_frequency  = 3600
    output_cfg%regrid_method     = c_null_ptr

    ! Call tide_validate_output_fields — expect ESMF_FAILURE
    test_rc = ESMF_SUCCESS
    num_validated = 0
    call tide_validate_output_fields(output_cfg, c_loc(mv_ptrs(1)), &
                                     3, validated_fields, &
                                     num_validated, test_rc)

    ok = .true.

    ! Verify rc == ESMF_FAILURE
    if (test_rc /= ESMF_FAILURE) then
      write(*,'(a)') "FAIL Test3: expected ESMF_FAILURE but got success"
      ok = .false.
    end if

    if (ok) then
      write(*,'(a)') "PASS Test3: Invalid entry correctly returns ESMF_FAILURE"
      num_pass = num_pass + 1
    else
      num_fail = num_fail + 1
    end if

    if (allocated(validated_fields)) deallocate(validated_fields)

  end subroutine test_invalid_entry_error

end program test_field_selection_unit
